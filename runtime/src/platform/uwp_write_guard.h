// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <memory>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
namespace uwp_write_guard {
#ifdef WWHD_UWP
bool enabled();
bool active();
int invoke(int(*entry)(void*),void* context);
#else
inline bool enabled(){return false;}
inline bool active(){return false;}
#endif
// Guard native roots once, not every recompiled memory access. Unexpected
// exceptions keep their normal propagation; only armed guest write faults resume.
template<class F>void run(F&& fn) {
#ifdef WWHD_UWP
 using Function=std::remove_reference_t<F>;
 if constexpr(std::is_function_v<Function>) {
  auto function=&fn;run([function]{function();});
 } else {
  invoke(+[](void* p)->int {(*static_cast<Function*>(p))();return 0;},
      const_cast<void*>(static_cast<const void*>(std::addressof(fn))));
 }
#else
 std::forward<F>(fn)();
#endif
}
template<class F,class...Args>std::thread start_thread(F&& fn,Args&&...args) {
#ifdef WWHD_UWP
 auto body=[function=std::decay_t<F>(std::forward<F>(fn)),arguments=std::make_tuple(std::forward<Args>(args)...)]()mutable {
  std::apply(std::move(function),std::move(arguments));
 };
 return std::thread([body=std::move(body)]()mutable{run(body);});
#else
 return std::thread(std::forward<F>(fn),std::forward<Args>(args)...);
#endif
}
}
