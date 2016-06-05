//
//  main.cpp
//  Kaleidoscope
//
//  Created by shaohua on 6/5/16.
//  Copyright © 2016 syang. All rights reserved.
//

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include <iostream>
#include <string>
#include <regex>
#include <map>
#include <cassert>

#pragma mark -- Lexer
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

#pragma mark - XXXAST
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

    virtual llvm::Value *codegen() = 0;
};

struct NumberExprAST : public ExprAST {
    double Val;
    NumberExprAST(double Val) : Val(Val) {
    }

    llvm::Value *codegen() override;
};

struct VariableExprAST : public ExprAST {
    std::string Name;

    VariableExprAST(const std::string &Name) : Name(Name) {
    }

    llvm::Value *codegen() override;
};

struct BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS;
    std::unique_ptr<ExprAST> RHS;

    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                  std::unique_ptr<ExprAST> RHS)
    : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {
    }

    llvm::Value *codegen() override;
};

struct CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

    CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
    : Callee(Callee), Args(std::move(Args)) {
    }

    llvm::Value *codegen() override;
};

struct PrototypeAST {
    std::string Name;
    std::vector<std::string> Args; // 语义分析中需要名字

    PrototypeAST(const std::string &Name, std::vector<std::string> Args)
    : Name(Name), Args(std::move(Args)) {
    }

    llvm::Function *codegen();
};

struct FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

    FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
    : Proto(std::move(Proto)), Body(std::move(Body)) {
    }

    llvm::Function *codegen();
};

#pragma mark - ParseXXX()
static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    assert(CurTok == tok_number && "tok_number expected.\n");
    double ThisValue = NumVal;
    getNextToken();
    return llvm::make_unique<NumberExprAST>(ThisValue);
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

    return Result;
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

        auto Result = llvm::make_unique<CallExprAST>(Identifier, std::move(Args));
        return std::move(Result);
    }

    return llvm::make_unique<VariableExprAST>(Identifier);
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

    return llvm::make_unique<PrototypeAST>(Name, Args);
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

    return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    assert(CurTok == tok_extern && "'extern' expected.\n");
    getNextToken(); // 'extern'
    return ParsePrototype();
}

#pragma mark - Binary Expression Parsing
static int GetTokPrecedence() {
    switch (CurTok) {
        case '<':
        case '>':
            return 10;
        case '+':
        case '-':
            return 20;
        case '*':
        case '/':
            return 40;
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
        LHS = llvm::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
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
        auto Proto = llvm::make_unique<PrototypeAST>("", std::vector<std::string>());
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}


#pragma mark - codgen
static llvm::LLVMContext TheContext;
static llvm::IRBuilder<> Builder(TheContext);
static std::unique_ptr<llvm::Module> TheModule;
static std::map<std::string, llvm::Value *> NamedValues;
llvm::Value *NumberExprAST::codegen() {
    return llvm::ConstantFP::get(TheContext, llvm::APFloat(Val));
}

llvm::Value *VariableExprAST::codegen() {
    auto V = NamedValues[Name];
    if (!V) {
        std::cerr << "unknown varible name: " << Name << '\n';
        return nullptr;
    }
    return V;
}

llvm::Value *BinaryExprAST::codegen() {
    auto L = LHS->codegen();
    auto R = RHS->codegen();
    if (!L || !R) {
        return nullptr;
    }
    switch (Op) {
        case '+':
            return Builder.CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder.CreateFSub(L, R, "subtmp");
        case '*':
            return Builder.CreateFMul(L, R, "multmp");
        case '/':
            return Builder.CreateFDiv(L, R, "divtmp");
        case '<':
            L = Builder.CreateFCmpULT(L, R, "lttmp");
            return Builder.CreateUIToFP(L, llvm::Type::getDoubleTy(TheContext), "booltmp");
        case '>':
            L = Builder.CreateFCmpUGT(L, R, "gttmp");
            return Builder.CreateUIToFP(L, llvm::Type::getDoubleTy(TheContext), "booltmp");
        default:
            std::cerr << "unimplemented op:" << Op << '\n';
            return nullptr;
    }
}

llvm::Value *CallExprAST::codegen() {
    llvm::Function *CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF) {
        std::cerr << "unknown function named: " << Callee << '\n';
        return nullptr;
    }

    if (CalleeF->arg_size() != Args.size()) {
        std::cerr << "wrong # arguments, expected " << CalleeF->arg_size() << " given " << Args.size() << '\n';
        return nullptr;
    }

    std::vector<llvm::Value *> ArgsV;
    for (size_t I = 0, E = Args.size(); I != E; ++I) {
        ArgsV.push_back(Args[I]->codegen());
        if (!ArgsV.back()) {
            return nullptr;
        }
    }

    return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

llvm::Function *PrototypeAST::codegen() {
    // Make the function type:  double(double,double) etc.
    std::vector<llvm::Type *> Doubles(Args.size(), llvm::Type::getDoubleTy(TheContext));
    llvm::FunctionType *FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(TheContext), Doubles, false);

    llvm::Function *F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, Name, TheModule.get());

    // Set names for all arguments.
    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);

    return F;
}

llvm::Function *FunctionAST::codegen() {
    // First, check for an existing function from a previous 'extern' declaration.
    llvm::Function *TheFunction = TheModule->getFunction(Proto->Name);

    if (!TheFunction)
        TheFunction = Proto->codegen();

    if (!TheFunction)
        return nullptr;

    // Create a new basic block to start insertion into.
    llvm::BasicBlock *BB = llvm::BasicBlock::Create(TheContext, "entry", TheFunction);
    Builder.SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map.
    NamedValues.clear();
    for (auto &Arg : TheFunction->args())
        NamedValues[Arg.getName()] = &Arg;

    if (llvm::Value *RetVal = Body->codegen()) {
        // Finish off the function.
        Builder.CreateRet(RetVal);

        // Validate the generated code, checking for consistency.
        verifyFunction(*TheFunction);

        return TheFunction;
    }

    // Error reading body, remove function.
    TheFunction->eraseFromParent();
    return nullptr;
}

#pragma mark - main
static void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read function definition:");
            FnIR->dump();
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto *FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Read extern: ");
            FnIR->dump();
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function.
    if (auto FnAST = ParseTopLevelExpr()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read top-level expression:");
            FnIR->dump();
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
            case tok_eof:
                return;
            case tok_def:
                HandleDefinition();
                break;
            case tok_extern:
                HandleExtern();
                break;
            case ';':
                getNextToken();
                break;
            default:
                HandleTopLevelExpression();
                break;
        }
    }
}

int main() {
    // Prime the first token.
    fprintf(stderr, "ready> ");
    getNextToken();

    TheModule = llvm::make_unique<llvm::Module>("my cool jit", TheContext);

    MainLoop();

    TheModule->dump();
    return 0;
}
