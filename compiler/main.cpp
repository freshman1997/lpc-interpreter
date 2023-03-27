#include <iostream>
#include <string>
#if LUNIX
#include <unistd.h>
#include <sys/stat.h>
#else
#include <direct.h>
#endif

#include "parser.h"
#include "codegen.h"

using namespace std;

#ifdef _POSIX_PATH_MAX
#define PATHNAME_MAX		POSIX_PATH_MAX
#else
#define PATHNAME_MAX		1000
#endif

static string cwd;
static string parent;

string get_cwd()
{
	return cwd;
}

string get_father()
{
	return parent;
}

static void recurve_mkdir(const string destPrefix)
{
    string cwd = get_cwd();
    luint32_t idx = 0;
    for (;;) {
#ifdef WIN32
#else
    luint32_t pos = destPrefix.find("/", idx);
    if (pos == string::npos) {
        break;
    }

    string dir = cwd + "/" + destPrefix.substr(0, pos);
    if (access(dir.c_str(), 0) == -1) {
        if (mkdir(dir.c_str(), 0777)) //如果不存在就用mkdir函数来创建
        {
            printf("creat dir failed!!!\n");
            exit(-1);
        }
    }

    idx = pos + 1;
#endif
    }
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
#endif
	for (int i = cwd.size() - 1; i >= 0; --i) {
		if (cwd[i] == '/') {
			parent = cwd.substr(0, i);
			break;
		}
	}

	cout << parent << endl;
	Parser parser;
	//init_sfun("/rc/simulate.c", parser);

	vector<AbstractExpression *> *docs = parser.parse(cwd.c_str());
	for (auto &it : *docs) {
		DocumentExpression *doc = dynamic_cast<DocumentExpression *>(it);
		try {
			// 内部可以使用，但是不能删除
			CodeGenerator g;
			g.set_parsed_files(docs);
			g.generate(it);
			g.dump();

			cout << "[success] " << doc->file_name << "\n";
		} catch (...) {
			std::cout << "error occured in file: " << doc->file_name << " !!!\n";
			continue;
		}
	}

	return 0;
}
