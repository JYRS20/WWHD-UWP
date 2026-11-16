// SPDX-License-Identifier: MPL-2.0
#include "../src/gfx/vulkan/driver_trace_hook.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <array>
#include <stdexcept>
using namespace gfxvk::drivertrace;
static void check(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct FakeClock {inline static uint64_t time=0;static uint64_t now(){return time;}};
struct Always {explicit Always(uint32_t=1){} bool take(){return true;}};
struct TestStore {static State<FakeClock,Always>& state(){static State<FakeClock,Always> value;return value;}};
using Signature=int(VKAPI_PTR*)(int,const void*,void*);
inline const void* seenInput=nullptr;inline void* seenOutput=nullptr;inline int seenValue=0;
static VKAPI_ATTR int VKAPI_CALL original(int n,const void* input,void* output) {
 seenInput=input;seenOutput=output;seenValue=n;FakeClock::time+=7;return -37;
}
static VKAPI_ATTR int VKAPI_CALL replacement(int,const void*,void*) {FakeClock::time+=11;return 91;}
static VKAPI_ATTR int VKAPI_CALL throws(int,const void*,void*) {FakeClock::time+=5;throw std::runtime_error("fixture");}
static VKAPI_ATTR int VKAPI_CALL thread_function(int n,const void*,void*) {return n+1;}
int main() {
 State<FakeClock,Always> trace;
 FakeClock::time=10;auto outer=trace.begin(Kind::vkCmdDraw);
 FakeClock::time=15;auto inner=trace.begin(Kind::vkCmdBindPipeline);
 FakeClock::time=25;trace.end(inner);FakeClock::time=30;trace.end(outer);
 check(trace.cells[size_t(Kind::vkCmdDraw)].counts.ns==20,"outer wall includes nested work once");
 check(trace.cells[size_t(Kind::vkCmdBindPipeline)].counts.calls==0 && trace.nested==1,"nested wrapper must not double count");
 check(trace.depth()==0,"balanced nesting");
 trace.reset_window();check(trace.cells[0].counts.calls==0 && trace.nested==0,"report resets window");
 State<FakeClock> sampled;
 for(int i=0;i<256;++i){auto t=sampled.begin(Kind::vkCmdDraw);++FakeClock::time;sampled.end(t);}
 const auto& count=sampled.cells[size_t(Kind::vkCmdDraw)].counts;
 check(count.calls==256 && count.samples==4 && count.ns==4 && count.estimated_ns()==256,"sampled scaling and exact call count");
 State<FakeClock> a,b;
 for(int i=0;i<11;++i){a.end(a.begin(Kind::vkCmdDraw));b.end(b.begin(Kind::vkCmdDraw));}
 a.reset_window();
 for(int i=0;i<200;++i){auto x=a.begin(Kind::vkCmdDraw),y=b.begin(Kind::vkCmdDraw);check(x.sampled==y.sampled,"sampling phase must survive report reset");a.end(x);b.end(y);}
 using Trampoline=Hook<Kind::vkCmdDraw,Signature,TestStore>;
 Signature target=original;Trampoline::install(target);Trampoline::install(target);
 int input=42,output=19;check(target(123,&input,&output)==-37,"negative result unchanged");
 check(seenInput==&input && seenOutput==&output && seenValue==123 && output==19,"pointers and values unchanged; no writes injected");
 check(TestStore::state().cells[size_t(Kind::vkCmdDraw)].counts.ns==7,"actual trampoline measures original call once");
 Signature absent=nullptr;Trampoline::install(absent);check(absent==nullptr,"absent optional entry point remains null");
 target=replacement;Trampoline::install(target);check(target(0,nullptr,nullptr)==91,"device reload refreshes original function");
 target=throws;Trampoline::install(target);try{target(0,nullptr,nullptr);check(false,"exception expected");}catch(const std::runtime_error&){}
 check(TestStore::state().depth()==0,"exception leaves nesting balanced");
 using ThreadHook=Hook<Kind::vkCmdDispatch,Signature>;
 Signature parallel=thread_function;ThreadHook::install(parallel);
 std::array<uint64_t,2> counts{};std::array<bool,2> results{true,true};
 std::array<std::thread,2> workers;
 for(size_t i=0;i<2;++i)workers[i]=std::thread([&,i]{Store::state().reset_window();for(int n=0;n<int(100+i*50);++n)results[i]&=parallel(n,nullptr,nullptr)==n+1;counts[i]=Store::state().cells[size_t(Kind::vkCmdDispatch)].counts.calls;});
 for(auto& worker:workers)worker.join();
 check(results[0] && results[1] && counts[0]==100 && counts[1]==150 && Store::state().cells[size_t(Kind::vkCmdDispatch)].counts.calls==0,"thread-local isolation and returned values");
 std::puts("driver_trace_test: PASS (nesting, sampling, ABI forwarding, null/reload, exceptions, thread isolation)");
}
