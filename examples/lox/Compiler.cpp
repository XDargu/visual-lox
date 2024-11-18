#include "Compiler.h"

#include <iostream>
#include <iomanip>

#include "Debug.h"
#include "Object.h"

Precedence nextPrecedence(Precedence precedence) { return static_cast<Precedence>(static_cast<int>(precedence) + 1); }

CompilerScope::CompilerScope()
    : enclosing(nullptr)
    , function(nullptr)
    , type(FunctionType::SCRIPT)
    , localCount(0)
    , scopeDepth(0)
{
}

CompilerScope::CompilerScope(FunctionType type, CompilerScope* enclosing, Token* token)
    : enclosing(enclosing)
    , function(nullptr)
    , type(type)
    , localCount(0)
    , scopeDepth(0)
{
    function = newFunction();

    Local& local = locals[localCount++];

    if (type != FunctionType::SCRIPT)
    {
        function->name = copyString(token->start, token->length);
    }

    local.depth = 0;
    local.name.start = "";
    local.name.length = 0;
    local.constant = false;
    local.isCaptured = false;

    if (type != FunctionType::FUNCTION)
    {
        local.name.start = "this";
        local.name.length = 4;
    }
    else
    {
        local.name.start = "";
        local.name.length = 0;
    }
}

Compiler::ParseRule::ParseRule(ParseFn prefix, ParseFn infix, Precedence precedence)
    : prefix(prefix)
    , infix(infix)
    , precedence(precedence)
{}

void Compiler::debugScanner()
{
    int line = -1;
    for (;;)
    {
        const Token token = scanner.scanToken();
        if (token.line != line)
        {
            std::cout << std::setfill('0') << std::setw(4) << token.line << " ";
            line = token.line;
        }
        else
        {
            std::cout <<  "   | ";
        }
        std::cout << tokenTypeToString(token.type) << " " << token.toString() << std::endl;

        std::cout << std::endl;

        if (token.type == TokenType::EOFILE) break;
    }
}

ObjFunction* Compiler::compile(const std::string& source)
{
    scanner.init(source);

    compilerData = CompilerScope(FunctionType::SCRIPT, nullptr, nullptr);
    current = &compilerData;
    currentClass = nullptr;

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TokenType::EOFILE))
    {
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? nullptr : function;
}

void Compiler::beginCompile()
{
    compilerData = CompilerScope(FunctionType::SCRIPT, nullptr, nullptr);
    current = &compilerData;
    currentClass = nullptr;
}

bool Compiler::check(TokenType type)
{
    return parser.current.type == type;
}

Compiler::Compiler()
    : scanner()
    , compilerData()
    , current(&compilerData)
    , currentClass(nullptr)
{
}

void Compiler::advance()
{
    parser.previous = parser.current;

    for (;;)
    {
        parser.current = scanner.scanToken();
        if (parser.current.type != TokenType::ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

void Compiler::consume(TokenType type, const char* message)
{
    if (parser.current.type == type)
    {
        advance();
        return;
    }

    errorAtCurrent(message);
}

bool Compiler::match(TokenType type)
{
    if (!check(type)) return false;
    advance();
    return true;
}

void Compiler::emitDWord(uint32_t byte)
{
    // Convert constant to an array of 4 uint8_t
    const uint8_t* vp = (uint8_t*)&byte;

    emitByte(vp[0]);
    emitByte(vp[1]);
    emitByte(vp[2]);
    emitByte(vp[3]);
}

void Compiler::emitShort(uint16_t byte)
{
    // Convert constant to an array of 2 uint8_t
    const uint8_t* vp = (uint8_t*)&byte;

    emitByte(vp[0]);
    emitByte(vp[1]);
}

void Compiler::emitByte(uint8_t byte)
{
    currentChunk()->write(byte, parser.previous.line);
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2)
{
    emitByte(byte1);
    emitByte(byte2);
}

void Compiler::emitLoop(size_t loopStart)
{
    emitByte(OpByte(OpCode::OP_LOOP));

    const size_t offset = currentChunk()->code.size() - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitShort(static_cast<uint16_t>(offset));
}

size_t Compiler::emitJump(uint8_t instruction)
{
    emitByte(instruction);

    emitShort(0xffff);
    return currentChunk()->code.size() - 2;
}

void Compiler::emitOpWithValue(OpCode shortOp, OpCode longOp, uint32_t value)
{
#ifdef FORCE_LONG_OPS
    emitByte(OpByte(longOp));
    emitDWord(value);
    return;
#endif

    if (value > UINT8_MAX)
    {
        emitByte(OpByte(longOp));
        emitDWord(value);
    }
    else
    {
        emitBytes(OpByte(shortOp), static_cast<uint8_t>(value));
    }
}

void Compiler::emitReturn()
{
    if (current->type == FunctionType::INITIALIZER)
    {
        emitBytes(OpByte(OpCode::OP_GET_LOCAL), 0);
    }
    else
    {
        emitByte(OpByte(OpCode::OP_NIL));
    }

    emitByte(OpByte(OpCode::OP_RETURN));
}

uint32_t Compiler::makeConstant(Value value)
{
    return currentChunk()->addConstant(value);
}

void Compiler::emitConstant(Value value)
{
    const uint32_t constant = makeConstant(value);
    emitOpWithValue(OpCode::OP_CONSTANT, OpCode::OP_CONSTANT_LONG, constant);
}

void Compiler::emitVariable(const Token& name, bool shouldAssign, bool ignoreConst)
{
    OpCode getOp;
    OpCode getOpLong;
    OpCode setOp;
    OpCode setOpLong;
    int arg = resolveLocal(*current, name);

    const bool isLocal = arg != -1;

    if (!isLocal)
        arg = resolveUpvalue(*current, name);

    const bool isUpValue = !isLocal && arg != -1;

    if (isLocal)
    {
        getOp = OpCode::OP_GET_LOCAL;
        getOpLong = OpCode::OP_GET_LOCAL_LONG;
        setOp = OpCode::OP_SET_LOCAL;
        setOpLong = OpCode::OP_SET_LOCAL_LONG;
    }
    else if (isUpValue)
    {
        getOp = OpCode::OP_GET_UPVALUE;
        getOpLong = OpCode::OP_GET_UPVALUE; // No long ops, can't have more than 255 upvalues
        setOp = OpCode::OP_SET_UPVALUE;
        setOpLong = OpCode::OP_SET_UPVALUE; // No long ops, can't have more than 255 upvalues
    }
    else
    {
        arg = identifierConstant(name);
        getOp = OpCode::OP_GET_GLOBAL;
        getOpLong = OpCode::OP_GET_GLOBAL_LONG;
        setOp = OpCode::OP_SET_GLOBAL;
        setOpLong = OpCode::OP_SET_GLOBAL_LONG;
    }

    if (shouldAssign)
    {
        if (!ignoreConst)
        {
            if (isLocal)
            {
                if (isLocalConst(*current, arg))
                    error("Can't reassign a const variable");
            }
            else if (isUpValue)
            {
                if (isUpvalueConst(*current, arg))
                    error("Can't reassign a const variable");
            }
            else
            {
                if (isGlobalConst(arg))
                    error("Can't reassign a const variable");
            }
        }

        emitOpWithValue(setOp, setOpLong, arg);
    }
    else
    {
        emitOpWithValue(getOp, getOpLong, arg);
    }
}

void Compiler::patchJump(size_t offset)
{
    // -2 to adjust for the bytecode for the jump offset itself.
    const size_t jump = currentChunk()->code.size() - offset - 2;

    if (jump > UINT16_MAX)
    {
        error("Too much code to jump over.");
    }

    // Convert constant to an array of 2 uint8_t
    const uint8_t* vp = (uint8_t*)&jump;

    currentChunk()->code[offset] = vp[0];
    currentChunk()->code[offset + 1] = vp[1];
}

ObjFunction* Compiler::endCompiler()
{
    emitReturn();
    ObjFunction* function = current->function;

    #ifdef DEBUG_PRINT_CODE
        if (!parser.hadError)
        {
            disassembleChunk(*currentChunk(), function->name != nullptr
                ? function->name->chars.c_str() : "<script>");
        }
    #endif
    
    current = current->enclosing;
    return function;
}

void Compiler::binary(bool canAssign)
{
    const TokenType operatorType = parser.previous.type;
    const ParseRule* rule = getRule(operatorType);
    parsePrecedence(nextPrecedence(rule->precedence));

    switch (operatorType)
    {
        case TokenType::BANG_EQUAL:    emitBytes(OpByte(OpCode::OP_EQUAL), OpByte(OpCode::OP_NOT)); break;
        case TokenType::EQUAL_EQUAL:   emitByte(OpByte(OpCode::OP_EQUAL)); break;
        case TokenType::GREATER:       emitByte(OpByte(OpCode::OP_GREATER)); break;
        case TokenType::GREATER_EQUAL: emitBytes(OpByte(OpCode::OP_LESS), OpByte(OpCode::OP_NOT)); break;
        case TokenType::LESS:          emitByte(OpByte(OpCode::OP_LESS)); break;
        case TokenType::LESS_EQUAL:    emitBytes(OpByte(OpCode::OP_GREATER), OpByte(OpCode::OP_NOT)); break;
        case TokenType::PLUS:          emitByte(OpByte(OpCode::OP_ADD)); break;
        case TokenType::MINUS:         emitByte(OpByte(OpCode::OP_SUBTRACT)); break;
        case TokenType::STAR:          emitByte(OpByte(OpCode::OP_MULTIPLY)); break;
        case TokenType::SLASH:         emitByte(OpByte(OpCode::OP_DIVIDE)); break;
        case TokenType::PERCENTAGE:    emitByte(OpByte(OpCode::OP_MODULO)); break;
        case TokenType::DOT_DOT:       emitByte(OpByte(OpCode::OP_BUILD_RANGE)); break;
        default: return; // Unreachable.
    }
}

void Compiler::call(bool canAssign)
{
    const uint8_t argCount = argumentList();
    emitBytes(OpByte(OpCode::OP_CALL), argCount);
}

void Compiler::dot(bool canAssign)
{
    consume(TokenType::IDENTIFIER, "Expect property name after '.'.");
    const uint32_t name = identifierConstant(parser.previous);

    if (canAssign && match(TokenType::EQUAL))
    {
        expression();
        emitOpWithValue(OpCode::OP_SET_PROPERTY, OpCode::OP_SET_PROPERTY_LONG, name);
    }
    else if (match(TokenType::LEFT_PAREN))
    {
        const uint8_t argCount = argumentList();
        emitOpWithValue(OpCode::OP_INVOKE, OpCode::OP_INVOKE_LONG, name);
        emitByte(argCount);
    }
    else
    {
        emitOpWithValue(OpCode::OP_GET_PROPERTY, OpCode::OP_GET_PROPERTY_LONG, name);
    }
}

void Compiler::subscript(bool canAssign)
{
    parsePrecedence(Precedence::OR);
    consume(TokenType::RIGHT_BRACKET, "Expect closing brackets ']'.");

    if (canAssign && match(TokenType::EQUAL))
    {
        expression();
        emitByte(OpByte(OpCode::OP_STORE_SUBSCR));
    }
    else
    {
        emitByte(OpByte(OpCode::OP_INDEX_SUBSCR));
    }

    
}

void Compiler::literal(bool canAssign)
{
    switch (parser.previous.type)
    {
        case TokenType::FALSE:         emitByte(OpByte(OpCode::OP_FALSE)); break;
        case TokenType::NIL:           emitByte(OpByte(OpCode::OP_NIL)); break;
        case TokenType::TRUE:          emitByte(OpByte(OpCode::OP_TRUE)); break;
        default: return; // Unreachable.
    }
}

void Compiler::grouping(bool canAssign)
{
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
}

void Compiler::number(bool canAssign)
{
    // We need to split the range in two integers
    const double value = strtod(parser.previous.start, nullptr);
    emitConstant(Value(value));
}

void Compiler::or_(bool canAssign)
{
    const size_t elseJump = emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
    const size_t endJump = emitJump(OpByte(OpCode::OP_JUMP));

    patchJump(elseJump);
    emitByte(OpByte(OpCode::OP_POP));

    parsePrecedence(Precedence::OR);
    patchJump(endJump);
}

void Compiler::string(bool canAssign)
{
    emitConstant(Value(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

void Compiler::namedVariable(const Token& name, bool canAssign)
{
    OpCode getOp;
    OpCode getOpLong;
    OpCode setOp;
    OpCode setOpLong;
    int arg = resolveLocal(*current, name);

    const bool isLocal = arg != -1;
    
    if (!isLocal)
        arg = resolveUpvalue(*current, name);

    const bool isUpValue = !isLocal && arg != -1;

    if (isLocal)
    {
        getOp = OpCode::OP_GET_LOCAL;
        getOpLong = OpCode::OP_GET_LOCAL_LONG;
        setOp = OpCode::OP_SET_LOCAL;
        setOpLong = OpCode::OP_SET_LOCAL_LONG;
    }
    else if (isUpValue)
    {
        getOp = OpCode::OP_GET_UPVALUE;
        getOpLong = OpCode::OP_GET_UPVALUE; // No long ops, can't have more than 255 upvalues
        setOp = OpCode::OP_SET_UPVALUE;
        setOpLong = OpCode::OP_SET_UPVALUE; // No long ops, can't have more than 255 upvalues
    }
    else
    {
        arg = identifierConstant(name);
        getOp = OpCode::OP_GET_GLOBAL;
        getOpLong = OpCode::OP_GET_GLOBAL_LONG;
        setOp = OpCode::OP_SET_GLOBAL;
        setOpLong = OpCode::OP_SET_GLOBAL_LONG;
    }

    if (canAssign && match(TokenType::EQUAL))
    {
        if (isLocal)
        {
            if (isLocalConst(*current, arg))
                error("Can't reassign a const variable");
        }
        else if (isUpValue)
        {
            if (isUpvalueConst(*current, arg))
                error("Can't reassign a const variable");
        }
        else
        {
            if (isGlobalConst(arg))
                error("Can't reassign a const variable");
        }

        expression();
        emitOpWithValue(setOp, setOpLong, arg);
    }
    else
    {
        emitOpWithValue(getOp, getOpLong, arg);
    }
}

void Compiler::variable(bool canAssign)
{
    namedVariable(parser.previous, canAssign);
}

void Compiler::this_(bool canAssign)
{
    if (currentClass == nullptr)
    {
        error("Can't use 'this' outside of a class.");
        return;
    }

    variable(false);
}

void Compiler::unary(bool canAssign)
{
    const TokenType operatorType = parser.previous.type;

    // Compile the operand.
    parsePrecedence(Precedence::UNARY);

    // Emit the operator instruction.
    switch (operatorType)
    {
        case TokenType::MINUS: emitByte(OpByte(OpCode::OP_NEGATE)); break;
        case TokenType::BANG: emitByte(OpByte(OpCode::OP_NOT)); break;
        default: return; // Unreachable.
    }
}

void Compiler::funExpr(bool canAssign)
{
    function(FunctionType::FUNCTION);
}

inline void Compiler::list(bool canAssign)
{
    int itemCount = 0;
    if (!check(TokenType::RIGHT_BRACKET))
    {
        do {
            if (check(TokenType::RIGHT_BRACKET))
            {
                // Trailing comma case
                break;
            }

            parsePrecedence(Precedence::OR);

            if (itemCount == UINT8_COUNT) {
                error("Cannot have more than 256 items in a list literal.");
            }
            itemCount++;
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RIGHT_BRACKET, "Expect ']' after list literal.");

    emitByte(OpByte(OpCode::OP_BUILD_LIST));
    emitByte(itemCount);
    return;
}

void Compiler::parsePrecedence(Precedence precedence)
{
    advance();
    const ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == nullptr)
    {
        error("Expect expression.");
        return;
    }

    const bool canAssign = precedence <= Precedence::ASSIGNMENT;
    (this->*prefixRule)(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence)
    {
        advance();
        const ParseFn infixRule = getRule(parser.previous.type)->infix;
        (this->*infixRule)(canAssign);
    }

    if (canAssign && match(TokenType::EQUAL))
    {
        error("Invalid assignment target.");
    }
}

uint32_t Compiler::identifierConstant(const Token& name)
{
    return makeConstant(Value(copyString(name.start, name.length)));
}

bool Compiler::identifiersEqual(const Token& a, const Token& b)
{
    if (a.length != b.length) return false;
    return memcmp(a.start, b.start, a.length) == 0;
}

int Compiler::resolveLocal(const CompilerScope& compilerScope, const Token& name)
{
    for (int i = compilerScope.localCount - 1; i >= 0; i--)
    {
        const Local& local = compilerScope.locals[i];
        if (identifiersEqual(name, local.name))
        {
            const bool isInOwnInitializer = local.depth == -1;
            if (isInOwnInitializer)
            {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

int Compiler::addUpvalue(CompilerScope& compilerScope, uint8_t index, bool isLocal)
{
    const int upvalueCount = compilerScope.function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++)
    {
        const Upvalue* upvalue = &compilerScope.upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal)
        {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT)
    {
        error("Too many closure variables in function.");
        return 0;
    }

    compilerScope.upvalues[upvalueCount].isLocal = isLocal;
    compilerScope.upvalues[upvalueCount].index = index;
    return compilerScope.function->upvalueCount++;
}

int Compiler::resolveUpvalue(CompilerScope& compilerScope, const Token& name)
{
    if (compilerScope.enclosing == nullptr) return -1;

    const int local = resolveLocal(*compilerScope.enclosing, name);
    if (local != -1)
    {
        compilerScope.enclosing->locals[local].isCaptured = true;
        return addUpvalue(compilerScope, static_cast<uint8_t>(local), true);
    }

    const int upvalue = resolveUpvalue(*compilerScope.enclosing, name);
    if (upvalue != -1)
    {
        return addUpvalue(compilerScope, static_cast<uint8_t>(upvalue), false);
    }

    return -1;
}

bool Compiler::isLocalConst(const CompilerScope& compilerScope, int index)
{
    const Local& local = compilerScope.locals[index];
    return local.constant;
}

const Local* getUpvalue(const CompilerScope& compilerScope, int index)
{
    if (compilerScope.enclosing == nullptr) { return nullptr; }

    const Upvalue& upvalue = compilerScope.upvalues[index];

    if (upvalue.isLocal)
        return &compilerScope.enclosing->locals[upvalue.index];

    return getUpvalue(*compilerScope.enclosing, upvalue.index);
}

bool Compiler::isUpvalueConst(const CompilerScope& compilerScope, int index)
{
    if (const Local* local = getUpvalue(compilerScope, index))
        return local->constant;

    return false;
}

bool Compiler::isGlobalConst(uint8_t index)
{
    return constGlobals.find(index) != constGlobals.end();
}

void Compiler::addLocal(const Token& name, bool isConstant)
{
    if (current->localCount == UINT8_COUNT)
    {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = current->scopeDepth;
    local->constant = isConstant;
    local->isCaptured = false;
}

void Compiler::declareVariable(bool isConstant)
{
    if (current->scopeDepth == 0) return;

    const Token& name = parser.previous;

    for (int i = current->localCount - 1; i >= 0; i--)
    {
        const Local& local = current->locals[i];
        if (local.depth != -1 && local.depth < current->scopeDepth)
        {
            break;
        }

        if (identifiersEqual(name, local.name))
        {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(name, isConstant);
}

 uint32_t Compiler::parseVariable(const char* errorMessage, bool isConstant)
{
    consume(TokenType::IDENTIFIER, errorMessage);

    declareVariable(isConstant);
    if (current->scopeDepth > 0) return 0;

    const uint32_t constant = identifierConstant(parser.previous);
    if (isConstant)
    {
        constGlobals.insert(constant);
    }

    return constant;
}

 void Compiler::markInitialized()
 {
     if(current->scopeDepth == 0) return;
     current->locals[current->localCount - 1].depth = current->scopeDepth;
 }

void Compiler::defineVariable(uint32_t global)
{
    if (current->scopeDepth > 0)
    {
        markInitialized();
        return;
    }

    emitOpWithValue(OpCode::OP_DEFINE_GLOBAL, OpCode::OP_DEFINE_GLOBAL_LONG, global);
}

uint8_t Compiler::argumentList()
{
    uint8_t argCount = 0;
    if (!check(TokenType::RIGHT_PAREN))
    {
        do
        {
            expression();
            if (argCount == 255)
            {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

void Compiler::and_(bool canAssign)
{
    const size_t endJump = emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));

    emitByte(OpByte(OpCode::OP_POP));
    parsePrecedence(Precedence::AND);

    patchJump(endJump);
}

const Compiler::ParseRule* Compiler::getRule(TokenType type) const
{
    return &rules[static_cast<int>(type)];
}

void Compiler::expression()
{
    parsePrecedence(Precedence::ASSIGNMENT);
}

void Compiler::block()
{
    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::EOFILE))
    {
        declaration();
    }

    consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
}

void Compiler::function(FunctionType type)
{
    CompilerScope compilerScope(type, current, &parser.previous);
    current = &compilerScope;
    beginScope();

    consume(TokenType::LEFT_PAREN, "Expect '(' after function name.");

    if (!check(TokenType::RIGHT_PAREN))
    {
        do
        {
            current->function->arity++;
            if (current->function->arity > 255)
            {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            const uint32_t constant = parseVariable("Expect parameter name.", false);
            defineVariable(constant);
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TokenType::LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    const uint32_t constant = makeConstant(Value(function));
    emitOpWithValue(OpCode::OP_CLOSURE, OpCode::OP_CLOSURE_LONG, constant);

    for (int i = 0; i < function->upvalueCount; i++)
    {
        emitByte(compilerScope.upvalues[i].isLocal ? 1 : 0);
        emitByte(compilerScope.upvalues[i].index);
    }
}

void Compiler::method()
{
    consume(TokenType::IDENTIFIER, "Expect method name.");
    const uint32_t constant = identifierConstant(parser.previous);

    FunctionType type = FunctionType::METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0)
    {
        type = FunctionType::INITIALIZER;
    }
    function(type);

    emitOpWithValue(OpCode::OP_METHOD, OpCode::OP_METHOD_LONG, constant);
}

inline void Compiler::classDeclaration()
{
    consume(TokenType::IDENTIFIER, "Expect class name.");
    const Token className = parser.previous;

    const uint32_t nameConstant = identifierConstant(parser.previous);
    declareVariable(false);

    emitOpWithValue(OpCode::OP_CLASS, OpCode::OP_CLASS_LONG, nameConstant);
    defineVariable(nameConstant);

    ClassCompilerScope classCompilerScope;
    classCompilerScope.enclosing = currentClass;
    currentClass = &classCompilerScope;

    namedVariable(className, false);

    consume(TokenType::LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::EOFILE))
    {
        method();
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after class body.");
    emitByte(OpByte(OpCode::OP_POP));

    currentClass = currentClass->enclosing;
}

void Compiler::funDeclaration()
{
    const uint32_t global = parseVariable("Expect function name.", false);
    markInitialized();
    function(FunctionType::FUNCTION);
    defineVariable(global);
}

void Compiler::beginScope()
{
    current->scopeDepth++;
}

void Compiler::endScope()
{
    current->scopeDepth--;

    while (current->localCount > 0 &&
        current->locals[current->localCount - 1].depth > current->scopeDepth)
    {
        if (current->locals[current->localCount - 1].isCaptured)
        {
            emitByte(OpByte(OpCode::OP_CLOSE_UPVALUE));
        }
        else {
            emitByte(OpByte(OpCode::OP_POP));
        }
        current->localCount--;
    }
}

void Compiler::varDeclaration(bool isConstant)
{
    const uint32_t global = parseVariable("Expect variable name.", isConstant);

    if (match(TokenType::EQUAL))
    {
        expression();
    }
    else
    {
        emitByte(OpByte(OpCode::OP_NIL));
    }
    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}

void Compiler::expressionStatement()
{
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    emitByte(OpByte(OpCode::OP_POP));
}

void Compiler::forStatement()
{
    if (!check(TokenType::LEFT_PAREN))
    {
        forInStatement();
        return;
    }

    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'for'.");

    if (match(TokenType::SEMICOLON))
    {
        // No initializer.
    }
    else if (match(TokenType::CONST))
    {
        varDeclaration(true);
    }
    else if (match(TokenType::VAR))
    {
        varDeclaration(false);
    }
    else
    {
        expressionStatement();
    }

    size_t loopStart = currentChunk()->code.size();
    size_t exitJump = SIZE_MAX;

    if (!match(TokenType::SEMICOLON))
    {
        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        exitJump = emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
        emitByte(OpByte(OpCode::OP_POP)); // Condition.
    }

    if (!match(TokenType::RIGHT_PAREN))
    {
        const size_t bodyJump = emitJump(OpByte(OpCode::OP_JUMP));
        const size_t incrementStart = currentChunk()->code.size();
        expression();
        emitByte(OpByte(OpCode::OP_POP));
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != SIZE_MAX)
    {
        patchJump(exitJump);
        emitByte(OpByte(OpCode::OP_POP)); // Condition.
    }

    endScope();
}

void Compiler::forInStatement()
{
    beginScope();

    consume(TokenType::IDENTIFIER, "Expected variable after 'for'");

    const Token localVarToken = parser.previous;

    // Initialize our hidden local
    const Token iterToken(TokenType::VAR, "__iter", 6, parser.current.line);
    addLocal(iterToken, false);
    // Push the initial iterator value (0)
    emitConstant(Value(0.0));
    // Set the iter local to 0
    emitVariable(iterToken, true);

    consume(TokenType::IN, "Expect 'in' after loop variable.");

    // Initialize our hidden local range var
    const Token rangeToken(TokenType::VAR, "__range", 7, parser.current.line);
    addLocal(rangeToken, false);
    expression(); // This should resolve to a range or a string
    emitVariable(rangeToken, true); // Set the range value

    const size_t loopStart = currentChunk()->code.size();

    // Condition
    namedVariable(rangeToken, false); // Load range
    namedVariable(iterToken, false);
    emitByte(OpByte(OpCode::OP_RANGE_IN_BOUNDS));

    const size_t exitJump = emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
    emitByte(OpByte(OpCode::OP_POP));

    beginScope();

    // create a hidden local variable. Syntactic sugar for var i = rangeValue(__range, __iter);
    addLocal(localVarToken, true);

    // Set value from iterator
    namedVariable(rangeToken, false); // Load range
    namedVariable(iterToken, false); // Load iterator

    // Set the local variable value before running the statement
    emitByte(OpByte(OpCode::OP_INDEX_SUBSCR));
    emitVariable(localVarToken, true, true); // Set local variable

    statement();

    endScope();

    // After statement happens, increase values

    // Increment iterator
    namedVariable(iterToken, false); // Get iterator value
    emitByte(OpByte(OpCode::OP_INCREMENT));
    emitVariable(iterToken, true); // Set iterator value incremented
    emitByte(OpByte(OpCode::OP_POP)); // Pop iterator

    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OpByte(OpCode::OP_POP));

    endScope();
}

void Compiler::ifStatement()
{
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    const size_t thenJump = emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
    emitByte(OpByte(OpCode::OP_POP));
    statement();

    const size_t elseJump = emitJump(OpByte(OpCode::OP_JUMP));
    patchJump(thenJump);
    emitByte(OpByte(OpCode::OP_POP));

    if (match(TokenType::ELSE))
    {
        statement();
    }

    patchJump(elseJump);
}

void Compiler::printStatement()
{
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after value.");
    emitByte(OpByte(OpCode::OP_PRINT));
}

void Compiler::returnStatement()
{
    if (current->type == FunctionType::SCRIPT)
    {
        error("Can't return from top-level code.");
    }

    if (match(TokenType::SEMICOLON))
    {
        emitReturn();
    }
    else
    {
        if (current->type == FunctionType::INITIALIZER)
        {
            error("Can't return a value from an initializer.");
        }

        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after return value.");
        emitByte(OpByte(OpCode::OP_RETURN));
    }
}

void Compiler::whileStatement()
{
    const size_t loopStart = currentChunk()->code.size();
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    const size_t exitJump = emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
    emitByte(OpByte(OpCode::OP_POP));
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OpByte(OpCode::OP_POP));
}

void Compiler::matchStatement()
{
    beginScope();
    const size_t conditionStart = currentChunk()->code.size();

    expression();

    // Initialize our hidden local
    const Token matchVarToken(TokenType::VAR, "__match", 6, parser.current.line);
    addLocal(matchVarToken, true);
    emitVariable(matchVarToken, true, true);

    consume(TokenType::LEFT_BRACE, "Expect '{' after 'match expression'.");

    std::vector<size_t> exitJumps;

    // At this point the expresson result is on the top of the stack
    while (!check(TokenType::RIGHT_BRACE))
    {
        emitVariable(matchVarToken, false);

        beginScope();

        pattern();

        emitByte(OpByte(OpCode::OP_MATCH));
        const size_t nextCaseJump = emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
        emitByte(OpByte(OpCode::OP_POP));

        consume(TokenType::COLON, "Expect ':' after pattern.");

        statement();

        // Hack to "end scope" here too and pop all variables
        // TODO: How to do this in a correct way?
        int localCount = current->localCount;
        while (localCount > 0 &&
            current->locals[localCount - 1].depth > (current->scopeDepth - 1))
        {
            if (current->locals[localCount - 1].isCaptured)
            {
                emitByte(OpByte(OpCode::OP_CLOSE_UPVALUE));
            }
            else {
                emitByte(OpByte(OpCode::OP_POP));
            }
            localCount--;
        }

        const size_t exitJump = emitJump(OpByte(OpCode::OP_JUMP));
        exitJumps.push_back(exitJump);

        patchJump(nextCaseJump);
        emitByte(OpByte(OpCode::OP_POP));

        endScope();
    }

    consume(TokenType::RIGHT_BRACE, "Expect '}' after 'match expression cases'.");


    for (const size_t exitJump : exitJumps)
        patchJump(exitJump);

    endScope();
}

void Compiler::pattern()
{
    // Optional variable pattern
    if (check(TokenType::IDENTIFIER))
    {
        const Token patternVarToken = parser.current;
        
        consume(TokenType::IDENTIFIER, "Expect pattern identifier'");

        const bool isWildcard = (patternVarToken.start == "_" && patternVarToken.length == 1);

        if (!isWildcard)
        {
            addLocal(patternVarToken, true);
            emitVariable(patternVarToken, true, true);
        }

        // We need to compare the guard to true
        emitByte(OpByte(OpCode::OP_TRUE));

        // Optional guard
        if (match(TokenType::IF))
        {
            expression();
        }
        else
        {
            // No guard, always true
            emitByte(OpByte(OpCode::OP_TRUE));
        }

        return;
    }
    expression();
}

void Compiler::synchronize()
{
    parser.panicMode = false;

    while (parser.current.type != TokenType::EOFILE)
    {
        if (parser.previous.type == TokenType::SEMICOLON) return;
        switch (parser.current.type)
        {
            case TokenType::CLASS:
            case TokenType::FUN:
            case TokenType::VAR:
            case TokenType::CONST:
            case TokenType::FOR:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::MATCH:
            case TokenType::PRINT:
            case TokenType::RETURN:
                return;

            default:
                ; // Do nothing.
        }

        advance();
    }
}

void Compiler::declaration()
{
    if (match(TokenType::CLASS))
    {
        classDeclaration();
    }
    else if (match(TokenType::FUN))
    {
        funDeclaration();
    }
    else if (match(TokenType::VAR))
    {
        varDeclaration(false);
    }
    else if (match(TokenType::CONST))
    {
        varDeclaration(true);
    }
    else
    {
        statement();
    }

    if (parser.panicMode) synchronize();
}

void Compiler::statement()
{
    if (match(TokenType::PRINT))
    {
        printStatement();
    }
    else if (match(TokenType::FOR))
    {
        forStatement();
    }
    else if (match(TokenType::IF))
    {
        ifStatement();
    }
    else if (match(TokenType::RETURN))
    {
        returnStatement();
    }
    else if (match(TokenType::WHILE))
    {
        whileStatement();
    }
    else if (match(TokenType::MATCH))
    {
        matchStatement();
    }
    else if (match(TokenType::LEFT_BRACE))
    {
        beginScope();
        block();
        endScope();
    }
    else
    {
        expressionStatement();
    }
}

void Compiler::errorAtCurrent(const std::string& message)
{
    errorAt(parser.current, message);
}

void Compiler::error(const std::string& message)
{
    errorAt(parser.previous, message);
}

void Compiler::errorAt(const Token& token, const std::string& message)
{
    if (parser.panicMode) return;
    parser.panicMode = true;

    std::cerr << "[line " << token.line << "] Error";

    if (token.type == TokenType::EOFILE)
    {
        std::cerr << " at end";
    }
    else if (token.type == TokenType::ERROR)
    {
        // Nothing.
    }
    else
    {
        std::cerr << " at " << token.toString();
    }

    std::cerr << ": " << message << std::endl;
    parser.hadError = true;
}
