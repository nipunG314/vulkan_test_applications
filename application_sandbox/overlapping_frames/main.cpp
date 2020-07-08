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

#include "application_sandbox/sample_application_framework/sample_application.h"
#include "support/entry/entry.h"
#include "vulkan_helpers/vulkan_application.h"

#include "mathfu/matrix.h"
#include "mathfu/vector.h"

using Mat44 = mathfu::Matrix<float, 4, 4>;
using Vector4 = mathfu::Vector<float, 4>;

uint32_t vert_shader[] =
#include "tri.vert.spv"
    ;

uint32_t frag_shader[] =
#include "tri.frag.spv"
    ;

vulkan::VkRenderPass buildRenderPass(vulkan::VulkanApplication& app) {
    // Build render pass
    VkAttachmentReference color_attachment = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    vulkan::VkRenderPass render_pass = app.CreateRenderPass(
        {{
           0,                                        // flags
           app.swapchain().format(),                 // format
           VK_SAMPLE_COUNT_1_BIT,                    // samples
           VK_ATTACHMENT_LOAD_OP_CLEAR,              // loadOp
           VK_ATTACHMENT_STORE_OP_STORE,             // storeOp
           VK_ATTACHMENT_LOAD_OP_DONT_CARE,          // stenilLoadOp
           VK_ATTACHMENT_STORE_OP_DONT_CARE,         // stenilStoreOp
           VK_IMAGE_LAYOUT_UNDEFINED,                // initialLayout
           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR           // finalLayout
        }},                                          // AttachmentDescriptions
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
        }},                                  // SubpassDescriptions
        {}                                   // SubpassDependencies
    );

    return render_pass;
}

vulkan::VulkanGraphicsPipeline buildGraphicsPipeline(vulkan::VulkanApplication& app, vulkan::VkRenderPass* render_pass) {
    // Build Graphics Pipeline
    vulkan::PipelineLayout pipeline_layout(app.CreatePipelineLayout({{}}));
    vulkan::VulkanGraphicsPipeline pipeline = app.CreateGraphicsPipeline(&pipeline_layout, render_pass, 0);

    pipeline.AddShader(VK_SHADER_STAGE_VERTEX_BIT, "main", vert_shader);
    pipeline.AddShader(VK_SHADER_STAGE_FRAGMENT_BIT, "main", frag_shader);

    pipeline.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipeline.SetScissor({
        {0, 0},                                             // offset
        {app.swapchain().width(), app.swapchain().height()} // extent
    });
    pipeline.SetViewport({
        0.0f,                                           // x
        0.0f,                                           // y
        static_cast<float>(app.swapchain().width()),    // width
        static_cast<float>(app.swapchain().height()),   // height
        0.0f,                                           // minDepth
        1.0f                                            // maxDepth
    });
    pipeline.SetSamples(VK_SAMPLE_COUNT_1_BIT);
    pipeline.AddAttachment();
    pipeline.Commit();

    return pipeline;
}

int main_entry(const entry::EntryData* data) {
    data->logger()->LogInfo("Application Startup");

    vulkan::VulkanApplication app(data->allocator(), data->logger(), data);
    vulkan::VkDevice& device = app.device();

    auto render_pass = buildRenderPass(app);
    auto pipeline = buildGraphicsPipeline(app, &render_pass);

    data->logger()->LogInfo("Application Shutdown");
    return 0;
}
