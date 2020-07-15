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

#include "support/containers/vector.h"
#include "support/entry/entry.h"
#include "vulkan_core.h"
#include "vulkan_helpers/vulkan_application.h"
#include "vulkan_helpers/helper_functions.h"

#include "mathfu/matrix.h"
#include "mathfu/vector.h"
#include "vulkan_wrapper/sub_objects.h"

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
        {{
           VK_SUBPASS_EXTERNAL,                             // srcSubpass
           0,                                               // dstSubpass
           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,   // srcStageMask
           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,   // dstStageMsdk
           0,                                               // srcAccessMask
           VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,            // dstAccessMask
           0                                                // dependencyFlags
        }}
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

void buildFramebuffers(vulkan::VulkanApplication& app, vulkan::VkRenderPass& render_pass, VkFramebuffer* framebuffers, const entry::EntryData* data) {
    for(int index=0; index < app.swapchain_images().size(); index++) {
        VkImage& swapchain_image = app.swapchain_images()[index];

        VkImageViewCreateInfo image_view_create_info {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,  // sType
            nullptr,                                   // pNext
            0,                                         // flags
            swapchain_image,                           // image
            VK_IMAGE_VIEW_TYPE_2D,                     // viewType
            app.swapchain().format(),                  // format
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
        LOG_ASSERT(==, data->logger(), app.device()->vkCreateImageView(app.device(), &image_view_create_info, nullptr, &raw_image_view), VK_SUCCESS);

        VkFramebufferCreateInfo framebuffer_create_info {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,  // sType
            nullptr,                                    // pNext
            0,                                          // flags
            render_pass,                                // renderPass
            1,                                          // attachmentCount
            &raw_image_view,                            // attachments
            app.swapchain().width(),                    // width
            app.swapchain().height(),                   // height
            1                                           // layers
        };

        LOG_ASSERT(==, data->logger(), app.device()->vkCreateFramebuffer(app.device(), &framebuffer_create_info, nullptr, &framebuffers[index]), VK_SUCCESS);
    }
}

int main_entry(const entry::EntryData* data) {
    data->logger()->LogInfo("Application Startup");

    vulkan::VulkanApplication app(data->allocator(), data->logger(), data);
    vulkan::VkDevice& device = app.device();

    auto render_pass = buildRenderPass(app);
    auto pipeline = buildGraphicsPipeline(app, &render_pass);

    VkFramebuffer framebuffers[(const int)app.swapchain_images().size()];
    buildFramebuffers(app, render_pass, framebuffers, data);

    VkClearValue clear_color = {0.0f, 0.0f, 0.0f, 1.0f};

    uint32_t image_index;

    vulkan::VkSemaphore image_acquired = CreateSemaphore(&app.device());
    vulkan::VkSemaphore render_finished = CreateSemaphore(&app.device());
    while(!data->WindowClosing()) {
        app.device()->vkAcquireNextImageKHR(
            app.device(),
            app.swapchain().get_raw_object(),
            UINT64_MAX,
            image_acquired.get_raw_object(),
            static_cast<VkFence>(VK_NULL_HANDLE),
            &image_index
        );

        VkRenderPassBeginInfo pass_begin {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            nullptr,
            render_pass,
            framebuffers[image_index],
            {{0, 0}, {app.swapchain().width(), app.swapchain().height()}},
            1,
            &clear_color
        };

        auto cmd_buf = app.GetCommandBuffer();
        app.BeginCommandBuffer(&cmd_buf);
        vulkan::RecordImageLayoutTransition(
            app.swapchain_images()[image_index],
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            VK_IMAGE_LAYOUT_UNDEFINED,
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            &cmd_buf
        );
        cmd_buf->vkCmdBeginRenderPass(cmd_buf, &pass_begin, VK_SUBPASS_CONTENTS_INLINE);
        cmd_buf->vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        cmd_buf->vkCmdDraw(cmd_buf, 3, 1, 0, 0);
        cmd_buf->vkCmdEndRenderPass(cmd_buf);
        LOG_ASSERT(==, data->logger(), VK_SUCCESS,
            app.EndAndSubmitCommandBuffer(
                &cmd_buf,
                &app.render_queue(),
                {image_acquired.get_raw_object()},
                {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
                {render_finished.get_raw_object()},
                static_cast<VkFence>(VK_NULL_HANDLE)
            )
        );

        VkSemaphore signal_semaphores[] = {render_finished.get_raw_object()};
        VkSwapchainKHR swapchains[] = {app.swapchain().get_raw_object()};

        VkPresentInfoKHR present_info {
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            nullptr,
            1,
            signal_semaphores,
            1,
            swapchains,
            &image_index,
            nullptr
        };

        app.present_queue()->vkQueuePresentKHR(app.present_queue(), &present_info);
    }

    data->logger()->LogInfo("Application Shutdown");
    return 0;
}
