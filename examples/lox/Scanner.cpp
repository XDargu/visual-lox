#include "Scanner.h"

const std::string& tokenTypeToString(const TokenType type)
{
    static std::string values[] = {
        "LEFT_PAREN", "RIGHT_PAREN", "LEFT_BRACE", "RIGHT_BRACE", "LEFT_BRACKET", "RIGHT_BRACKET",
        "COMMA", "DOT", "MINUS", "PLUS", "COLON", "SEMICOLON", "SLASH", "STAR",

        // One or two character tokens.
        "BANG", "BANG_EQUAL",
        "EQUAL", "EQUAL_EQUAL",
        "GREATER", "GREATER_EQUAL",
        "LESS", "LESS_EQUAL", "PLUS_PLUS", "MINUS_MINUS", "DOT_DOT"
        "PERCENTAGE",

        // Literals.
        "IDENTIFIER", "STRING", "NUMBER",

        // Keywords.
        "AND", "CLASS", "ELSE", "FALSE", "FUN", "FOR", "IF", "NIL", "OR",
        "PRINT", "RETURN", "SUPER", "THIS", "TRUE", "VAR", "CONST", "WHILE",
        "MATCH", "CASE", "BREAK", "CONTINUE", "IN",

        "ERROR", "EOFILE"
    };

    return values[static_cast<size_t>(type)];
}

Token::Token()
    : type(TokenType::ERROR)
    , start(nullptr)
    , length(0)
    , line(0)
{}

Token::Token(const TokenType type, const char* start, int length, const size_t line)
    : type(type)
    , start(start)
    , length(length)
    , line(line)
{
}