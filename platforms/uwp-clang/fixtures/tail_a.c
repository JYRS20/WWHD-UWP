typedef unsigned long long uintptr_t;
typedef struct State {unsigned remaining,value;uintptr_t stack_low,stack_high;} State;
_Static_assert(sizeof(State)==24,"x64 state ABI");
extern unsigned wwhd_host_mix(unsigned);
extern unsigned wwhd_fallback_adjust(unsigned);
extern __declspec(dllimport) volatile unsigned wwhd_host_counter;
extern __declspec(dllimport) const unsigned wwhd_host_salt;
extern __declspec(dllimport) unsigned char* wwhd_host_flags;
extern void tail_b(State*);
__attribute__((noinline)) void tail_a(State* s) {
 uintptr_t sp=(uintptr_t)__builtin_frame_address(0);
 if(sp<s->stack_low)s->stack_low=sp;
 if(sp>s->stack_high)s->stack_high=sp;
 if(s->stack_high-s->stack_low>256)return; /* Fail boundedly if tail calls regress. */
 if(!s->remaining)return;
 --s->remaining;
 ++wwhd_host_counter;
 s->value=wwhd_host_mix(wwhd_fallback_adjust(s->value ^ wwhd_host_salt ^ wwhd_host_flags[0]));
 __attribute__((musttail)) return tail_b(s);
}
