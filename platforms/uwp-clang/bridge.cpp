// In-process optimizing C frontend. No game-specific code or subprocess invocation.
#define WWHD_CLANG_EXPORTS
#include "clang_api.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>
#include <cstring>
#include <set>
#include <cctype>
#include <mutex>
#include <string>
#include <vector>
namespace {
bool valid_path(const char* name){
    if(!name||!*name||std::strlen(name)>200)return false;
    std::string segment;
    for(auto p=name;;++p){
        unsigned char ch=static_cast<unsigned char>(*p);
        if(!ch||ch=='/'){
            if(segment.empty()||segment=="."||segment=="..")return false;
            segment.clear();if(!ch)return true;
        }else{
            if(!(std::isalnum(ch)||ch=='_'||ch=='-'||ch=='.'))return false;
            segment+=char(ch);
        }
    }
}
}
extern "C" int wwhd_clang_compile_files(const WwhdClangRequest* request,void* opaque,
    WwhdClangOutput output,WwhdClangDiagnostic diagnostic){
    if(!request||request->version!=1||!output||!diagnostic)return -1;
    auto reject=[&](const char* message){diagnostic(opaque,message,std::strlen(message));return -1;};
    if(!valid_path(request->main_file)||!request->files||!request->file_count||request->file_count>512)
        return reject("Invalid virtual compiler input");
    constexpr size_t limit=128*1024*1024;size_t total=0;std::set<std::string> names;bool main_found=false;
    for(size_t i=0;i<request->file_count;++i){
        auto const& f=request->files[i];
        if(!valid_path(f.path)||!f.data||f.size>limit-total)return reject("Invalid virtual file or input size");
        total+=f.size;
        std::string key=f.path;for(auto& ch:key)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        if(!names.insert(key).second)return reject("Duplicate virtual file");
        if(std::strcmp(f.path,request->main_file)==0)main_found=true;
    }
    if(!main_found)return reject("Main file not present in virtual inputs");
    static std::once_flag once;
    std::call_once(once,[]{llvm::InitializeNativeTarget();llvm::InitializeNativeTargetAsmPrinter();llvm::InitializeNativeTargetAsmParser();});
    std::string errors;llvm::raw_string_ostream error_stream(errors);
    auto options=llvm::makeIntrusiveRefCnt<clang::DiagnosticOptions>();
    clang::CompilerInstance compiler;
    auto filesystem=llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
    filesystem->setCurrentWorkingDirectory("C:/wwhd-compiler");
    for(size_t i=0;i<request->file_count;++i){
        auto const& f=request->files[i];
        filesystem->addFile(std::string("C:/wwhd-compiler/")+f.path,0,
            llvm::MemoryBuffer::getMemBufferCopy(llvm::StringRef(f.data,f.size)));
    }
    const std::string main_path=std::string("C:/wwhd-compiler/")+request->main_file;
    compiler.createDiagnostics(*filesystem,new clang::TextDiagnosticPrinter(error_stream,options.get()),true);
    auto invocation=std::make_shared<clang::CompilerInvocation>();
    const char* args[]={"-triple","x86_64-pc-windows-msvc","-emit-obj","-O2","-x","c","-std=c11","-fdeclspec",
        "-nostdsysteminc","-nobuiltininc","-DWWHD_RUNTIME_DATA_IMPORT=1","-funwind-tables=2","-relaxed-aliasing","-fwrapv","-ffp-contract=off","-vectorize-loops","-vectorize-slp","-I","C:/wwhd-compiler","-Werror","-o","-",main_path.c_str()};
    if(!clang::CompilerInvocation::CreateFromArgs(*invocation,args,compiler.getDiagnostics())){
        diagnostic(opaque,errors.data(),errors.size());return -2;
    }
    compiler.setInvocation(invocation);
    compiler.createFileManager(filesystem);
    llvm::SmallVector<char,4096> object;
    compiler.setOutputStream(std::make_unique<llvm::raw_svector_ostream>(object));
    clang::EmitObjAction action;
    bool ok=compiler.ExecuteAction(action);
    diagnostic(opaque,errors.data(),errors.size());
    if(!ok)return -3;
    output(opaque,reinterpret_cast<const unsigned char*>(object.data()),object.size());return 0;
}

extern "C" int wwhd_clang_compile(const char* source,void* opaque,WwhdClangOutput output,WwhdClangDiagnostic diagnostic){
    if(!source)return -1;
    WwhdClangFile file{"input.c",source,std::strlen(source)};
    WwhdClangRequest request{1,"input.c",&file,1};
    return wwhd_clang_compile_files(&request,opaque,output,diagnostic);
}
