// SPDX-License-Identifier: MPL-2.0
#pragma once
#ifdef WWHD_UWP
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
namespace gfxvk::uwp_driver {
PFN_vkGetInstanceProcAddr open();
void log_memory(uint64_t frame, size_t surfaces) noexcept;
VkResult create_swapchain(PFN_vkCreateSwapchainKHR,VkDevice,const VkSwapchainCreateInfoKHR*,const VkAllocationCallbacks*,VkSwapchainKHR*);
bool create_surface(SDL_Window*,VkInstance,const VkAllocationCallbacks*,VkSurfaceKHR*);
}
#endif
