#ifndef loxcpp_scanner_h
#define loxcpp_scanner_h

#include <vector>
#include <string>
#include <unordered_map>

enum class TokenType
{
    // Single-character tokens.
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE, LEFT_BRACKET, RIGHT_BRACKET,
    COMMA, DOT, MINUS, PLUS, COLON, SEMICOLON, SLASH, STAR,

    // One or two character tokens.
    BANG, BANG_EQUAL,
    EQUAL, EQUAL_EQUAL,
    GREATER, GREATER_EQUAL,
    LESS, LESS_EQUAL, PLUS_PLUS, MINUS_MINUS, DOT_DOT,
    PERCENTAGE,

    // Literals.
    IDENTIFIER, STRING, NUMBER,

    // Keywords.
    AND, CLASS, ELSE, FALSE, FUN, FOR, IF, NIL, OR,
    PRINT, RETURN, SUPER, THIS, TRUE, VAR, CONST, WHILE,
    MATCH, CASE, BREAK, CONTINUE, IN,

    ERROR, EOFILE
};

const std::string& tokenTypeToString(const TokenType type);

struct Token
{
    Token();
    Token(const TokenType type, const char* start, int length, const size_t line);

    std::string toString() const { return std::string(start, length); }

    TokenType type;
    const char* start; // TODO: substitue for string view
    int length;
    int line;
};

class Scanner
{
public:

    Scanner() = default;

    void init(const std::string& sourceText)
    {
        source = sourceText;
        start = 0;
        current = 0;
        line = 1;
    }

    Token scanToken()
    {
        start = current;

        if (isAtEnd()) return makeToken(TokenType::EOFILE);

        skipWhitespace();

        if (isAtEnd()) return makeToken(TokenType::EOFILE);

        start = current;

        const char c = advance();

        if (isAlpha(c)) return identifier();
        if (isDigit(c)) return number();

        switch (c)
        {
            case '(': return makeToken(TokenType::LEFT_PAREN);
            case ')': return makeToken(TokenType::RIGHT_PAREN);
            case '{': return makeToken(TokenType::LEFT_BRACE);
            case '}': return makeToken(TokenType::RIGHT_BRACE);
            case '[': return makeToken(TokenType::LEFT_BRACKET);
            case ']': return makeToken(TokenType::RIGHT_BRACKET);
            case ',': return makeToken(TokenType::COMMA);
            case '.': return makeToken(match('.') ? TokenType::DOT_DOT : TokenType::DOT);
            case ';': return makeToken(TokenType::SEMICOLON);
            case ':': return makeToken(TokenType::COLON);
            case '-': return makeToken(match('-') ? TokenType::MINUS_MINUS : TokenType::MINUS);
            case '+': return makeToken(match('+') ? TokenType::PLUS_PLUS : TokenType::PLUS);
            case '*': return makeToken(TokenType::STAR);
            case '%': return makeToken(TokenType::PERCENTAGE);
            case '!': return makeToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
            case '=': return makeToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
            case '<': return makeToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
            case '>': return makeToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
            case '/': return makeToken(TokenType::SLASH);
            case '"': return string();
        }
        return errorToken("Unexpected character.");
    }

private:

    void skipWhitespace()
    {
        for (;;)
        {
            char c = peek();
            switch (c) {
                case ' ':
                case '\r':
                case '\t':
                    advance();
                    break;
                case '\n':
                    line++;
                    advance();
                    break;
                case '/':
                    if (peekNext() == '/')
                    {
                        // A comment goes until the end of the line.
                        while (peek() != '\n' && !isAtEnd()) advance();
                    }
                    else if (peekNext() == '*')
                    {
                        advance();
                        advance();
                        multiLineComment();
                    }
                    else
                    {
                        return;
                    }
                    break;
                default:
                    return;
            }
        }
    }

    char advance()
    {
        return source.at(current++);
    }

    Token makeToken(TokenType type)
    {
        const int length = static_cast<int>(current - start);
        const char* startChar = &source[start];
        return Token(type, startChar, length, line);
    }

    Token errorToken(const char* message)
    {
        return Token(TokenType::ERROR, message, static_cast<int>(strlen(message)), line);
    }

    bool match(const char expected)
    {
        if (isAtEnd()) return false;
        if (source.at(current) != expected) return false;

        current++;
        return true;
    }

    void multiLineComment()
    {
        // Multi line comment, ignore until */
        while (!(peek() == '*' && peekNext() == '/') && !isAtEnd()) advance();

        // Consume the "*"
        advance();

        // Consume the "/"
        advance();
    }

    Token number()
    {
        while (isDigit(peek())) advance();

        // Look for a fractional part.
        if (peek() == '.' && isDigit(peekNext()))
        {
            // Consume the "."
            advance();

            while (isDigit(peek())) advance();
        }

        return makeToken(TokenType::NUMBER);
    }

    Token string()
    {
        while (peek() != '"' && !isAtEnd())
        {
            if (peek() == '\n') line++;
            advance();
        }

        if (isAtEnd())
        {
            return errorToken("Unterminated string.");
        }

        // The closing ".
        advance();
        return makeToken(TokenType::STRING);
    }

    Token identifier()
    {
        while (isAlphaNumeric(peek())) advance();

        return makeToken(identifierType());
    }

    TokenType identifierType()
    {
        switch (source.at(start))
        {
            case 'a': return checkKeyword(1, 2, "nd", TokenType::AND);
            case 'b': return checkKeyword(1, 4, "reak", TokenType::BREAK);
            case 'c':
                if (current - start > 1)
                {
                    switch (source.at(start + 1))
                    {
                        case 'a': return checkKeyword(2, 2, "se", TokenType::CASE);
                        case 'l': return checkKeyword(2, 3, "ass", TokenType::CLASS);
                        case 'o': 
                            if (current - start > 2)
                            {
                                switch (source.at(start + 2))
                                {
                                    case 'n':
                                        if (current - start > 3)
                                        {
                                            switch (source.at(start + 3))
                                            {
                                            case 's': return checkKeyword(4, 1, "t", TokenType::CONST);
                                            case 't': return checkKeyword(4, 2, "inue", TokenType::CONTINUE);
                                            }
                                        }
                                        break;
                                }
                            }
                            break;
                    }
                }
                break;
            case 'e': return checkKeyword(1, 3, "lse", TokenType::ELSE);
            case 'i':
                if (current - start > 1)
                {
                    switch (source.at(start + 1))
                    {
                    case 'f': return checkKeyword(2, 0, "", TokenType::IF);
                    case 'n': return checkKeyword(2, 0, "", TokenType::IN);
                    }
                }
                break;
            case 'n': return checkKeyword(1, 2, "il", TokenType::NIL);
            case 'm': return checkKeyword(1, 4, "atch", TokenType::MATCH);
            case 'o': return checkKeyword(1, 1, "r", TokenType::OR);
            case 'p': return checkKeyword(1, 4, "rint", TokenType::PRINT);
            case 'r': return checkKeyword(1, 5, "eturn", TokenType::RETURN);
            case 's': return checkKeyword(1, 4, "uper", TokenType::SUPER);
            case 'v': return checkKeyword(1, 2, "ar", TokenType::VAR);
            case 'w': return checkKeyword(1, 4, "hile", TokenType::WHILE);
            case 'f':
                if (current - start > 1)
                {
                    switch (source.at(start + 1))
                    {
                        case 'a': return checkKeyword(2, 3, "lse", TokenType::FALSE);
                        case 'o': return checkKeyword(2, 1, "r", TokenType::FOR);
                        case 'u': return checkKeyword(2, 1, "n", TokenType::FUN);
                    }
                }
                break;
            case 't':
                if (current - start > 1)
                {
                    switch (source.at(start + 1))
                    {
                    case 'h': return checkKeyword(2, 2, "is", TokenType::THIS);
                    case 'r': return checkKeyword(2, 2, "ue", TokenType::TRUE);
                    }
                }
                break;
        }

        return TokenType::IDENTIFIER;
    }

    TokenType checkKeyword(size_t keyStart, size_t keyLength, const char* rest, TokenType type)
    {
        if (current - start == keyStart + keyLength && 
            memcmp(&source.at(start) + keyStart, rest, keyLength) == 0)
        {
            return type;
        }

        return TokenType::IDENTIFIER;
    }

    char peek() const
    {
        if (isAtEnd()) return '\0';
        return source.at(current);
    }

    char peekNext() const
    {
        if (current + 1 >= source.length()) return '\0';
        return source.at(current + 1);
    }

    bool isDigit(const char c) const
    {
        return c >= '0' && c <= '9';
    }

    bool isAlpha(const char c) const
    {
        return (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '_';
    }

    bool isAlphaNumeric(const char c) const
    {
        return isAlpha(c) || isDigit(c);
    }

    bool isAtEnd() const
    {
        return current >= source.length();
    }

    std::string source;
    size_t start = 0;
    size_t current = 0;
    size_t line = 1;
};

#endif