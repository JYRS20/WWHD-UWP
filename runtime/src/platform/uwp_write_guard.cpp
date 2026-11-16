// SPDX-License-Identifier: MPL-2.0
#include "uwp_write_guard.h"
#ifdef WWHD_UWP
#include "../write_watch.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
namespace uwp_write_guard {
namespace {
thread_local unsigned depth=0;
int filter(EXCEPTION_POINTERS* exception) noexcept {
 auto* record=exception->ExceptionRecord;
 if(!depth || record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record->NumberParameters<2 || record->ExceptionInformation[0]!=1)
  return EXCEPTION_CONTINUE_SEARCH;
 return wwatch::handle_guarded_write_fault(record->ExceptionInformation[1])?EXCEPTION_CONTINUE_EXECUTION:EXCEPTION_CONTINUE_SEARCH;
}
// Keep C++ objects/destructors out of the SEH frame. The filter never returns
// EXECUTE_HANDLER: unrelated faults and C++ exceptions are never swallowed.
__declspec(noinline) int protected_call(int(*entry)(void*),void* context) {
 __try{return entry(context);}
 __except(filter(GetExceptionInformation())){return -1;}
}
}
bool enabled() {
 static const bool requested=[] {auto* e=std::getenv("WWHD_UWP_WRITE_GUARD");return e&&!std::strcmp(e,"optimized");}();
 return requested;
}
bool active(){return depth!=0;}
int invoke(int(*entry)(void*),void* context) {
 if(!enabled())return entry(context);
 ++depth;
 try{int result=protected_call(entry,context);--depth;return result;}
 catch(...){--depth;throw;}
}
}
#endif
