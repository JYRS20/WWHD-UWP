// Desktop-run test of the actual WWHD_UWP core. No game functions or GX2 backend are stubbed into a "game".
// The synthetic tables below isolate loader/host helpers and import storage; no game is dispatched.
#include "runtime.h"
#include "recomp_table.h"
#include "write_watch.h"
#include "platform/host.h"
#include "platform/process.h"
#include <zlib.h>
#include <fstream>
#include <iostream>
#include <algorithm>
extern "C" {
const RecompEntry g_recomp_funcs[] = {{0, nullptr}};
const unsigned g_recomp_func_count = 0;
const RecompImport g_recomp_imports[] = {
    {0,0xC1006000u,"coreinit","__gh_FOPEN_MAX",0,nullptr},
    {0,0xC1007000u,"coreinit","test_data",0,nullptr},
    {0xC0000000u,0xC0000000u,"coreinit","test_function",1,nullptr},
};
const unsigned g_recomp_import_count = 3;
const uint32_t g_recomp_entry_point = 0;
}
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main(int argc,char** argv) {
 try {
    mem::init();
    MEMORY_BASIC_INFORMATION info{};
    require(VirtualQuery(PPC_MEM_BASE,&info,sizeof info)!=0,"guest map query failed");
    require(info.Protect==PAGE_NOACCESS,"null guard was not preserved");
    // Query actual OS commit accounting, not an estimate from the configured regions.
    uint64_t committed=0, reserved=0;
    for(uint64_t offset=0;offset<0x100000000ull;) {
        MEMORY_BASIC_INFORMATION region{};
        require(VirtualQuery(PPC_MEM_BASE+offset,&region,sizeof region)!=0,"guest region query failed");
        const uint64_t end=std::min<uint64_t>(0x100000000ull,
            (uint8_t*)region.BaseAddress-PPC_MEM_BASE+region.RegionSize);
        require(end>offset,"guest region query made no progress");
        if(region.State==MEM_COMMIT)committed+=end-offset;
        else if(region.State==MEM_RESERVE)reserved+=end-offset;
        else require(false,"guest address range lost its reservation");
        offset=end;
    }
    require(committed==1624ull*1024*1024+8192,"unexpected guest committed memory charge");
    require(committed+reserved==0x100000000ull,"guest map is not fully reserved");
    for(uint32_t address : {0xC1006000u,0xC1006fffu,0xC1007000u,0xC1007fffu}) {
        require(VirtualQuery(mem::ptr(address),&info,sizeof info)!=0 && info.State==MEM_COMMIT,
                "import data page must be committed before startup writes");
        *mem::ptr(address)=0x67;
        require(*mem::ptr(address)==0x67,"import data page must be writable");
    }
    require(VirtualQuery(mem::ptr(0xC0000000u),&info,sizeof info)!=0 && info.State==MEM_RESERVE,
            "dispatch-only function slots need no guest storage");
    for(uint32_t address : {0x50000000u,0x70000000u,0x80000000u,0xE2800000u,0xF6000000u}) {
        require(VirtualQuery(mem::ptr(address),&info,sizeof info)!=0 && info.State==MEM_RESERVE,
                "unused guest hole must not consume commit budget");
    }
    for(uint32_t address : {0x10000u,0x02000000u,mem::kMem2Start,mem::kMem2End-1,
         mem::kRuntimeStart,mem::kRuntimeEnd-1,0x7F000000u,0x7FFFFFFFu,
         mem::kFgBucket,mem::kFgBucket+mem::kFgBucketSize-1,mem::kMem1,mem::kMem1+mem::kMem1Size-1}) {
        *mem::ptr(address)=0x5a;
        require(*mem::ptr(address)==0x5a,"supported guest region must remain writable");
    }
    std::cout<<"Guest commitment: 1624 MiB plus two data-import pages; remaining holes reserved\n";
    require(!wwatch::active(),"UWP must not install a vectored exception tracker");
    auto value=mem::ptr(0x60010000);
    value[0]=0x52; value[1]=0xa6;
    auto stamp=wwatch::arm(0x60010000,2);
    value[0]=0x7b; // Must remain writable; an unsupported handler must never leave armed pages.
    require(value[0]==0x7b && value[1]==0xa6,"guest memory write failed");
    require(host::memory_touched(value,2),"UWP state capture must not discard an unknown page");
    (void)stamp;
    auto process=host::run_process({"this-tool-must-never-start.exe"});
    require(process.code==-1 && !process.error.empty(),"external tool failure must be explicit");
    LoadedModule absent{};
    require(!load_rpx("does-not-exist.rpx",absent),"missing RPX should fail");
    if(argc==3) {
        LoadedModule module{};
        require(load_rpx(argv[1],module),"real RPX did not load");
        std::ifstream expected(argv[2],std::ios::binary);
        require(bool(expected),"missing independent section checksums");
        uint32_t count=0;
        expected.read(reinterpret_cast<char*>(&count),4);
        require(count>0 && count<128,"invalid section count");
        for(uint32_t i=0;i<count;++i) {
            uint32_t values[3]{};
            expected.read(reinterpret_cast<char*>(values),sizeof values);
            require(bool(expected),"truncated section checksums");
            auto checksum=crc32(0,mem::ptr(values[0]),values[1]);
            require(checksum==values[2],"RPX section bytes differ from independent Python decode");
        }
        require(module.entry!=0 && module.stack_size!=0,"invalid loaded module metadata");
        std::cout<<"Real RPX loader: "<<count<<" sections verified against independent checksums\n";
    } else require(argc==1,"usage: uwp-core-test [cking.rpx checksums.bin]");
    // A loaded RPX can place an allocated section outside the predefined heaps.
    // Exercise the real loader against a previously uncommitted hole.
    const auto probe=std::filesystem::temp_directory_path()/
        ("wwhd-uwp-section-"+std::to_string(GetCurrentProcessId())+".rpx");
    std::vector<uint8_t> rpx(0x80);
    auto be=[&](size_t at,uint32_t value){for(int n=0;n<4;n++)rpx[at+n]=uint8_t(value>>(24-n*8));};
    be(0x20,0x40);rpx[0x2f]=40;rpx[0x31]=1;
    be(0x44,8);be(0x48,2);be(0x4c,0x80000000);be(0x54,4096);
    {std::ofstream f(probe,std::ios::binary);f.write((const char*)rpx.data(),rpx.size());require(bool(f),"synthetic RPX write failed");}
    LoadedModule extra{};
    const bool loaded=load_rpx(probe.string(),extra);
    std::filesystem::remove(probe);
    require(loaded,"RPX with allocated section in a reserved hole failed");
    require(VirtualQuery(mem::ptr(0x80000000),&info,sizeof info)!=0 && info.State==MEM_COMMIT,
            "RPX section did not commit its reserved pages");
    require(*mem::ptr(0x80000000)==0 && *mem::ptr(0x80000fff)==0,"RPX BSS was not zero filled");
    require(VirtualFree(PPC_MEM_BASE,0,MEM_RELEASE)!=0,"guest memory release failed");
    std::cout<<"UWP core: 4 GiB mapping, guard, writable fallback, conservative state capture and process restrictions PASS (desktop context)\n";
    return 0;
 } catch(std::exception const& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
