// SPDX-License-Identifier: MPL-2.0
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <string>
#include "dozen_draw_probe.h"
#ifdef WWHD_DOZEN_UWP
#include "present_probe.h"
#endif
#include "../src/gfx/vulkan/vertex_format_policy.h"
#include <vector>
#include <stdexcept>
#include <cstring>
static void check(VkResult r,const char* name){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(name)+" VkResult="+std::to_string(int(r)));}
#ifdef WWHD_DOZEN_UWP
int dozen_packaged_probe(IUnknown* window,uint32_t width,uint32_t height,DozenPump pump,void* context,int adapterIndex){try{
 HMODULE dll=LoadPackagedLibrary(L"vulkan_dzn.dll",0);
 if(!dll)throw std::runtime_error("LoadPackagedLibrary error="+std::to_string(GetLastError()));
#else
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("Pass pinned Dozen DLL path");
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
 if(!dll)throw std::runtime_error("LoadLibraryExW error="+std::to_string(GetLastError()));
#endif
 auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(dll,"vk_icdGetInstanceProcAddr"));
 if(!get)throw std::runtime_error("Missing ICD instance entry point");
 auto create=reinterpret_cast<PFN_vkCreateInstance>(get(VK_NULL_HANDLE,"vkCreateInstance"));
 if(!create)throw std::runtime_error("Missing vkCreateInstance");
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="WWHD Dozen headless capability probe";app.apiVersion=VK_API_VERSION_1_2;
 VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;VkInstance instance{};
#ifdef WWHD_DOZEN_UWP
 const char* instanceExtensions[]{VK_KHR_SURFACE_EXTENSION_NAME,"VK_KHR_win32_surface"};
 ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=instanceExtensions;
#endif
 check(create(&ci,nullptr,&instance),"vkCreateInstance");
 auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance,"vkEnumeratePhysicalDevices"));
 uint32_t count=0;check(enumerate(instance,&count,nullptr),"vkEnumeratePhysicalDevices count");std::cout<<"Physical devices="<<count<<'\n';
 std::vector<VkPhysicalDevice> devices(count);check(enumerate(instance,&count,devices.data()),"vkEnumeratePhysicalDevices");
 unsigned passed=0;
 for(size_t deviceIndex=0;deviceIndex<devices.size();++deviceIndex){
#ifdef WWHD_DOZEN_UWP
  if(adapterIndex>=0&&deviceIndex!=size_t(adapterIndex))continue;
#endif
  auto physical=devices[deviceIndex];
  auto props=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance,"vkGetPhysicalDeviceProperties"));
  VkPhysicalDeviceProperties properties{};props(physical,&properties);
  std::cout<<"Device="<<properties.deviceName<<" Vulkan="<<VK_VERSION_MAJOR(properties.apiVersion)<<'.'<<VK_VERSION_MINOR(properties.apiVersion)<<'\n';
  auto extensions=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(get(instance,"vkEnumerateDeviceExtensionProperties"));
  uint32_t n=0;check(extensions(physical,nullptr,&n,nullptr),"extension count");std::vector<VkExtensionProperties> list(n);check(extensions(physical,nullptr,&n,list.data()),"extensions");
  bool dynamicExt=false;for(auto& e:list)if(std::strcmp(e.extensionName,VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)==0)dynamicExt=true;
  auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(get(instance,"vkGetPhysicalDeviceFeatures2"));
  VkPhysicalDeviceDynamicRenderingFeatures dynamic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
  VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f.pNext=&dynamic;features(physical,&f);
  std::cout<<"Dynamic rendering: extension="<<dynamicExt<<" feature="<<dynamic.dynamicRendering
   <<" geometryShader="<<f.features.geometryShader<<" independentBlend="<<f.features.independentBlend<<" logicOp="<<f.features.logicOp<<'\n';
  // Report and enable the same optional core feature set selected by WWHD backend.cpp.
  VkPhysicalDeviceFeatures enabled{};
#define COPY_FEATURE(n) enabled.n=f.features.n;std::cout<<"WWHD feature " #n "="<<enabled.n<<'\n';
  COPY_FEATURE(textureCompressionBC);COPY_FEATURE(samplerAnisotropy);COPY_FEATURE(independentBlend);
  COPY_FEATURE(depthClamp);COPY_FEATURE(depthBiasClamp);COPY_FEATURE(shaderClipDistance);
  COPY_FEATURE(shaderCullDistance);COPY_FEATURE(fillModeNonSolid);COPY_FEATURE(largePoints);
  COPY_FEATURE(dualSrcBlend);COPY_FEATURE(logicOp);
#undef COPY_FEATURE
  bool swapchain=false;for(auto& e:list)if(std::strcmp(e.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME)==0)swapchain=true;
  std::cout<<"Swapchain advertised="<<swapchain<<" (capability query)\n";
  auto formatProps=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(get(instance,"vkGetPhysicalDeviceFormatProperties"));
  const auto vertexPolicy=gfxvk::vk::vertex_format_policy([&](VkFormat format){VkFormatProperties fp{};formatProps(physical,format,&fp);return bool(fp.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);},false);
  std::cout<<"WWHD vertex-format fallback mask="<<vertexPolicy.fallbackMask<<'\n';
  VkPhysicalDeviceVulkan12Features available12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 query12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};query12.pNext=&available12;features(physical,&query12);
  VkPhysicalDeviceVulkan12Features enabled12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  enabled12.samplerMirrorClampToEdge=available12.samplerMirrorClampToEdge;dynamic.pNext=&enabled12;
  std::cout<<"WWHD samplerMirrorClampToEdge="<<enabled12.samplerMirrorClampToEdge<<'\n';
  if(!dynamicExt||!dynamic.dynamicRendering)continue;
  auto queueProps=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
  uint32_t nq=0;queueProps(physical,&nq,nullptr);std::vector<VkQueueFamilyProperties> queues(nq);queueProps(physical,&nq,queues.data());
  uint32_t family=0;while(family<nq && !(queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
  if(family==nq)continue;float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
  const char* extension=VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&q;di.enabledExtensionCount=1;di.ppEnabledExtensionNames=&extension;di.pNext=&dynamic;di.pEnabledFeatures=&enabled;
#ifdef WWHD_DOZEN_UWP
  const char* deviceExtensions[]{VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  di.enabledExtensionCount=2;di.ppEnabledExtensionNames=deviceExtensions;
#endif
  auto createDevice=reinterpret_cast<PFN_vkCreateDevice>(get(instance,"vkCreateDevice"));VkDevice device{};check(createDevice(physical,&di,nullptr,&device),"vkCreateDevice");
  auto getDevice=reinterpret_cast<PFN_vkGetDeviceProcAddr>(get(instance,"vkGetDeviceProcAddr"));
  dozen_draw_probe(get,instance,physical,device,family);
#ifdef WWHD_DOZEN_UWP
  dozen_present_probe(dll,get,instance,physical,device,family,window,width,height,pump,context);
#endif
  auto idle=reinterpret_cast<PFN_vkDeviceWaitIdle>(getDevice(device,"vkDeviceWaitIdle"));check(idle(device),"vkDeviceWaitIdle");
  auto destroy=reinterpret_cast<PFN_vkDestroyDevice>(getDevice(device,"vkDestroyDevice"));destroy(device,nullptr);
  ++passed;
  std::cout<<"Logical device creation, authored offscreen draw and idle PASS\n";
 }
 auto destroy=reinterpret_cast<PFN_vkDestroyInstance>(get(instance,"vkDestroyInstance"));destroy(instance,nullptr);FreeLibrary(dll);
 return passed?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
