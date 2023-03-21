#include <iostream>
#include <ctime>
#if WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "os/os.h"
#include "efun/efun.h"
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
#if LUNIX
	char buf[PATHNAME_MAX];
	if (NULL == getcwd(buf, sizeof(buf))) {
        perror("getcwd error");
        exit(1);
    }

	cwd = buf;
#else
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
#endif
    lint64_t seed = time(NULL);
	//debug_message("xxxxxxxxxxxxxxxxxxxxx %d:%d,%d,%d\n", 100, _abs(-1), hash_("hello"), luaS_hash("hello", 5, (unsigned int)(seed)));
	os::init_seed(seed);
	cout << "random value: " << os::random() << endl;

    lpc_vm_t *vm = lpc_vm_t::create_vm();
    vm->bootstrap();
	return 0;
}
