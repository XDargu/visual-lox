#ifndef loxcpp_compiler_h
#define loxcpp_compiler_h

#include <string>
#include <array>
#include <set>

#include "Chunk.h"
#include "Scanner.h"
#include "Object.h"

enum class Precedence : uint8_t
{
    NONE = 0,
    ASSIGNMENT,  // =
    OR,          // or
    AND,         // and
    EQUALITY,    // == !=
    COMPARISON,  // < > <= >=
    TERM,        // + -
    FACTOR,      // * /
    UNARY,       // ! -
    RANGE,       // ..
    CALL,        // . ()
    SUBSCRIPT,   //  []
    PRIMARY
};

struct Parser
{
    Token current;
    Token previous;
    bool hadError = false;
    bool panicMode = false;
};

struct Local
{
    Token name;
    int depth = -1;
    bool constant = false;
    bool isCaptured = false;
};

struct Upvalue
{
    uint8_t index = 0;
    bool isLocal = false;
};

 enum class FunctionType 
 {
    FUNCTION,
    INITIALIZER,
    METHOD,
    SCRIPT
};

struct CompilerScope
{
    CompilerScope();
    CompilerScope(FunctionType type, CompilerScope* enclosing, Token* token);

    CompilerScope* enclosing;
    ObjFunction* function;
    FunctionType type;

    std::array<Local, UINT8_COUNT> locals;
    int localCount;
    std::array<Upvalue, UINT8_COUNT> upvalues;
    int scopeDepth;
};

struct ClassCompilerScope
{
    struct ClassCompilerScope* enclosing;
};

inline uint8_t OpByte(OpCode opCode) { return static_cast<uint8_t>(opCode); }

class Compiler
{
    using ParseFn = void (Compiler::*)(bool canAssign);

    struct ParseRule
    {
        ParseRule(ParseFn prefix, ParseFn infix, Precedence precedence);

        ParseFn prefix;
        ParseFn infix;
        Precedence precedence;
    };

    using Rules = std::array<ParseRule, static_cast<size_t>(TokenType::EOFILE) + 1>;

    const Rules rules =
    {
      ParseRule(&Compiler::grouping,  &Compiler::call,     Precedence::CALL),        // LEFT_PAREN    
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // RIGHT_PAREN   
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // LEFT_BRACE    
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // RIGHT_BRACE   
      ParseRule(&Compiler::list,      &Compiler::subscript,Precedence::SUBSCRIPT), // LEFT_BRACKET  
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // RIGHT_BRACKET 
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // COMMA         
      ParseRule(nullptr,              &Compiler::dot,      Precedence::CALL),        // DOT           
      ParseRule(&Compiler::unary,     &Compiler::binary,   Precedence::TERM),        // MINUS         
      ParseRule(nullptr,              &Compiler::binary,   Precedence::TERM),        // PLUS          
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // COLON....     
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // SEMICOLON     
      ParseRule(nullptr,              &Compiler::binary,   Precedence::FACTOR),      // SLASH         
      ParseRule(nullptr,              &Compiler::binary,   Precedence::FACTOR),      // STAR          
      ParseRule(&Compiler::unary,     nullptr,             Precedence::NONE),        // BANG          
      ParseRule(nullptr,              &Compiler::binary,   Precedence::EQUALITY),    // BANG_EQUAL    
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // EQUAL         
      ParseRule(nullptr,              &Compiler::binary,   Precedence::EQUALITY),    // EQUAL_EQUAL   
      ParseRule(nullptr,              &Compiler::binary,   Precedence::COMPARISON),  // GREATER       
      ParseRule(nullptr,              &Compiler::binary,   Precedence::COMPARISON),  // GREATER_EQUAL 
      ParseRule(nullptr,              &Compiler::binary,   Precedence::COMPARISON),  // LESS          
      ParseRule(nullptr,              &Compiler::binary,   Precedence::COMPARISON),  // LESS_EQUAL    
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // PLUS_PLUS     
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // MINUS_MINUS   // TODO
      ParseRule(nullptr,              &Compiler::binary,   Precedence::RANGE),       // DOT_DOT       // TODO
      ParseRule(nullptr,              &Compiler::binary,   Precedence::FACTOR),      // PERCENTAGE    // TODO
      ParseRule(&Compiler::variable,  nullptr,             Precedence::NONE),        // IDENTIFIER    // TODO
      ParseRule(&Compiler::string,    nullptr,             Precedence::NONE),        // STRING        
      ParseRule(&Compiler::number,    nullptr,             Precedence::NONE),        // NUMBER        
      ParseRule(nullptr,              &Compiler::and_,     Precedence::AND),         // AND           
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // CLASS         
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // ELSE          
      ParseRule(&Compiler::literal,   nullptr,             Precedence::NONE),        // FALSE         
      ParseRule(&Compiler::funExpr,   nullptr,             Precedence::NONE),        // FUN           
      ParseRule(nullptr,              nullptr,             Precedence::RANGE),       // FOR           
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // IF            
      ParseRule(&Compiler::literal,   nullptr,             Precedence::NONE),        // NIL           
      ParseRule(nullptr,              &Compiler::or_,      Precedence::OR),          // OR            
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // PRINT         
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // RETURN        
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // SUPER         
      ParseRule(&Compiler::this_,     nullptr,             Precedence::NONE),        // THIS          
      ParseRule(&Compiler::literal,   nullptr,             Precedence::NONE),        // TRUE          
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // VAR           
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // CONST         
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // WHILE         
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // MATCH         
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // CASE          
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // BREAK         
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // CONTINUE      
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // IN            
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // ERROR         
      ParseRule(nullptr,              nullptr,             Precedence::NONE),        // EOFILE        
    };

public:
    
    Compiler();

    void debugScanner();
    ObjFunction* compile(const std::string& source);
    void beginCompile();
    bool check(TokenType type);

    void advance();
    void consume(TokenType type, const char* message);
    bool match(TokenType type);
    void emitDWord(uint32_t byte);
    void emitShort(uint16_t byte);
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t byte1, uint8_t byte2);
    void emitLoop(size_t loopStart);
    size_t emitJump(uint8_t instruction);
    void emitOpWithValue(OpCode shortOp, OpCode longOp, uint32_t value);
    void emitReturn();
    uint32_t makeConstant(Value value);
    void emitConstant(Value value);
    void emitVariable(const Token& name, bool shouldAssign, bool ignoreConst = false);
    void patchJump(size_t offset);

    ObjFunction* endCompiler();

    void binary(bool canAssign);
    void call(bool canAssign);
    void dot(bool canAssign);
    void subscript(bool canAssign);
    void literal(bool canAssign);
    void grouping(bool canAssign);
    void number(bool canAssign);
    void or_(bool canAssign);
    void string(bool canAssign);
    void namedVariable(const Token& name, bool canAssign);
    void variable(bool canAssign);
    void this_(bool canAssign);
    void unary(bool canAssign);
    void funExpr(bool canAssign);
    void list(bool canAssign);
    void parsePrecedence(Precedence precedence);
    uint32_t identifierConstant(const Token& name);
    bool identifiersEqual(const Token& a, const Token& b);
    int resolveLocal(const CompilerScope& compilerScope, const Token& name);
    int addUpvalue(CompilerScope& compilerScope, uint8_t index, bool isLocal);
    int resolveUpvalue(CompilerScope& compilerScope, const Token& name);
    bool isLocalConst(const CompilerScope& compilerScope, int index);
    bool isUpvalueConst(const CompilerScope& compilerScope, int index);
    bool isGlobalConst(uint8_t index);
    void addLocal(const Token& name, bool isConstant);
    void declareVariable(bool isConstant);
    uint32_t parseVariable(const char* errorMessage, bool isConstant);
    void markInitialized();
    void defineVariable(uint32_t global);
    uint8_t argumentList();
    void and_(bool canAssign);
    const ParseRule* getRule(TokenType type) const;
    void expression();
    void block();
    void function(FunctionType type);
    void method();
    void classDeclaration();
    void funDeclaration();
    void beginScope();
    void endScope();
    void varDeclaration(bool isConstant);
    void expressionStatement();
    void forStatement();
    void forInStatement();
    void ifStatement();
    void printStatement();
    void returnStatement();
    void whileStatement();
    void matchStatement();
    void pattern();
    void synchronize();
    void declaration();
    void statement();

    void errorAtCurrent(const std::string& message);
    void error(const std::string& message);
    void errorAt(const Token& token, const std::string& message);

    Chunk* currentChunk() { return &current->function->chunk; }

    Scanner scanner;
    Parser parser;
    CompilerScope compilerData;
    std::set<uint32_t> constGlobals;

    CompilerScope* current;
    ClassCompilerScope* currentClass;
};

#endif
