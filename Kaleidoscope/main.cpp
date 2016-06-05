//
//  main.cpp
//  Kaleidoscope
//
//  Created by shaohua on 6/5/16.
//  Copyright © 2016 syang. All rights reserved.
//

#include <iostream>
#include <string>
#include <regex>
#include <map>
#include <cassert>

/* Lexer:
 IdentifierStr
 NumVal
 gettok()
 */
enum Token {
    tok_eof = -1,

    tok_def = -2,
    tok_extern = -3,

    tok_identifier = -4,
    tok_number = -5,
};

static std::string IdentifierStr;
static double NumVal;

static int gettok() {
    static int LastChar = ' '; // EOF is -1

    while (std::isspace(LastChar)) {
        LastChar = std::cin.get();
    }

    if (std::isalpha(LastChar)) {
        IdentifierStr = LastChar;

        LastChar = std::cin.get();
        while (std::isalnum(LastChar)) {
            IdentifierStr += LastChar;
            LastChar = std::cin.get();
        }

        if (IdentifierStr == "def") {
            return tok_def;
        }

        if (IdentifierStr == "extern") {
            return tok_extern;
        }

        return tok_identifier;
    }

    if (std::isdigit(LastChar) || LastChar == '.') {
        std::string NumStr(1, LastChar);

        LastChar = std::cin.get();
        while (std::isdigit(LastChar) || LastChar == '.') {
            NumStr += LastChar;
            LastChar = std::cin.get();
        }

        NumVal = std::strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    if (LastChar == '#') {
        LastChar = std::getc(stdin);
        while (LastChar != '\n' && LastChar != EOF) {
            LastChar = std::cin.get();
        }
    }

    if (LastChar == EOF) {
        return tok_eof;
    }

    int ThisChar = LastChar;
    LastChar = std::cin.get();
    return ThisChar;
}

/* Parser: 成功吃掉对应 tokens，出错不要吃掉当前 token。

 XxxAST
 parseXxx
 CurTok
 getNextToken()
 */

static int CurTok;
static int getNextToken() {
    return CurTok = gettok();
}

struct ExprAST {
    virtual ~ExprAST() {};
};

struct NumberExprAST : public ExprAST {
    double Val;
    NumberExprAST(double Val) : Val(Val) {
    }
};

struct VariableExprAST : public ExprAST {
    std::string Name;

    VariableExprAST(const std::string &Name) : Name(Name) {
    }
};

struct BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS;
    std::unique_ptr<ExprAST> RHS;

    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                  std::unique_ptr<ExprAST> RHS)
    : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {
    }
};

struct CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

    CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
    : Callee(Callee), Args(std::move(Args)) {
    }
};

struct PrototypeAST {
    std::string Name;
    std::vector<std::string> Args; // 语义分析中需要名字

    PrototypeAST(const std::string &Name, std::vector<std::string> Args)
    : Name(Name), Args(std::move(Args)) {
    }
};

struct FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

    FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
    : Proto(std::move(Proto)), Body(std::move(Body)) {
    }
};

static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    assert(CurTok == tok_number && "tok_number expected.\n");
    double ThisValue = NumVal;
    getNextToken();
    return std::make_unique<NumberExprAST>(ThisValue);
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    assert(CurTok == '(' && "'(' number expected.\n");
    getNextToken();

    auto Result = ParseExpression();
    if (!Result) {
        return nullptr;
    }

    if (CurTok != ')') {
        std::cerr << "')' expected.\n";
        return nullptr;
    }
    getNextToken(); // ')'

    return std::move(Result);
}

/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'        函数调用
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    assert(CurTok == tok_identifier && "tok_identifier expected.\n");
    std::string Identifier = IdentifierStr;
    getNextToken();

    if (CurTok == '(') {
        getNextToken(); // '('

        std::vector<std::unique_ptr<ExprAST>> Args;
        while (CurTok != ')') {
            if (auto Arg = ParseParenExpr()) {
                Args.push_back(std::move(Arg));
            } else {
                return nullptr;
            }

            if (CurTok == ')') {
                break;
            }

            if (CurTok != ',') {
                std::cerr << "',' expected.\n";
                return nullptr;
            }
            getNextToken(); // ','
        }
        getNextToken(); // ')'

        auto Result = std::make_unique<CallExprAST>(Identifier, std::move(Args));
        return std::move(Result);
    }

    return std::make_unique<VariableExprAST>(Identifier);
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
        default:
            std::cerr << "unexpected token " << (char)CurTok << '\n';
            break;
    }
    return nullptr;
}

/// prototype
///   ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    assert(CurTok == tok_identifier && "tok_identifier number expected.\n");
    auto Name = IdentifierStr;
    getNextToken();

    if (CurTok != '(') {
        std::cerr << "'(' expected.\n";
        return nullptr;
    }
    getNextToken();

    std::vector<std::string> Args;
    while (CurTok != ')') {
        if (CurTok == tok_identifier) {
            Args.push_back(IdentifierStr);
            getNextToken();
        } else {
            return nullptr;
        }

        if (CurTok == ')') {
            break;
        }

        if (CurTok != ',') {
            std::cerr << "',' expected.\n";
            return nullptr;
        }
        getNextToken(); // ','
    }
    getNextToken(); // ')'

    return std::make_unique<PrototypeAST>(Name, Args);
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    assert(CurTok == tok_def && "'def' expected.\n");
    getNextToken(); // 'def'

    auto Proto = ParsePrototype();
    if (!Proto) {
        return nullptr;
    }
    auto E = ParseExpression();
    if (!E) {
        return nullptr;
    }

    auto Result = std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return std::move(Result);
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    assert(CurTok == tok_extern && "'extern' expected.\n");
    getNextToken(); // 'extern'
    return std::move(ParsePrototype());
}

#pragma mark - Binary Expression Parsing
static int GetTokPrecedence() {
    switch (CurTok) {
        case '<':
        case '>':
            return -10;
        case '+':
        case '-':
            return -20;
        case '*':
        case '/':
            return -40;
        default:
            return -1;
    }
}

/// binoprhs
///   ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    // If this is a binop, find its precedence.
    while (true) {
        int TokPrec = GetTokPrecedence();

        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done.
        if (TokPrec < ExprPrec)
            return LHS;

        // Okay, we know this is a binop.
        int BinOp = CurTok;
        getNextToken(); // eat binop

        // Parse the primary expression after the binary operator.
        auto RHS = ParsePrimary();
        if (!RHS)
            return nullptr;

        // If BinOp binds less tightly with RHS than the operator after RHS, let
        // the pending operator take RHS as its LHS.
        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }

        // Merge LHS/RHS.
        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

/// expression
///   ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression() {
    return ParseBinOpRHS(0, ParsePrimary());
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // Make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
            case tok_eof:
                return;
            case tok_def:
                if (ParseDefinition()) {
                    std::cerr << "found definition.\n";
                } else {
                    getNextToken();
                }
                break;
            case tok_extern:
                if (ParseExtern()) {
                    std::cerr << "found extern.\n";
                } else {
                    getNextToken();
                }
                break;
            case ';':
                getNextToken();
                break;
            default:
                if (ParseTopLevelExpr()) {
                    std::cerr << "found expression.\n";
                } else {
                    getNextToken();
                }
                break;
        }
    }
}

int main() {
    // Prime the first token.
    fprintf(stderr, "ready> ");
    getNextToken();

    MainLoop();
    return 0;
}
