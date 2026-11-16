// SPDX-License-Identifier: MPL-2.0
// Isolated renderer-smoke experiment. Never protects guest/game memory.
#ifdef WWHD_UWP
#include "runtime.h"
#include "../../exception_report.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace gfxvk {
namespace {
struct ProbeContext {uint8_t* base=nullptr;size_t size=0,page=0;std::atomic<unsigned> faults=0;};
thread_local ProbeContext* context=nullptr;
int probe_filter(EXCEPTION_POINTERS* exception) noexcept {
 auto* ctx=context;auto* record=exception->ExceptionRecord;
 if(!ctx || record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record->NumberParameters<2 || record->ExceptionInformation[0]!=1)return EXCEPTION_CONTINUE_SEARCH;
 const uintptr_t addr=record->ExceptionInformation[1],base=reinterpret_cast<uintptr_t>(ctx->base);
 if(addr<base || addr-base>=ctx->size)return EXCEPTION_CONTINUE_SEARCH;
 ULONG old=0;
 if(!VirtualProtectFromApp(ctx->base+((addr-base)/ctx->page)*ctx->page,ctx->page,PAGE_READWRITE,&old))return EXCEPTION_CONTINUE_SEARCH;
 ctx->faults.fetch_add(1,std::memory_order_relaxed);
 return EXCEPTION_CONTINUE_EXECUTION;
}
// No C++ objects with destructors in this SEH frame. All unexpected faults and
// C++ exceptions continue to existing handlers; no global VEH is installed.
__declspec(noinline) int protected_call(int(*fn)(void*),void* arg) {
 __try {return fn(arg);}
 __except(probe_filter(GetExceptionInformation())) {return -1;}
}
int run(ProbeContext& ctx,int(*fn)(void*),void* arg) {
 auto* previous=context;context=&ctx;
 try {int result=protected_call(fn,arg);context=previous;return result;}
 catch(...){context=previous;throw;}
}
__declspec(noinline) int deep_write(unsigned depth,uint8_t* bytes) {
 volatile uint8_t pad[64];pad[0]=uint8_t(depth);
 if(depth)return deep_write(depth-1,bytes)+pad[0];
 bytes[0]^=3;return bytes[0]+pad[0];
}
struct Depth {unsigned value;uint8_t* bytes;};
int do_deep(void* arg){auto& d=*static_cast<Depth*>(arg);return deep_write(d.value,d.bytes);}
int write_pages(void* arg) {
 auto& ctx=*static_cast<ProbeContext*>(arg);
 ctx.base[0]=19;ctx.base[ctx.page+7]=23;ctx.base[ctx.size-1]=41;
 return 37;
}
}
void structured_write_guard_probe() {
 SYSTEM_INFO info{};GetNativeSystemInfo(&info);
 ProbeContext ctx;ctx.page=info.dwPageSize;ctx.size=ctx.page*4;
 struct Allocation {void* p=nullptr;~Allocation(){if(p)VirtualFree(p,0,MEM_RELEASE);}} allocation;
 allocation.p=VirtualAllocFromApp(nullptr,ctx.size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!allocation.p)exception_report::raise("SEH probe allocation failed");
 ctx.base=static_cast<uint8_t*>(allocation.p);
 auto protect=[&] {ULONG old;if(!VirtualProtectFromApp(ctx.base,ctx.size,PAGE_READONLY,&old))exception_report::raise("SEH probe protection failed");};
 protect();if(run(ctx,write_pages,&ctx)!=37 || ctx.faults!=3 || ctx.base[0]!=19 || ctx.base[ctx.page+7]!=23 || ctx.base[ctx.size-1]!=41)
  exception_report::raise("SEH probe failed to resume guarded writes");
 protect();std::thread worker([&]{if(run(ctx,write_pages,&ctx)!=37)std::terminate();});worker.join();
 if(ctx.faults!=6)exception_report::raise("SEH worker did not catch page writes");
 bool propagated=false;
 try{run(ctx,+[](void*)->int{throw std::runtime_error("SEH probe intentional C++ exception");},nullptr);}catch(const std::runtime_error&){propagated=true;}
 if(!propagated || context)exception_report::raise("SEH guard swallowed C++ exception or leaked thread context");
 for(unsigned depth:{0u,128u,512u}) {
  double total=0;
  for(int n=0;n<32;++n) {
   protect();Depth arg{depth,ctx.base};auto start=std::chrono::steady_clock::now();run(ctx,do_deep,&arg);
   total+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
  }
  LOG("[UWP structured write guard] depth %u: %.2f us/fault including stack calls; isolated diagnostic, NOT game FPS",depth,total/32);
 }
 LOG("[UWP structured write guard] PASS resumed protected writes, worker thread, C++ exception propagation, bounded range; no global VEH; guest memory untouched");
}
}
#endif
