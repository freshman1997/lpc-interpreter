#include <iostream>
#include <ctime>
#if WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "os/os.h"
#include "runtime/vm.h"
#include "runtime/interpreter.h"
#include "type/lpc_mapping.h"

#ifdef _POSIX_PATH_MAX
#define PATHNAME_MAX		POSIX_PATH_MAX
#else
#define PATHNAME_MAX		1000
#endif

extern void debug_message(const char *fmt, ...);
extern unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);

using namespace std;

static string cwd;
static string parent;

string get_cwd()
{
	return cwd;
}

int main(int argc, char **argv)
{
	os::register_exception_handler();
	
#if WIN32
	char *buf;
	// Get the current working directory:
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
    lint64_t seed = time(NULL);
	srand(seed);
	//debug_message("xxxxxxxxxxxxxxxxxxxxx %d:%d,%d,%d\n", 100, _abs(-1), hash_("hello"), luaS_hash("hello", 5, (unsigned int)(seed)));
	os::init_seed(seed);
	cout << "random value: " << os::random() << endl;

	time_t start = clock();
    lpc_vm_t *vm = lpc_vm_t::create_vm();
    vm->bootstrap();

	//vm->on_debug_mode();
	//vm->start_debug();
	vm->run_main();

	cout << "Exited normally.\n";
	time_t end = clock();
	cout << "spent: " << (end - start) << endl;
	cout.flush();
	
	return 0;
}
