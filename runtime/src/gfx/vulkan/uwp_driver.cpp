// SPDX-License-Identifier: MPL-2.0
#include "uwp_driver.h"
#ifdef WWHD_UWP
#include <windows.h>
#include <stdexcept>
#include <string>
#include <winrt/Windows.System.h>
#include "runtime.h"
namespace gfxvk::uwp_driver {
namespace {
HMODULE driver=nullptr;
using CreateSurface=VkResult(VKAPI_PTR*)(VkInstance,IUnknown*,const VkAllocationCallbacks*,VkSurfaceKHR*);
CreateSurface createCoreWindowSurface=nullptr;
}
void log_memory(uint64_t frame, size_t surfaces) noexcept {
 try {
  using winrt::Windows::System::MemoryManager;
  const auto usage=MemoryManager::AppMemoryUsage(), limit=MemoryManager::AppMemoryUsageLimit();
  LOG("[uwp memory] frame %llu: app %.1f / %.1f MiB; cached surfaces %zu",
      (unsigned long long)frame, usage/1048576.0, limit/1048576.0, surfaces);
 } catch (...) { LOG("[uwp memory] app budget query unavailable"); }
}
PFN_vkGetInstanceProcAddr open(){
 if(!driver)driver=LoadPackagedLibrary(L"vulkan_dzn.dll",0);
 if(!driver)throw std::runtime_error("Packaged Dozen driver failed to load: "+std::to_string(GetLastError()));
 auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(driver,"vk_icdGetInstanceProcAddr"));
 createCoreWindowSurface=reinterpret_cast<CreateSurface>(GetProcAddress(driver,"wwhd_dzn_create_core_window_surface"));
 if(!get||!createCoreWindowSurface)throw std::runtime_error("Pinned Dozen driver lacks UWP entry points");
 // Keep the module loaded for the renderer's entire resource lifetime.
 return get;
}
VkResult create_swapchain(PFN_vkCreateSwapchainKHR function,VkDevice device,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks* allocator,VkSwapchainKHR* out){
 if(SDL_IsMainThread())return function(device,info,allocator,out);
 struct Request{PFN_vkCreateSwapchainKHR function;VkDevice device;const VkSwapchainCreateInfoKHR* info;const VkAllocationCallbacks* allocator;VkSwapchainKHR* out;VkResult result;};
 Request request{function,device,info,allocator,out,VK_ERROR_INITIALIZATION_FAILED};
 auto invoke=[](void* data){auto& r=*static_cast<Request*>(data);r.result=r.function(r.device,r.info,r.allocator,r.out);};
 if(!SDL_RunOnMainThread(invoke,&request,true))throw std::runtime_error(std::string("Cannot dispatch CoreWindow swapchain creation: ")+SDL_GetError());
 return request.result;
}
bool create_surface(SDL_Window* window,VkInstance instance,const VkAllocationCallbacks* allocator,VkSurfaceKHR* out){
 if(!window||!createCoreWindowSurface)return SDL_SetError("UWP renderer/window is not initialized");
 auto native=static_cast<IUnknown*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WINRT_WINDOW_POINTER,nullptr));
 if(!native)return SDL_SetError("SDL did not expose its CoreWindow");
 VkResult result=createCoreWindowSurface(instance,native,allocator,out);
 if(result!=VK_SUCCESS)return SDL_SetError("CoreWindow Vulkan surface failed: %d",int(result));
 return true;
}
}
#endif
