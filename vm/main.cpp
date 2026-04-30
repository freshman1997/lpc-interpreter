#include <iostream>
#include <ctime>
#include <cstdint>
#if WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "os/os.h"
#include "vm/runtime/self_check.h"
#include "vm/runtime/entry.h"
#include "cli/cli.h"

#ifdef _POSIX_PATH_MAX
#define PATHNAME_MAX		POSIX_PATH_MAX
#else
#define PATHNAME_MAX		1000
#endif

using namespace std;

static string cwd;

string get_cwd()
{
	return cwd;
}

int main(int argc, char **argv)
{
	os::register_exception_handler();

#if WIN32
	char *buf;
	if ( (buf = getcwd( NULL, 0 )) == NULL ) {
		perror( "_getcwd error" );
		return -1;
	} else {
		cwd = buf;
		free(buf);
	}
	system("chcp 65001");
	
#else
	char buf[PATHNAME_MAX];
	if (NULL == getcwd(buf, sizeof(buf))) {
        perror("getcwd error");
        exit(1);
    }

	cwd = buf;
#endif

	if (argc > 1) {
		if (argc == 2 && std::string(argv[1]) == "--self-check") {
			return lpc::vm::RunSelfChecks();
		}
		if (argc == 2 && std::string(argv[1]) == "--perf") {
			return lpc::vm::RunPerfBenchmarks();
		}
		if (argc == 2 && std::string(argv[1]) == "--self-check-perf") {
			int r = lpc::vm::RunSelfChecks();
			if (r != 0) return r;
			return lpc::vm::RunPerfBenchmarks();
		}
		return lpc::cli::Run(argc, argv);
	}
    int64_t seed = time(NULL);
	srand(static_cast<unsigned>(seed));
	os::init_seed(seed);
	cout << "random value: " << os::random() << endl;

	time_t start = clock();

	lpc::vm::RuntimeError s = lpc::vm::RunEntryModule("");
	if (!s.ok()) {
		cerr << "run failed: " << s.message << endl;
	}

	cout << "Exited normally.\n";
	time_t end = clock();
	cout << "spent: " << (end - start) << endl;
	cout.flush();
	
	return 0;
}
