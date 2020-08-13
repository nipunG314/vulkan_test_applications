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

#include "support/entry/entry.h"
#include "vulkan_helpers/helper_functions.h"
#include "vulkan_helpers/vulkan_application.h"
#include "vulkan_helpers/vulkan_model.h"

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

  // Wait for Screen Model to be initialized
  app.device()->vkWaitForFences(app.device(), 1, &init_fence.get_raw_object(),
                                VK_TRUE, UINT64_MAX);

  data->logger()->LogInfo("Application Shutdown");

  return 0;
}
