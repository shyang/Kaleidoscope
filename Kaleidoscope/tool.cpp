//
//  tool.cpp
//  Kaleidoscope
//
//  Created by shaohua on 6/7/16.
//  Copyright © 2016 syang. All rights reserved.
//

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

#include <fstream>
#include <iostream>
#include <cassert>

class MyASTVisitor : public clang::RecursiveASTVisitor<MyASTVisitor> {
public:
    explicit MyASTVisitor(clang::ASTContext &Context)
    : Context(&Context) {
    }

    void PrintMethod(clang::ObjCMethodDecl *Method) {
        clang::Selector Selector = Method->getSelector();
        /*
         object: @ ==> @"ClassName"
         block: @? ==> @?<Encoding>

         arrayWithContentsOfFile: @"NSMutableArray"24@0:8@"NSString"16
         sortWithOptions:usingComparator: v32@0:8Q16@?<q@?@@>24
         */
        std::string Types;
        bool errCode = Context->getObjCEncodingForMethodDecl(Method, Types, true /* Extended */);
        assert(!errCode);
        std::cout << "  " << Selector.getAsString() << " " << Types << "\n";
    }

    void PrintProperty(clang::ObjCPropertyDecl *Property) {
        // T@"NSString",R,C,&,N
        //
        // N: nullable
        // R: readonly
        // C: copy
        // &: retain
        //
        // Context->getObjCEncodingForPropertyDecl(Property, nullptr, Types);
        std::string Types;
        Context->getObjCEncodingForPropertyType(Property->getType(), Types);
        std::cout << "  " << Property->getNameAsString() << " " << Types << "\n";
    }
    int nProps;
    int nMethods;

    bool VisitDecl(clang::Decl *Decl) {
        std::string Name;
        std::string Types;
        bool indent = false;

        clang::ObjCCategoryDecl *Category = nullptr;
        clang::ObjCContainerDecl *Container = nullptr;
        switch (Decl->getKind()) {
            case clang::Decl::Kind::ObjCCategoryImpl: {
                clang::ObjCCategoryImplDecl *CategoryImpl = clang::cast<clang::ObjCCategoryImplDecl>(Decl);
                Category = CategoryImpl->getCategoryDecl();
            }
            case clang::Decl::Kind::ObjCCategory:
                Category = Category ?: clang::cast<clang::ObjCCategoryDecl>(Decl);
                Container = Category->getClassInterface();
            case clang::Decl::Kind::ObjCInterface:
            case clang::Decl::Kind::ObjCImplementation:
            case clang::Decl::Kind::ObjCProtocol: {
                Container = Container ?: clang::cast<clang::ObjCContainerDecl>(Decl);
                Name = Container->getNameAsString();
                std::cout << Name << "\n";
                for (const auto &I : Container->properties()) {
                    PrintProperty(I);
                    ++nProps;
                }

                for (const auto &I : Container->methods()) {
                    PrintMethod(I);
                    ++nMethods;
                }
                return true;
            }

            // Var, Function, EnumConstant are in global naming space
            case clang::Decl::Kind::Var: {
                clang::VarDecl *VD = clang::cast<clang::VarDecl>(Decl);
                Context->getObjCEncodingForType(VD->getType(), Types);
                // clang::APValue *Value = VD->getEvaluatedValue(); 少部分全局变量有初始值，大部分是 extern
                // Value->getAsString(*Context, VD->getType())
                Name = "var " + VD->getNameAsString();
                break;
            }
            case clang::Decl::Kind::Function: {
                clang::FunctionDecl *Function = clang::cast<clang::FunctionDecl>(Decl);
                Context->getObjCEncodingForFunctionDecl(Function, Types);
                Name = Function->getNameAsString() + "()";
                break;
            }
            case clang::Decl::Kind::EnumConstant: {
                clang::EnumConstantDecl *Enum = clang::cast<clang::EnumConstantDecl>(Decl);
                llvm::APSInt Value = Enum->getInitVal();

                Context->getObjCEncodingForType(Enum->getType(), Types); // 只有 i I q Q 整数类型。所有 enum constant 都有值。
                Name = Enum->getNameAsString() + " = " + Value.toString(10);
                indent = true;
                break;
            }

            default:
                return true;
        }
        std::cout << (indent ? "  " : "") << (Name.length() ? Name : "(Anonymous)") << " " << Types << "\n";
        return true;
    }
private:
    clang::ASTContext *Context;
};

class MyASTConsumer : public clang::ASTConsumer {
public:
    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        Visitor = llvm::make_unique<MyASTVisitor>(Context);
        Visitor->TraverseDecl(Context.getTranslationUnitDecl());
        std::cout << "properties: " << Visitor->nProps << " methods: " << Visitor->nMethods << '\n';
    }
private:
    std::unique_ptr<MyASTVisitor> Visitor;
};

class FindNamedClassAction : public clang::ASTFrontendAction {
public:
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
        return std::unique_ptr<clang::ASTConsumer>(new MyASTConsumer);
    }
};

int main(int argc, const char **argv) {

    std::string Sdk = "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk";
    std::string Arch = "x86_64"; // i386 x86_64 arm arm64
    std::string Path = Sdk + "/System/Library/Frameworks/Foundation.framework/Headers/NSArray.h";

    std::ifstream File(Path);
    std::string Code(std::istreambuf_iterator<char>(File), (std::istreambuf_iterator<char>()));

    std::vector<std::string> Args = {
        "-ObjC++", "-Wall", "-isysroot", Sdk, "-arch", Arch, "-miphoneos-version-min=7.0", "-std=c++11",
        // http://llvm.org/releases/3.4/tools/clang/docs/LibTooling.html
        // search "../lib/clang/3.8.0/include" by default
        "-I/usr/local/Cellar/llvm/3.8.0/lib/clang/3.8.0/include",
        // "-v",
    };
    clang::tooling::runToolOnCodeWithArgs(new FindNamedClassAction, Code, Args);
    return 0;
}
