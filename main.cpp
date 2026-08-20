#include <iostream>

enum Token 
{
    tok_eof = -1,

    tok_def = -2,
    tok_extern = -3,

    tok_identifier = -4,
    tok_number = -5,
};
static std::string IdentifierStr;
static double NumVal;

static int gettok()
{
    static int LastChar = ' ';

    while (isspace(LastChar)) LastChar = getchar();
}

int main()
{

}