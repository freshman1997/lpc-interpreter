#include <iostream>
#include <string>
#if LUNIX
#include <unistd.h>
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
	ExpressionVisitor *doc = parser.parse("1.txt");
	/*Visitor *vis = new ConcretVisitor;
	vis->visit(dynamic_cast<AbstractExpression *>(doc));*/
	CodeGenerator g;
	g.generate(dynamic_cast<AbstractExpression *>(doc));
	g.dump();

	/*
	// find all files
	vector<CompileFile> files;
	for (auto &it : files) {
		if (it.isFile) {
			
		}
	}
	*/
	return 0;
}
