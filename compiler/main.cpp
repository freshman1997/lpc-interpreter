#include <iostream>
#include <string>
#if LUNIX
#include <unistd.h>
#else

#endif

using namespace std;

#ifdef _POSIX_PATH_MAX
#define PATHNAME_MAX		POSIX_PATH_MAX
#else
#define PATHNAME_MAX		1000
#endif

static string cwd;
static string parent;

int main(int argc, char **argv)
{
	char buf[PATHNAME_MAX];
#if LUNIX
	if (NULL == getcwd(buf, sizeof(buf))) {
        perror("getcwd error");
        exit(1);
    }
#else

#endif
	cwd = buf;
	for (int i = cwd.size() - 1; i >= 0; --i) {
		if (cwd[i] == '/') {
			parent = cwd.substr(0, i);
			break;
		}
	}

	cout << parent << endl;
	return 0;
}
