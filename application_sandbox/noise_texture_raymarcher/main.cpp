// Copyright 2020 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "support/containers/unique_ptr.h"
#include "support/containers/vector.h"
#include "support/entry/entry.h"
#include "vulkan_helpers/helper_functions.h"
#include "vulkan_helpers/vulkan_application.h"
#include "vulkan_helpers/vulkan_model.h"
#include "vulkan_wrapper/command_buffer_wrapper.h"
#include "vulkan_wrapper/sub_objects.h"

namespace raymarcher {
uint32_t vert[] =
#include "raymarcher.vert.spv"
    ;

uint32_t frag[] =
#include "raymarcher.frag.spv"
    ;
};  // namespace raymarcher

namespace screen_model {
#include "fullscreen_quad.obj.h"
}
const auto& screen_data = screen_model::model;

struct FrameData {
  // Command Buffers
  containers::unique_ptr<vulkan::VkCommandBuffer> raymarcher_cmd_buf;

  // Semaphores
  containers::unique_ptr<vulkan::VkSemaphore> image_acquired;
  containers::unique_ptr<vulkan::VkSemaphore> raymarcher_render_finished;

  // Fences
  containers::unique_ptr<vulkan::VkFence> rendering_fence;
};

vulkan::VkRenderPass buildRenderPass(vulkan::VulkanApplication* app,
                                     VkImageLayout initial_layout,
                                     VkImageLayout final_layout) {
  // Build render pass
  VkAttachmentReference color_attachment = {
      0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  vulkan::VkRenderPass render_pass = app->CreateRenderPass(
      {{
          0,                                 // flags
          app->swapchain().format(),         // format
          VK_SAMPLE_COUNT_1_BIT,             // samples
          VK_ATTACHMENT_LOAD_OP_CLEAR,       // loadOp
          VK_ATTACHMENT_STORE_OP_STORE,      // storeOp
          VK_ATTACHMENT_LOAD_OP_DONT_CARE,   // stenilLoadOp
          VK_ATTACHMENT_STORE_OP_DONT_CARE,  // stenilStoreOp
          initial_layout,                    // initialLayout
          final_layout                       // finalLayout
      }},                                    // AttachmentDescriptions
      {{
          0,                                // flags
          VK_PIPELINE_BIND_POINT_GRAPHICS,  // pipelineBindPoint
          0,                                // inputAttachmentCount
          nullptr,                          // pInputAttachments
          1,                                // colorAttachmentCount
          &color_attachment,                // colorAttachment
          nullptr,                          // pResolveAttachments
          nullptr,                          // pDepthStencilAttachment
          0,                                // preserveAttachmentCount
          nullptr                           // pPreserveAttachments
      }},                                   // SubpassDescriptions
      {{
          VK_SUBPASS_EXTERNAL,                            // srcSubpass
          0,                                              // dstSubpass
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // dstStageMsdk
          0,                                              // srcAccessMask
          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,  // dstAccessMask
          0                                          // dependencyFlags
      }});

  return render_pass;
}

vulkan::VulkanGraphicsPipeline buildRaymarcherPipeline(
    vulkan::VulkanApplication* app, vulkan::VkRenderPass* render_pass,
    vulkan::VulkanModel* screen) {
  // Build Triangle Pipeline
  vulkan::PipelineLayout pipeline_layout(app->CreatePipelineLayout({{}}));
  vulkan::VulkanGraphicsPipeline pipeline =
      app->CreateGraphicsPipeline(&pipeline_layout, render_pass, 0);

  pipeline.AddShader(VK_SHADER_STAGE_VERTEX_BIT, "main", raymarcher::vert);
  pipeline.AddShader(VK_SHADER_STAGE_FRAGMENT_BIT, "main", raymarcher::frag);

  pipeline.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  pipeline.SetInputStreams(screen);
  pipeline.SetScissor({
      {0, 0},                                                // offset
      {app->swapchain().width(), app->swapchain().height()}  // extent
  });
  pipeline.SetViewport({
      0.0f,                                           // x
      0.0f,                                           // y
      static_cast<float>(app->swapchain().width()),   // width
      static_cast<float>(app->swapchain().height()),  // height
      0.0f,                                           // minDepth
      1.0f                                            // maxDepth
  });
  pipeline.SetSamples(VK_SAMPLE_COUNT_1_BIT);
  pipeline.AddAttachment();
  pipeline.Commit();

  return pipeline;
}

containers::vector<vulkan::VkImageView> buildSwapchainImageViews(
    vulkan::VulkanApplication* app, const entry::EntryData* data) {
  containers::vector<vulkan::VkImageView> image_views(data->allocator());
  image_views.reserve(app->swapchain_images().size());

  for (int index = 0; index < app->swapchain_images().size(); index++) {
    VkImage& swapchain_image = app->swapchain_images()[index];

    VkImageViewCreateInfo image_view_create_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,  // sType
        nullptr,                                   // pNext
        0,                                         // flags
        swapchain_image,                           // image
        VK_IMAGE_VIEW_TYPE_2D,                     // viewType
        app->swapchain().format(),                 // format
        {
            VK_COMPONENT_SWIZZLE_IDENTITY,  // components.r
            VK_COMPONENT_SWIZZLE_IDENTITY,  // components.g
            VK_COMPONENT_SWIZZLE_IDENTITY,  // components.b
            VK_COMPONENT_SWIZZLE_IDENTITY,  // components.a
        },
        {
            VK_IMAGE_ASPECT_COLOR_BIT,  // subresourceRange.aspectMask
            0,                          // subresourceRange.baseMipLevel
            1,                          // subresourceRange.levelCount
            0,                          // subresourceRange.baseArrayLayer
            1,                          // subresourceRange.layerCount
        },
    };

    VkImageView raw_image_view;
    LOG_ASSERT(
        ==, data->logger(),
        app->device()->vkCreateImageView(app->device(), &image_view_create_info,
                                         nullptr, &raw_image_view),
        VK_SUCCESS);
    image_views.push_back(
        vulkan::VkImageView(raw_image_view, nullptr, &app->device()));
  }

  return image_views;
}

containers::vector<vulkan::VkFramebuffer> buildFramebuffers(
    vulkan::VulkanApplication* app, const vulkan::VkRenderPass& render_pass,
    const containers::vector<vulkan::VkImageView>& image_views,
    const entry::EntryData* data) {
  containers::vector<vulkan::VkFramebuffer> framebuffers(data->allocator());
  framebuffers.reserve(app->swapchain_images().size());

  for (int index = 0; index < app->swapchain_images().size(); index++) {
    VkFramebufferCreateInfo framebuffer_create_info{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,  // sType
        nullptr,                                    // pNext
        0,                                          // flags
        render_pass,                                // renderPass
        1,                                          // attachmentCount
        &image_views[index].get_raw_object(),       // attachments
        app->swapchain().width(),                   // width
        app->swapchain().height(),                  // height
        1                                           // layers
    };

    VkFramebuffer raw_framebuffer;
    LOG_ASSERT(
        ==, data->logger(),
        app->device()->vkCreateFramebuffer(
            app->device(), &framebuffer_create_info, nullptr, &raw_framebuffer),
        VK_SUCCESS);

    framebuffers.push_back(
        vulkan::VkFramebuffer(raw_framebuffer, nullptr, &app->device()));
  }

  return framebuffers;
}

int main_entry(const entry::EntryData* data) {
  data->logger()->LogInfo("Application Startup");

  vulkan::VulkanApplication app(data->allocator(), data->logger(), data);
  vulkan::VulkanModel screen(data->allocator(), data->logger(), screen_data);

  // Initialize Screen Model
  auto init_cmd_buf = app.GetCommandBuffer();
  app.BeginCommandBuffer(&init_cmd_buf);

  screen.InitializeData(&app, &init_cmd_buf);
  vulkan::VkFence init_fence = CreateFence(&app.device());
  app.EndAndSubmitCommandBuffer(&init_cmd_buf, &app.render_queue(), {}, {}, {},
                                init_fence.get_raw_object());

  // Raymarching Render Pass
  auto raymarcher_render_pass = buildRenderPass(
      &app, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  auto raymarcher_pipeline =
      buildRaymarcherPipeline(&app, &raymarcher_render_pass, &screen);
  auto raymarcher_image_views = buildSwapchainImageViews(&app, data);
  auto raymarcher_framebuffers = buildFramebuffers(
      &app, raymarcher_render_pass, raymarcher_image_views, data);
  VkClearValue clear_color = {0.0f, 0.0f, 0.0f, 1.0f};

  // FrameData
  containers::vector<FrameData> frameData(data->allocator());
  frameData.resize(app.swapchain_images().size());

  for (int index = 0; index < app.swapchain_images().size(); index++) {
    frameData[index].raymarcher_cmd_buf =
        containers::make_unique<vulkan::VkCommandBuffer>(
            data->allocator(), app.GetCommandBuffer());
    frameData[index].image_acquired =
        containers::make_unique<vulkan::VkSemaphore>(
            data->allocator(), vulkan::CreateSemaphore(&app.device()));
    frameData[index].raymarcher_render_finished =
        containers::make_unique<vulkan::VkSemaphore>(
            data->allocator(), vulkan::CreateSemaphore(&app.device()));
    frameData[index].rendering_fence = containers::make_unique<vulkan::VkFence>(
        data->allocator(), vulkan::CreateFence(&app.device(), 1));
  }

  // Frame Counter
  uint32_t current_frame = 0;
  uint32_t image_index;

  // Wait for Screen Model to be initialized
  app.device()->vkWaitForFences(app.device(), 1, &init_fence.get_raw_object(),
                                VK_TRUE, UINT64_MAX);

  data->logger()->LogInfo("Application Shutdown");

  return 0;
}
