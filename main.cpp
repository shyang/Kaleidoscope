//
//  tool.cpp
//  Kaleidoscope
//
//  Created by shaohua on 6/7/16.
//  Copyright © 2016 syang. All rights reserved.
//

// Tutorial:
// https://clang.llvm.org/docs/LibASTMatchersTutorial.html
// https://clang.llvm.org/docs/RAVFrontendAction.html

// brew install llvm
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

// brew install jsoncpp
#include <json/json.h>

#include <fstream>
#include <iostream>
#include <cassert>
#include <unordered_map>
#include <map>

class MyASTVisitor : public clang::RecursiveASTVisitor<MyASTVisitor> {
public:
    explicit MyASTVisitor(clang::ASTContext &Context)
        : Context(&Context) {}

    void EnumerateContainer(clang::ObjCContainerDecl *Container, std::string Name) {
        auto &Cls = Root["class"][Name];

        for (const auto Property : Container->properties()) {
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

            Cls[Property->getNameAsString()] = Types;
        }

        for (const auto Method : Container->methods()) {
            clang::Selector Selector = Method->getSelector();
            /*
             object: @ ==> @"ClassName"
             block: @? ==> @?<Encoding>

             arrayWithContentsOfFile: @"NSMutableArray"24@0:8@"NSString"16
             sortWithOptions:usingComparator: v32@0:8Q16@?<q@?@@>24
             */
            std::string Types = Context->getObjCEncodingForMethodDecl(Method, true /* Extended */);
            std::string Name = Selector.getAsString();
            Cls[Name] = Types;
        }
    }

    bool VisitDecl(clang::Decl *Decl) {
        switch (Decl->getKind()) {
            case clang::Decl::Kind::ObjCCategoryImpl: {
                auto CategoryImpl = clang::cast<clang::ObjCCategoryImplDecl>(Decl);
                auto Category = CategoryImpl->getCategoryDecl();
                auto Interface = Category->getClassInterface();
                EnumerateContainer(CategoryImpl, Interface->getNameAsString());
            }
                break;
            case clang::Decl::Kind::ObjCCategory: {
                auto Category = clang::cast<clang::ObjCCategoryDecl>(Decl);
                auto Interface = Category->getClassInterface();
                EnumerateContainer(Category, Interface->getNameAsString());
                break;
            }
            case clang::Decl::Kind::ObjCProtocol: {
                auto Container = clang::cast<clang::ObjCProtocolDecl>(Decl);
                Root["protocols"].append(Container->getNameAsString());
                EnumerateContainer(Container, Container->getNameAsString());
                break;
            }
            case clang::Decl::Kind::ObjCInterface:
            case clang::Decl::Kind::ObjCImplementation: {
                auto Container = clang::cast<clang::ObjCContainerDecl>(Decl);
                EnumerateContainer(Container, Container->getNameAsString());
                break;
            }

            // Var, Function, EnumConstant are in global naming space
            case clang::Decl::Kind::Var: {
                clang::VarDecl *VD = clang::cast<clang::VarDecl>(Decl);
                if (!VD->hasExternalStorage()) {
                    break;
                }
                std::string Types;
                Context->getObjCEncodingForType(VD->getType(), Types);
                // clang::APValue *Value = VD->getEvaluatedValue(); 少部分全局变量有初始值，大部分是 extern
                // Value->getAsString(*Context, VD->getType())
                Root["var"][VD->getNameAsString()] = Types;
                break;
            }
            case clang::Decl::Kind::Function: {
                clang::FunctionDecl *Function = clang::cast<clang::FunctionDecl>(Decl);
                if (!Function->isExternC()) {
                    break;
                }
                std::string Types = Context->getObjCEncodingForFunctionDecl(Function);
                Root["func"][Function->getNameAsString()] = Types;
                break;
            }
            case clang::Decl::Kind::Enum: {
                auto Enum = clang::cast<clang::EnumDecl>(Decl);
                auto Name = Enum->getNameAsString();
                if (!Name.length()) {
                    auto File = Enum->getLocation().printToString(Context->getSourceManager());
                    Name = File.substr(0, File.rfind('<') - 1);
                    Name = Name.substr(Name.rfind('/') + 1);
                }
                auto &Map = Root["enum"][Name];

                for (const auto I : Enum->enumerators()) {
                    llvm::APSInt Value = I->getInitVal();
                    Map[I->getNameAsString()] = Value.toString(10);
                }
                // std::string Types;
                // Context->getObjCEncodingForType(Enum->getType(), Types); // 只有 i I q Q 整数类型。所有 enum constant 都有值。
                break;
            }

            default:
                return true;
        }
        return true;
    }

    clang::ASTContext *Context;

    // class -> {name -> type}
    // var -> type/value
    // func -> type
    Json::Value Root;
};

class MyASTConsumer : public clang::ASTConsumer {
public:
    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        MyASTVisitor Visitor(Context);
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
        std::cout << Visitor.Root << '\n';
    }
private:
    std::unique_ptr<MyASTVisitor> Visitor;
};

class MyAction : public clang::ASTFrontendAction {
public:
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
        return std::unique_ptr<clang::ASTConsumer>(new MyASTConsumer);
    }
};

int main(int argc, const char **argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n  SignatureExtractor -arch arm64 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk/System/Library/Frameworks/Foundation.framework/Headers/NSArray.h\n";
        return -1;
    }
    std::vector<std::string> Args = {
        "-ObjC", "-Wall", "-miphoneos-version-min=7.0",
        // "-std=c++11",
        // http://llvm.org/releases/3.4/tools/clang/docs/LibTooling.html
        // search "../lib/clang/3.8.0/include" by default
        "-I/usr/local/Cellar/llvm/7.0.0/lib/clang/7.0.0/include",
        // "-v",
    };

    // -isysroot
    // -arch=x86_64 | i386 | arm | arm64
    for (int i = 1; i < argc - 1; ++i) {
        Args.push_back(argv[i]);
    }

    std::ifstream File(argv[argc - 1]);
    std::string Code(std::istreambuf_iterator<char>(File), (std::istreambuf_iterator<char>()));

    clang::tooling::runToolOnCodeWithArgs(new MyAction, Code, Args);
    return 0;
}
