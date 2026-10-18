// SPDX-License-Identifier: MPL-2.0
// Authored headless Vulkan draw/readback through an explicitly supplied experimental Dozen driver.
#include "dozen_draw_probe.h"
#ifndef WWHD_DOZEN_FIXED_SHADERS
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#else
#include "dozen_fixed_shaders.h"
#endif
#include <vector>
#include <stdexcept>
#include <string>
#include <cmath>
#include <iostream>
namespace {
void check(VkResult r,const char* name){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(name)+" result="+std::to_string(int(r)));}
#ifndef WWHD_DOZEN_FIXED_SHADERS
std::vector<uint32_t> compile(const char* source,EShLanguage language){
 glslang::TShader shader(language);shader.setStrings(&source,1);
 shader.setEnvInput(glslang::EShSourceGlsl,language,glslang::EShClientVulkan,100);
 shader.setEnvClient(glslang::EShClientVulkan,glslang::EShTargetVulkan_1_1);
 shader.setEnvTarget(glslang::EShTargetSpv,glslang::EShTargetSpv_1_3);
 const auto messages=EShMessages(EShMsgSpvRules|EShMsgVulkanRules);
 if(!shader.parse(GetDefaultResources(),450,false,messages))throw std::runtime_error(shader.getInfoLog());
 glslang::TProgram program;program.addShader(&shader);if(!program.link(messages))throw std::runtime_error(program.getInfoLog());
 std::vector<uint32_t> words;glslang::SpvOptions options;options.disableOptimizer=true;
 glslang::GlslangToSpv(*program.getIntermediate(language),words,&options);return words;
}
#endif
}
void dozen_draw_probe(PFN_vkGetInstanceProcAddr get,VkInstance instance,VkPhysicalDevice physical,VkDevice device,uint32_t family){
 auto getDevice=reinterpret_cast<PFN_vkGetDeviceProcAddr>(get(instance,"vkGetDeviceProcAddr"));
#define LOAD(n) auto n=reinterpret_cast<PFN_##n>(getDevice(device,#n));if(!n)throw std::runtime_error("Missing " #n)
 LOAD(vkCreateImage);LOAD(vkGetImageMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkBindImageMemory);LOAD(vkCreateImageView);
 LOAD(vkCreateBuffer);LOAD(vkGetBufferMemoryRequirements);LOAD(vkBindBufferMemory);LOAD(vkCreateShaderModule);LOAD(vkCreatePipelineLayout);
 LOAD(vkCreateGraphicsPipelines);LOAD(vkCreateCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkBeginCommandBuffer);LOAD(vkCmdPipelineBarrier);
 LOAD(vkCmdBeginRenderingKHR);LOAD(vkCmdEndRenderingKHR);LOAD(vkCmdBindPipeline);LOAD(vkCmdDraw);LOAD(vkCmdCopyImageToBuffer);LOAD(vkEndCommandBuffer);
 LOAD(vkGetDeviceQueue);LOAD(vkCreateFence);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);LOAD(vkMapMemory);LOAD(vkUnmapMemory);
 LOAD(vkDestroyFence);LOAD(vkDestroyCommandPool);LOAD(vkDestroyPipeline);LOAD(vkDestroyPipelineLayout);LOAD(vkDestroyShaderModule);
 LOAD(vkDestroyImageView);LOAD(vkDestroyImage);LOAD(vkDestroyBuffer);LOAD(vkFreeMemory);
#undef LOAD
 auto memoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(get(instance,"vkGetPhysicalDeviceMemoryProperties"));
 VkPhysicalDeviceMemoryProperties memory{};memoryProperties(physical,&memory);
 auto allocate=[&](VkMemoryRequirements requirements,VkMemoryPropertyFlags flags){
  uint32_t type=0;while(type<memory.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type)) || (memory.memoryTypes[type].propertyFlags&flags)!=flags))++type;
  if(type==memory.memoryTypeCount)throw std::runtime_error("No suitable Vulkan memory type");
  VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};info.allocationSize=requirements.size;info.memoryTypeIndex=type;
  VkDeviceMemory result{};check(vkAllocateMemory(device,&info,nullptr,&result),"allocate");return result;
 };
 constexpr VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;constexpr uint32_t width=32,height=32;
 VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};imageInfo.imageType=VK_IMAGE_TYPE_2D;imageInfo.format=format;
 imageInfo.extent={width,height,1};imageInfo.mipLevels=imageInfo.arrayLayers=1;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;
 imageInfo.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
 VkImage image{};check(vkCreateImage(device,&imageInfo,nullptr,&image),"create image");VkMemoryRequirements requirements{};
 vkGetImageMemoryRequirements(device,image,&requirements);auto imageMemory=allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);check(vkBindImageMemory(device,image,imageMemory,0),"bind image");
 VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};viewInfo.image=image;viewInfo.viewType=VK_IMAGE_VIEW_TYPE_2D;viewInfo.format=format;
 viewInfo.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkImageView view{};check(vkCreateImageView(device,&viewInfo,nullptr,&view),"create image view");
 VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bufferInfo.size=width*height*4;bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
 VkBuffer buffer{};check(vkCreateBuffer(device,&bufferInfo,nullptr,&buffer),"create buffer");vkGetBufferMemoryRequirements(device,buffer,&requirements);
 auto bufferMemory=allocate(requirements,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(vkBindBufferMemory(device,buffer,bufferMemory,0),"bind buffer");
#ifndef WWHD_DOZEN_FIXED_SHADERS
 glslang::InitializeProcess();
 auto vertex=compile("#version 450\nvoid main(){vec2 p[3]=vec2[](vec2(-1,-1),vec2(-1,3),vec2(3,-1));gl_Position=vec4(p[gl_VertexIndex],0.5,1);}",EShLangVertex);
 auto pixel=compile("#version 450\nlayout(location=0)out vec4 c;void main(){c=vec4(0.25,0.5,0.75,1);}",EShLangFragment);
 glslang::FinalizeProcess();
#else
 const std::vector<uint32_t> vertex(std::begin(dozen_vertex_spv),std::end(dozen_vertex_spv));
 const std::vector<uint32_t> pixel(std::begin(dozen_pixel_spv),std::end(dozen_pixel_spv));
#endif
 auto module=[&](const auto& words){VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=words.size()*4;info.pCode=words.data();
  VkShaderModule result{};check(vkCreateShaderModule(device,&info,nullptr,&result),"shader module");return result;};
 VkShaderModule vs=module(vertex),ps=module(pixel);
 VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}
 stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=ps;
 VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VkPipelineLayout layout{};check(vkCreatePipelineLayout(device,&layoutInfo,nullptr,&layout),"pipeline layout");
 VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
 VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 VkViewport viewport{0,0,float(width),float(height),0,1};VkRect2D scissor{{0,0},{width,height}};
 VkPipelineViewportStateCreateInfo vport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vport.viewportCount=vport.scissorCount=1;vport.pViewports=&viewport;vport.pScissors=&scissor;
 VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
 VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
 VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
 VkPipelineColorBlendStateCreateInfo color{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};color.attachmentCount=1;color.pAttachments=&blend;
 VkPipelineRenderingCreateInfoKHR rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&format;
 VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pipelineInfo.pNext=&rendering;pipelineInfo.stageCount=2;pipelineInfo.pStages=stages;
 pipelineInfo.pVertexInputState=&input;pipelineInfo.pInputAssemblyState=&assembly;pipelineInfo.pViewportState=&vport;pipelineInfo.pRasterizationState=&raster;
 pipelineInfo.pMultisampleState=&ms;pipelineInfo.pColorBlendState=&color;pipelineInfo.layout=layout;VkPipeline pipeline{};
 check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&pipeline),"graphics pipeline");
 VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family;VkCommandPool pool{};check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool),"command pool");
 VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=pool;alloc.commandBufferCount=1;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
 VkCommandBuffer cmd{};check(vkAllocateCommandBuffers(device,&alloc,&cmd),"allocate commands");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(cmd,&begin),"begin commands");
 VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image;barrier.subresourceRange=viewInfo.subresourceRange;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
 VkRenderingAttachmentInfoKHR attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};attachment.imageView=view;attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
 attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
 VkRenderingInfoKHR render{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};render.renderArea=scissor;render.layerCount=1;render.colorAttachmentCount=1;render.pColorAttachments=&attachment;
 vkCmdBeginRenderingKHR(cmd,&render);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderingKHR(cmd);
 barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
 VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
 VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
 host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer;host.size=VK_WHOLE_SIZE;
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);check(vkEndCommandBuffer(cmd),"end commands");
 VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};check(vkCreateFence(device,&fenceInfo,nullptr,&fence),"fence");
 VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
 check(vkQueueSubmit(queue,1,&submit,fence),"submit");check(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull),"draw fence");
 void* mapped=nullptr;check(vkMapMemory(device,bufferMemory,0,VK_WHOLE_SIZE,0,&mapped),"map readback");const auto* bytes=static_cast<const uint8_t*>(mapped);
 bool good=true;const int expected[]{64,128,191,255};for(uint32_t i=0;i<width*height;++i)for(int c=0;c<4;++c)good&=std::abs(int(bytes[i*4+c])-expected[c])<=1;
 vkUnmapMemory(device,bufferMemory);if(!good)throw std::runtime_error("Dozen triangle pixels differ from authored colour");
 vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);
 vkDestroyShaderModule(device,vs,nullptr);vkDestroyShaderModule(device,ps,nullptr);vkDestroyImageView(device,view,nullptr);vkDestroyImage(device,image,nullptr);
 vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,imageMemory,nullptr);vkFreeMemory(device,bufferMemory,nullptr);
 std::cout<<"Dozen Vulkan -> D3D12 dynamic-rendering triangle/readback PASS, 1024 pixels\n";
}
