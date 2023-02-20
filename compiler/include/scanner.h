#ifndef __COMPILER_SCANNER_H__
#define __COMPILER_SCANNER_H__
// 单文件处理
#include <fstream>
#include "type_decl.h"
#include "token.h"

class Scanner
{
public:
	Scanner(const char * file);
	Scanner() {}
	Scanner(Scanner &s) = delete;
	Scanner & operator = (Scanner &) = delete;

	luint8_t peek();
	luint8_t peek1();
	luint8_t read();
	bool is_eof();
	void back();
	~Scanner(){ if (input.is_open()) input.close(); }

	void set_file(const char *file);
private:
	void init();
	void read_more();
	const char *filename = nullptr;	
	std::ifstream input;
	luint8_t one = 0;
	luint8_t two = 0;
	luint8_t use = 0;
	bool eof = false;
};

class TokenReader
{
public:
	void set_scanner(Scanner *);
	Token * get_head();
	void set_head(Token *head);

	Token * get_cur();
	Token * next();

	bool is_eof();
private:
	int line;
	int col;
	Token *head = nullptr;
	Token *cur = nullptr;
    Scanner *scanner = nullptr;
};

#endif

