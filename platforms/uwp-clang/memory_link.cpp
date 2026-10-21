// LLVM COFF linking experiment. No game code or operating-system symbol search.
#define NOMINMAX
#define WWHD_CLANG_EXPORTS
#include "link_api.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Error.h"
// Parse LLVM COFF declarations before Windows defines macros with the same names.
#include <windows.h>
#include <memoryapi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
constexpr size_t arena_size=8*1024*1024;
struct Mapper final:llvm::SectionMemoryManager::MemoryMapper {
    void* base=nullptr;size_t used=0;size_t capacity;
    explicit Mapper(size_t limit=arena_size):capacity(limit){}
    ~Mapper()override{if(base)VirtualFree(base,0,MEM_RELEASE);}
    llvm::sys::MemoryBlock allocateMappedMemory(llvm::SectionMemoryManager::AllocationPurpose,size_t bytes,const llvm::sys::MemoryBlock*,unsigned flags,std::error_code& error)override{
        if(flags&llvm::sys::Memory::MF_EXEC){error={ERROR_ACCESS_DENIED,std::system_category()};return {};}
        if(!base){
#ifdef WWHD_UWP
            base=VirtualAllocFromApp(nullptr,capacity,MEM_RESERVE,PAGE_READWRITE);
#else
            base=VirtualAlloc(nullptr,capacity,MEM_RESERVE,PAGE_READWRITE);
#endif
        }
        if(!base){error={int(GetLastError()),std::system_category()};return {};}
        if(bytes>capacity||used>capacity-bytes){error={ERROR_NOT_ENOUGH_MEMORY,std::system_category()};return {};}
        size_t size=(bytes+65535)&~size_t(65535);
        if(size>capacity-used){error={ERROR_NOT_ENOUGH_MEMORY,std::system_category()};return {};}
        void* ptr=static_cast<char*>(base)+used;
#ifdef WWHD_UWP
        auto committed=VirtualAllocFromApp(ptr,size,MEM_COMMIT,PAGE_READWRITE);
#else
        auto committed=VirtualAlloc(ptr,size,MEM_COMMIT,PAGE_READWRITE);
#endif
        if(!committed){error={int(GetLastError()),std::system_category()};return {};}
        used+=size;error.clear();return {ptr,size};
    }
    std::error_code protectMappedMemory(llvm::sys::MemoryBlock const& block,unsigned flags)override{
        bool execute=(flags&llvm::sys::Memory::MF_EXEC)!=0,write=(flags&llvm::sys::Memory::MF_WRITE)!=0;
        if(execute&&write)return {ERROR_ACCESS_DENIED,std::system_category()};
        auto start=reinterpret_cast<uintptr_t>(block.base()),low=reinterpret_cast<uintptr_t>(base);
        if(start<low||start-low>capacity||block.allocatedSize()>capacity-(start-low))return {ERROR_INVALID_ADDRESS,std::system_category()};
        ULONG old=0;DWORD access=execute?PAGE_EXECUTE_READ:(write?PAGE_READWRITE:PAGE_READONLY);
#ifdef WWHD_UWP
        BOOL ok=VirtualProtectFromApp(block.base(),block.allocatedSize(),access,&old);
#else
        BOOL ok=VirtualProtect(block.base(),block.allocatedSize(),access,&old);
#endif
        if(!ok)return {int(GetLastError()),std::system_category()};
        if(execute&&!FlushInstructionCache(GetCurrentProcess(),block.base(),block.allocatedSize()))return {int(GetLastError()),std::system_category()};
        return {};
    }
    // Blocks are slices of one arena. Release exactly once in the mapper destructor.
    std::error_code releaseMappedMemory(llvm::sys::MemoryBlock&)override{return {};}
};
struct Manager final:llvm::SectionMemoryManager {
    Mapper& mapper;std::vector<std::vector<RUNTIME_FUNCTION>> tables;bool unwind_ok=true;
    explicit Manager(Mapper& m):SectionMemoryManager(&m),mapper(m){}
    ~Manager()override{deregisterEHFrames();}
    void registerEHFrames(uint8_t* addr,uint64_t,size_t size)override{
        if(size==0)return;
        if(size%sizeof(RUNTIME_FUNCTION)){unwind_ok=false;return;}
        std::vector<RUNTIME_FUNCTION> copy(size/sizeof(RUNTIME_FUNCTION));memcpy(copy.data(),addr,size);
        for(auto const& f:copy){if(f.BeginAddress>=f.EndAddress||f.EndAddress>mapper.capacity||f.UnwindData>=mapper.capacity){unwind_ok=false;return;}}
        std::sort(copy.begin(),copy.end(),[](auto const& a,auto const& b){return a.BeginAddress<b.BeginAddress;});
        if(!RtlAddFunctionTable(copy.data(),DWORD(copy.size()),reinterpret_cast<DWORD64>(mapper.base))){unwind_ok=false;return;}
        tables.push_back(std::move(copy));
    }
    void deregisterEHFrames()override{for(auto& table:tables)RtlDeleteFunctionTable(table.data());tables.clear();}
};
extern "C" __declspec(noinline) uint32_t host_mix(uint32_t v){return v*1664525u+1013904223u;}
unsigned host_initializer_count=0;
extern "C" __declspec(noinline) void host_initialize(){++host_initializer_count;}
volatile uint32_t host_counter=0;
const uint32_t host_salt=0x13579u;
unsigned char host_flag_storage[1]{0x5a};
unsigned char* host_flags=host_flag_storage;
struct Resolver final:llvm::LegacyJITSymbolResolver {
    llvm::RuntimeDyld* linker=nullptr;
    std::map<std::string,std::string> weak_aliases;
    llvm::JITSymbol findSymbolInLogicalDylib(std::string const&)override{return nullptr;}
    llvm::JITSymbol findSymbol(std::string const& name)override{
        if(name=="wwhd_host_initialize")return {reinterpret_cast<uint64_t>(&host_initialize),llvm::JITSymbolFlags::Exported};
        if(name=="wwhd_host_mix")return {reinterpret_cast<uint64_t>(&host_mix),llvm::JITSymbolFlags::Exported};
        if(name=="wwhd_host_counter")return {reinterpret_cast<uint64_t>(&host_counter),llvm::JITSymbolFlags::Exported};
        if(name=="wwhd_host_salt")return {reinterpret_cast<uint64_t>(&host_salt),llvm::JITSymbolFlags::Exported};
        if(name=="wwhd_host_flags")return {reinterpret_cast<uint64_t>(&host_flags),llvm::JITSymbolFlags::Exported};
        // COFF weak external aliases are not section definitions. Resolve their
        // fallback only after checking the explicit host registry.
        auto fallback=weak_aliases.find(name);
        if(linker&&fallback!=weak_aliases.end())return llvm::JITSymbol(linker->getSymbol(fallback->second));
        return nullptr;
    }
};
struct State {uint32_t remaining,value;uintptr_t stack_low,stack_high;};
static_assert(sizeof(State)==24);
DWORD invoke_initializer(void* entry){
    __try{reinterpret_cast<void(*)()>(entry)();return 0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
DWORD invoke(void* entry,State* state){
    __try{reinterpret_cast<void(*)(State*)>(entry)(state);return 0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
// Objects must come from the caller's compilation in this process. Diagnostic only.
extern "C" __declspec(dllexport) int wwhd_clang_link_tail_test(
    const unsigned char* a,size_t a_size,const unsigned char* b,size_t b_size,
    void* opaque,void(*report)(void*,const char*,size_t)){
    auto say=[&](std::string const& s){report(opaque,s.data(),s.size());};
    if(!a||!b||!report||a_size>1024*1024||b_size>1024*1024)return -1;
    Mapper mapper;Manager memory(mapper);Resolver resolver;llvm::RuntimeDyld linker(memory,resolver);resolver.linker=&linker;
    std::array<llvm::StringRef,2> data{llvm::StringRef(reinterpret_cast<char const*>(a),a_size),llvm::StringRef(reinterpret_cast<char const*>(b),b_size)};
    std::vector<std::unique_ptr<llvm::object::ObjectFile>> objects;
    std::vector<std::unique_ptr<llvm::RuntimeDyld::LoadedObjectInfo>> loaded;
    for(auto const& bytes:data){
        auto obj=llvm::object::ObjectFile::createObjectFile(llvm::MemoryBufferRef(bytes,"compiled-unit"));
        if(!obj){say(llvm::toString(obj.takeError()));return -2;}
        objects.push_back(std::move(*obj));
        auto* coff=llvm::dyn_cast<llvm::object::COFFObjectFile>(objects.back().get());
        if(!coff||coff->getMachine()!=IMAGE_FILE_MACHINE_AMD64){say("FAIL expected x64 COFF");return -2;}
        for(auto const& symbol:coff->symbols()){
            auto raw=coff->getCOFFSymbol(symbol);
            if(auto* alias=raw.getWeakExternal()){
                if(alias->Characteristics!=IMAGE_WEAK_EXTERN_SEARCH_ALIAS){say("FAIL unsupported weak external kind");return -2;}
                auto target=coff->getSymbol(alias->TagIndex);
                if(!target){say(llvm::toString(target.takeError()));return -2;}
                auto name=coff->getSymbolName(raw),fallback=coff->getSymbolName(*target);
                if(!name){say(llvm::toString(name.takeError()));return -2;}
                if(!fallback){say(llvm::toString(fallback.takeError()));return -2;}
                resolver.weak_aliases[name->str()]=fallback->str();
            }
        }
        loaded.push_back(linker.loadObject(*objects.back()));
        if(linker.hasError()){say(linker.getErrorString().str());return -3;}
    }
    linker.resolveRelocations();
    if(linker.hasError()){say(linker.getErrorString().str());return -4;}
    linker.registerEHFrames();
    if(!memory.unwind_ok||memory.tables.empty()){say("FAIL x64 unwind registration");return -5;}
    std::string error;if(memory.finalizeMemory(&error)){say(error);return -6;}
    // Clang's C constructor attribute emits .CRT$XCU pointer records on Windows.
    // Run only this diagnostic's freshly compiled records, after relocations and RX.
    host_initializer_count=0;
    for(size_t i=0;i<objects.size();++i){
        for(auto const& section:objects[i]->sections()){
            auto name=section.getName();if(!name){say(llvm::toString(name.takeError()));return -11;}
            if(*name!=".CRT$XCU")continue;
            auto address=loaded[i]->getSectionLoadAddress(section),size=section.getSize();
            auto base=reinterpret_cast<uint64_t>(mapper.base);
            if(size%sizeof(uint64_t)||address<base||address-base>arena_size||size>arena_size-(address-base))return -11;
            for(size_t offset=0;offset<size;offset+=sizeof(uint64_t)){
                uint64_t target=0;memcpy(&target,reinterpret_cast<void*>(address+offset),sizeof(target));
                if(target<base||target-base>=arena_size)return -11;
                auto exception=invoke_initializer(reinterpret_cast<void*>(target));
                if(exception){say("FAIL generated initializer exception="+std::to_string(exception));return -11;}
            }
        }
    }
    if(host_initializer_count!=1){say("FAIL expected exactly one generated initializer");return -11;}
    say("PASS generated COFF initializer executed once before entry");
    auto entry=linker.getSymbolLocalAddress("tail_a");if(!entry)return -7;
    DWORD64 image_base=0;
    auto unwind=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entry),&image_base,nullptr);
    if(!unwind||image_base!=reinterpret_cast<DWORD64>(mapper.base)){
        say("FAIL generated function not discoverable by Windows x64 unwind lookup");return -10;
    }
    say("PASS generated function discoverable by Windows x64 unwind lookup");
    constexpr uint32_t count=1000000;State state{count,7,UINTPTR_MAX,0};
    host_counter=0;
    auto exception=invoke(entry,&state);
    if(exception){say("FAIL tail execution exception="+std::to_string(exception));return -8;}
    uint32_t expected=7;for(uint32_t i=0;i<count;++i)expected=host_mix((expected ^ host_salt ^ host_flag_storage[0])+17u);
    if(host_counter!=count||state.remaining||state.value!=expected||state.stack_low==UINTPTR_MAX||state.stack_high<state.stack_low||state.stack_high-state.stack_low>256){say("FAIL tail parity/stack bound");return -9;}
    say("PASS two separately compiled units, explicit runtime import and 1000000 tail calls; stack span="+std::to_string(state.stack_high-state.stack_low)+" bytes; registered unwind tables="+std::to_string(memory.tables.size()));
    auto code_address=reinterpret_cast<uint64_t>(entry),data_address=reinterpret_cast<uint64_t>(&host_counter);
    auto distance=code_address>data_address?code_address-data_address:data_address-code_address;
    say("PASS runtime data read/write through import slots and native override and default weak fallback; code/data distance MiB="+std::to_string(distance/1048576));
    return 0;
}

namespace {
struct ImageResolver final:llvm::LegacyJITSymbolResolver {
    llvm::RuntimeDyld* linker=nullptr;
    std::map<std::string,llvm::JITEvaluatedSymbol> native;
    std::map<std::string,std::string> aliases;
    llvm::JITSymbol findSymbolInLogicalDylib(std::string const&)override{return nullptr;}
    llvm::JITSymbol findSymbol(std::string const& name)override{
        auto found=native.find(name);if(found!=native.end())return llvm::JITSymbol(found->second);
        auto alias=aliases.find(name);
        if(linker&&alias!=aliases.end())return llvm::JITSymbol(linker->getSymbol(alias->second));
        return nullptr;
    }
};
struct InitializerSection {std::string name;uint64_t address,size;};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string unwrap(llvm::Expected<llvm::StringRef> value){
    if(!value)throw std::runtime_error(llvm::toString(value.takeError()));return value->str();
}
}
struct WwhdLinkedImage {
    Mapper mapper;Manager memory;ImageResolver resolver;
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> buffers;
    std::vector<std::unique_ptr<llvm::object::ObjectFile>> objects;
    llvm::RuntimeDyld linker;
    std::vector<std::unique_ptr<llvm::RuntimeDyld::LoadedObjectInfo>> loaded;
    std::vector<InitializerSection> initializers;
    bool initialization_attempted=false;
    explicit WwhdLinkedImage(size_t capacity):mapper(capacity),memory(mapper),linker(memory,resolver){resolver.linker=&linker;}
};
extern "C" int wwhd_clang_link_create(const WwhdLinkRequest* request,void* opaque,WwhdClangDiagnostic report,WwhdLinkedImage** result){
    if(result)*result=nullptr;
    if(!report||!result)return -1;
    auto say=[&](std::string const& message){report(opaque,message.data(),message.size());};
    try{
        require(request&&request->version==1,"Unsupported linker request version");
        require(request->objects&&request->object_count&&request->object_count<=256,"Invalid object count");
        require(request->symbol_count<=16384&&(!request->symbol_count||request->symbols),"Invalid native symbol count");
        require(request->arena_bytes>=1048576&&request->arena_bytes<=512ull*1048576&&request->arena_bytes%65536==0,"Invalid image arena size");
        auto image=std::make_unique<WwhdLinkedImage>(request->arena_bytes);
        std::map<std::string,unsigned> definitions,natives;
        for(size_t i=0;i<request->symbol_count;++i){
            auto const& symbol=request->symbols[i];
            require(symbol.name&&*symbol.name&&std::strlen(symbol.name)<=512&&symbol.address,"Invalid native symbol");
            require(symbol.kind==WWHD_LINK_FUNCTION||symbol.kind==WWHD_LINK_DATA,"Invalid native symbol kind");
            require(natives.emplace(symbol.name,symbol.kind).second,"Duplicate native symbol");
            auto flags=llvm::JITSymbolFlags::Exported;
            if(symbol.kind==WWHD_LINK_FUNCTION)flags=flags|llvm::JITSymbolFlags::Callable;
            image->resolver.native.emplace(symbol.name,llvm::JITEvaluatedSymbol(symbol.address,flags));
        }
        size_t total=0;
        for(size_t i=0;i<request->object_count;++i){
            auto const& input=request->objects[i];
            require(input.data&&input.size>=20&&input.size<=16*1048576,"Invalid COFF object size");
            require(input.size<=256*1048576-total,"Object input budget exceeded");total+=input.size;
            image->buffers.push_back(llvm::MemoryBuffer::getMemBufferCopy(llvm::StringRef(reinterpret_cast<char const*>(input.data),input.size),"user-generated-unit"));
            auto object=llvm::object::ObjectFile::createObjectFile(image->buffers.back()->getMemBufferRef());
            if(!object)throw std::runtime_error(llvm::toString(object.takeError()));
            auto* coff=llvm::dyn_cast<llvm::object::COFFObjectFile>(object->get());
            require(coff&&coff->getMachine()==IMAGE_FILE_MACHINE_AMD64,"Expected Windows x64 COFF objects");
            for(auto const& symbol:coff->symbols()){
                auto raw=coff->getCOFFSymbol(symbol);
                auto name=unwrap(coff->getSymbolName(raw));
                if(auto* alias=raw.getWeakExternal()){
                    require(alias->Characteristics==IMAGE_WEAK_EXTERN_SEARCH_ALIAS,"Unsupported COFF weak external kind");
                    auto target=coff->getSymbol(alias->TagIndex);
                    if(!target)throw std::runtime_error(llvm::toString(target.takeError()));
                    auto fallback=unwrap(coff->getSymbolName(*target));
                    auto old=image->resolver.aliases.find(name);
                    require(old==image->resolver.aliases.end()||old->second==fallback,"Conflicting weak external aliases");
                    image->resolver.aliases[name]=fallback;
                }else if(raw.isExternal()&&raw.getSectionNumber()>0){
                    auto type=symbol.getType();if(!type)throw std::runtime_error(llvm::toString(type.takeError()));
                    unsigned kind=*type==llvm::object::SymbolRef::ST_Function?WWHD_LINK_FUNCTION:WWHD_LINK_DATA;
                    auto old=definitions.find(name);
                    require(old==definitions.end()||old->second==kind,"Conflicting symbol kinds");
                    definitions[name]=kind;
                }
            }
            image->objects.push_back(std::move(*object));
        }
        // Validate every referenced import before RuntimeDyld can fail fatally.
        for(auto const& object:image->objects){
            auto* coff=llvm::cast<llvm::object::COFFObjectFile>(object.get());
            for(auto const& section:coff->sections()){
                for(auto const& relocation:section.relocations()){
                    auto type=relocation.getType();
                    require(type==1||type==3||(type>=4&&type<=9),"Unsupported COFF relocation type");
                    auto symbol=relocation.getSymbol();require(symbol!=coff->symbol_end(),"COFF relocation without symbol");
                    auto raw=coff->getCOFFSymbol(*symbol);if(raw.getSectionNumber()>0)continue;
                    auto name=unwrap(symbol->getName());bool indirect=name.rfind("__imp_",0)==0;
                    if(indirect)name.erase(0,6);
                    auto native=natives.find(name),defined=definitions.find(name);
                    if(native==natives.end()&&defined==definitions.end()){
                        auto fallback=image->resolver.aliases.find(name);
                        if(fallback==image->resolver.aliases.end()||!definitions.count(fallback->second))
                            throw std::runtime_error("Unresolved runtime import: "+name);
                    }
                    unsigned kind=native!=natives.end()?native->second:(defined!=definitions.end()?defined->second:definitions.at(image->resolver.aliases.at(name)));
                    require(indirect||kind!=WWHD_LINK_DATA||type<4||type>9,"External REL32 data requires an indirect import slot");
                }
            }
        }
        for(auto const& object:image->objects){
            image->loaded.push_back(image->linker.loadObject(*object));
            if(image->linker.hasError())throw std::runtime_error(image->linker.getErrorString().str());
        }
        image->linker.resolveRelocations();
        if(image->linker.hasError())throw std::runtime_error(image->linker.getErrorString().str());
        image->linker.registerEHFrames();require(image->memory.unwind_ok,"x64 unwind registration failed");
        std::string error;bool failed=image->memory.finalizeMemory(&error);require(!failed,error.c_str());
        for(size_t i=0;i<image->objects.size();++i){
            for(auto const& section:image->objects[i]->sections()){
                auto name=unwrap(section.getName());if(name!=".CRT$XCU")continue;
                image->initializers.push_back({name,image->loaded[i]->getSectionLoadAddress(section),section.getSize()});
            }
        }
        say("PASS linked image: objects="+std::to_string(image->objects.size())+" committed arena KiB="+std::to_string(image->mapper.used/1024)+" unwind tables="+std::to_string(image->memory.tables.size()));
        *result=image.release();return 0;
    }catch(std::exception const& error){say(std::string("FAIL link image: ")+error.what());return -2;}
}
extern "C" unsigned long long wwhd_clang_link_find(WwhdLinkedImage* image,const char* name){
    if(!image||!name)return 0;return reinterpret_cast<uint64_t>(image->linker.getSymbolLocalAddress(name));
}
extern "C" int wwhd_clang_link_initialize(WwhdLinkedImage* image,void* opaque,WwhdClangDiagnostic report){
    if(!image||!report)return -1;
    auto say=[&](const char* message){report(opaque,message,std::strlen(message));};
    if(image->initialization_attempted){say("FAIL image initialization already attempted");return -2;}
    image->initialization_attempted=true;
    auto base=reinterpret_cast<uint64_t>(image->mapper.base);size_t count=0;
    for(auto const& section:image->initializers){
        if(section.size%8||section.address<base||section.address-base>image->mapper.used||section.size>image->mapper.used-(section.address-base))return -3;
        for(size_t offset=0;offset<section.size;offset+=8){
            uint64_t target=0;memcpy(&target,reinterpret_cast<void*>(section.address+offset),8);
            if(target<base||target-base>=image->mapper.used)return -3;
            if(invoke_initializer(reinterpret_cast<void*>(target))){say("FAIL generated initializer raised an exception");return -4;}
            ++count;
        }
    }
    auto message=std::string("PASS linked image initializers executed exactly once; constructors=")+std::to_string(count);
    say(message.c_str());return 0;
}
extern "C" void wwhd_clang_link_destroy(WwhdLinkedImage* image){delete image;}

// Project-authored lifecycle regression. No game code or implicit host exports.
extern "C" __declspec(dllexport) int wwhd_clang_link_image_test(
 const unsigned char* a,size_t a_size,const unsigned char* b,size_t b_size,
 void* opaque,WwhdClangDiagnostic report){
    if(!a||!b||!report)return -1;
    auto say=[&](std::string const& message){report(opaque,message.data(),message.size());};
    WwhdClangObject objects[]{{a,a_size},{b,b_size}};
    WwhdRuntimeSymbol symbols[]{
        {"wwhd_host_mix",reinterpret_cast<uint64_t>(&host_mix),WWHD_LINK_FUNCTION},
        {"wwhd_host_initialize",reinterpret_cast<uint64_t>(&host_initialize),WWHD_LINK_FUNCTION},
        {"wwhd_host_counter",reinterpret_cast<uint64_t>(&host_counter),WWHD_LINK_DATA},
        {"wwhd_host_salt",reinterpret_cast<uint64_t>(&host_salt),WWHD_LINK_DATA},
        {"wwhd_host_flags",reinterpret_cast<uint64_t>(&host_flags),WWHD_LINK_DATA}};
    WwhdLinkRequest request{1,objects,2,symbols,5,8*1048576};
    auto capture=+[](void* context,const char* bytes,size_t size){static_cast<std::string*>(context)->append(bytes,size);};
    WwhdLinkedImage* image=nullptr;std::string rejected;
    request.symbol_count=0;
    int status=wwhd_clang_link_create(&request,&rejected,capture,&image);
    if(status==0||image||rejected.find("Unresolved runtime import")==std::string::npos){if(image)wwhd_clang_link_destroy(image);say("FAIL missing import preflight");return -2;}
    say("PASS missing runtime imports rejected before code allocation");
    request.symbol_count=5;
    status=wwhd_clang_link_create(&request,opaque,report,&image);if(status)return -3;
    std::unique_ptr<WwhdLinkedImage,void(*)(WwhdLinkedImage*)> owner(image,wwhd_clang_link_destroy);
    if(wwhd_clang_link_find(image,"does_not_exist")){say("FAIL unknown export");return -4;}
    auto entry=wwhd_clang_link_find(image,"tail_a");if(!entry)return -4;
    DWORD64 image_base=0;
    if(!RtlLookupFunctionEntry(entry,&image_base,nullptr)||image_base!=reinterpret_cast<uint64_t>(image->mapper.base)){say("FAIL linked image unwind lookup");return -5;}
    host_initializer_count=0;host_counter=0;
    if(wwhd_clang_link_initialize(image,opaque,report)||host_initializer_count!=1)return -6;
    rejected.clear();
    if(wwhd_clang_link_initialize(image,&rejected,capture)==0||host_initializer_count!=1){say("FAIL repeated image initialization");return -7;}
    constexpr uint32_t calls=1000000;State state{calls,7,UINTPTR_MAX,0};
    auto exception=invoke(reinterpret_cast<void*>(entry),&state);
    uint32_t expected=7;for(uint32_t i=0;i<calls;++i)expected=host_mix((expected^host_salt^host_flag_storage[0])+17u);
    if(exception||state.remaining||state.value!=expected||host_counter!=calls||state.stack_high<state.stack_low||state.stack_high-state.stack_low>256){say("FAIL linked image execution");return -8;}
    owner.reset();
    DWORD64 old_base=0;
    if(RtlLookupFunctionEntry(entry,&old_base,nullptr)){say("FAIL unwind registration survived image destruction");return -9;}
    say("PASS linked image lifetime: 1000000 tail calls, runtime data, weak fallback, initializer once, unwind cleanup after destroy");
    return 0;
}
