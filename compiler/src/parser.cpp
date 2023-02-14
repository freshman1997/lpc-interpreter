#include "parser.h"

void Parser::init_base_include(const vector<string> &includes)
{

}

void Parser::preprocessing(Token *tok)
{
    Token *cur = tok;
    Token *pre = nullptr;
    while (cur) {
        if (cur->kind == TokenKind::k_identity) {
            // 替换原来的Token，copy macro 然后链接起来
            if (macros.count(cur->strval)) {
                Macro *macro = macros[cur->strval];
                // built in macro
                if (macro->handler) {
                    macro->handler(cur);
                    continue;
                }

                // replace and insert body, and chain them

            }
            else if (cur->kind == TokenKind::k_key_word && cur->strval == "define") {
                // read params and body and insert new macro
                
            }
        }
        pre = cur;
        cur = cur->next;
    }
}

// 常量折叠，分支处理，死代码删除，循环无关外提，

ExpressionVisitor * Parser::parse(const char *filename)
{
    return NULL;
}
