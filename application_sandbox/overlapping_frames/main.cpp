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

#include <memory>
#include <utility>

#include "support/containers/unique_ptr.h"
#include "support/containers/unordered_map.h"
#include "support/containers/vector.h"
#include "support/entry/entry.h"
#include "vulkan_core.h"
#include "vulkan_helpers/vulkan_application.h"
#include "vulkan_helpers/helper_functions.h"

#include "mathfu/matrix.h"
#include "mathfu/vector.h"
#include "vulkan_helpers/vulkan_model.h"
#include "vulkan_wrapper/command_buffer_wrapper.h"
#include "vulkan_wrapper/descriptor_set_wrapper.h"
#include "vulkan_wrapper/sub_objects.h"

using Mat44 = mathfu::Matrix<float, 4, 4>;
using Vector4 = mathfu::Vector<float, 4>;

struct CommandTracker {
    containers::unique_ptr<vulkan::VkCommandBuffer> command_buffer;
    containers::unique_ptr<vulkan::VkFence> rendering_fence;
    containers::unique_ptr<vulkan::DescriptorSet> descriptor_set;
};

uint32_t tri_vert_shader[] =
#include "tri.vert.spv"
    ;

uint32_t tri_frag_shader[] =
#include "tri.frag.spv"
    ;

uint32_t post_vert_shader[] =
#include "post.vert.spv"
    ;

uint32_t post_frag_shader[] =
#include "post.frag.spv"
    ;

namespace screen_model {
#include "fullscreen_quad.obj.h"
}
const auto& screen_data = screen_model::model;

vulkan::VkRenderPass buildRenderPass(
    vulkan::VulkanApplication& app,
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
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
           initial_layout,                           // initialLayout
           final_layout                              // finalLayout
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

vulkan::VulkanGraphicsPipeline buildTrianglePipeline(vulkan::VulkanApplication& app, vulkan::VkRenderPass* render_pass) {
    // Build Triangle Pipeline
    vulkan::PipelineLayout pipeline_layout(app.CreatePipelineLayout({{}}));
    vulkan::VulkanGraphicsPipeline pipeline = app.CreateGraphicsPipeline(&pipeline_layout, render_pass, 0);

    pipeline.AddShader(VK_SHADER_STAGE_VERTEX_BIT, "main", tri_vert_shader);
    pipeline.AddShader(VK_SHADER_STAGE_FRAGMENT_BIT, "main", tri_frag_shader);

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

vulkan::VulkanGraphicsPipeline buildPostPipeline(
    vulkan::VulkanApplication& app,
    vulkan::PipelineLayout& pipeline_layout,
    vulkan::VkRenderPass* render_pass,
    vulkan::VulkanModel* screen) {
    // Build Post Pipeline
    vulkan::VulkanGraphicsPipeline pipeline = app.CreateGraphicsPipeline(&pipeline_layout, render_pass, 0);

    pipeline.AddShader(VK_SHADER_STAGE_VERTEX_BIT, "main", post_vert_shader);
    pipeline.AddShader(VK_SHADER_STAGE_FRAGMENT_BIT, "main", post_frag_shader);

    pipeline.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipeline.SetInputStreams(screen);
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

containers::vector<vulkan::ImagePointer> buildTempImages(
    vulkan::VulkanApplication& app,
    const entry::EntryData* data) {
    containers::vector<vulkan::ImagePointer> images(data->allocator());

    for(int index=0; index<app.swapchain_images().size(); index++) {
        VkImageCreateInfo image_create_info = {
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,    // sType
            nullptr,                                // pNext
            0,                                      // flags
            VK_IMAGE_TYPE_2D,                       // imageType
            app.swapchain().format(),               // format
            {
                app.swapchain().width(),            // width
                app.swapchain().height(),           // height
                1                                   // depth
            },                                      // extent
            1,                                      // mipLevels
            1,                                      // arrayLayers
            VK_SAMPLE_COUNT_1_BIT,                  // samples
            VK_IMAGE_TILING_OPTIMAL,                // tiling
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,    // usage
            VK_SHARING_MODE_EXCLUSIVE,              // sharingMode
            0,                                      // queueFamilyIndexCount
            nullptr,                                // pQueueFamilyIndices
            VK_IMAGE_LAYOUT_UNDEFINED               // initialLayout
        };
        images.push_back(app.CreateAndBindImage(&image_create_info));
    }

    return images;
}

containers::vector<vulkan::VkImageView> buildSwapchainImageViews(
    vulkan::VulkanApplication& app,
    const entry::EntryData* data) {
    containers::vector<vulkan::VkImageView> image_views(data->allocator());

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
        LOG_ASSERT(==, data->logger(), app.device()->vkCreateImageView(
            app.device(),
            &image_view_create_info,
            nullptr,
            &raw_image_view
        ), VK_SUCCESS);
        image_views.push_back(vulkan::VkImageView(raw_image_view, nullptr, &app.device()));
    }

    return image_views;
}

containers::vector<vulkan::VkImageView> buildTempImageViews(
    vulkan::VulkanApplication& app,
    containers::vector<vulkan::ImagePointer>& images,
    const entry::EntryData* data) {
    containers::vector<vulkan::VkImageView> image_views(data->allocator());

    for(int index=0; index < images.size(); index++) {
        auto temp_image = images[index].get();

        VkImageViewCreateInfo image_view_create_info {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,  // sType
            nullptr,                                   // pNext
            0,                                         // flags
            temp_image->get_raw_image(),               // image
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
        LOG_ASSERT(==, data->logger(), app.device()->vkCreateImageView(
            app.device(),
            &image_view_create_info,
            nullptr,
            &raw_image_view
        ), VK_SUCCESS);
        image_views.push_back(vulkan::VkImageView(raw_image_view, nullptr, &app.device()));
    }

    return image_views;
}

containers::vector<vulkan::VkFramebuffer> buildFramebuffers(
    vulkan::VulkanApplication& app,
    vulkan::VkRenderPass& render_pass,
    containers::vector<vulkan::VkImageView>& image_views,
    const entry::EntryData* data) {
    containers::vector<vulkan::VkFramebuffer> framebuffers(data->allocator());

    for(int index=0; index < app.swapchain_images().size(); index++) {
        VkFramebufferCreateInfo framebuffer_create_info {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,  // sType
            nullptr,                                    // pNext
            0,                                          // flags
            render_pass,                                // renderPass
            1,                                          // attachmentCount
            &image_views[index].get_raw_object(),       // attachments
            app.swapchain().width(),                    // width
            app.swapchain().height(),                   // height
            1                                           // layers
        };

        VkFramebuffer raw_framebuffer;
        LOG_ASSERT(==, data->logger(), app.device()->vkCreateFramebuffer(
            app.device(),
            &framebuffer_create_info,
            nullptr,
            &raw_framebuffer
        ), VK_SUCCESS);

        framebuffers.push_back(vulkan::VkFramebuffer(raw_framebuffer, nullptr, &app.device()));
    }

    return framebuffers;
}

vulkan::DescriptorSet buildDescriptorSet(
    vulkan::VulkanApplication& app,
    vulkan::VkSampler& sampler,
    vulkan::VkImageView& image_view) {
    auto descriptor_set = app.AllocateDescriptorSet({{
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        nullptr
    }});

    VkDescriptorImageInfo image_info = {
        sampler.get_raw_object(),
        image_view.get_raw_object(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkWriteDescriptorSet write{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,    // sType
        nullptr,                                   // pNext
        descriptor_set,                            // dstSet
        0,                                         // dstbinding
        0,                                         // dstArrayElement
        1,                                         // descriptorCount
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // descriptorType
        &image_info,                               // pImageInfo
        nullptr,                                   // pBufferInfo
        nullptr,                                   // pTexelBufferView
    };

    app.device()->vkUpdateDescriptorSets(app.device(), 1, &write, 0, nullptr);

    return descriptor_set;
}

int main_entry(const entry::EntryData* data) {
    data->logger()->LogInfo("Application Startup");

    vulkan::VulkanApplication app(data->allocator(), data->logger(), data);

    auto temp_images = buildTempImages(app, data);
    vulkan::VulkanModel screen(data->allocator(), data->logger(), screen_data);

    // Initialize Screen Model
    auto init_cmd_buf = app.GetCommandBuffer();
    app.BeginCommandBuffer(&init_cmd_buf);

    screen.InitializeData(&app, &init_cmd_buf);
    vulkan::VkFence init_fence = CreateFence(&app.device());
    app.EndAndSubmitCommandBuffer(
        &init_cmd_buf,
        &app.render_queue(),
        {},
        {},
        {},
        init_fence.get_raw_object()
    );
    app.device()->vkWaitForFences(
        app.device(),
        1,
        &init_fence.get_raw_object(),
        VK_TRUE,
        UINT64_MAX
    );

    // Default Sampler
    auto sampler = CreateDefaultSampler(&app.device());

    // Pipeline Layout
    auto post_pipeline_layout = app.CreatePipelineLayout({{{
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        nullptr
    }}});

    // Triangle Render Pass
    auto render_pass_triangle = buildRenderPass(
        app,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    auto pipeline_triangle = buildTrianglePipeline(app, &render_pass_triangle);
    auto image_views_triangle = buildTempImageViews(app, temp_images, data);
    auto framebuffers_triangle = buildFramebuffers(
        app,
        render_pass_triangle,
        image_views_triangle,
        data
    );

    // Post Render Pass
    auto render_pass_post = buildRenderPass(
        app,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );
    auto pipeline_post = buildPostPipeline(app, post_pipeline_layout, &render_pass_post, &screen);
    auto image_views_post = buildSwapchainImageViews(app, data);
    auto framebuffers_post = buildFramebuffers(
        app,
        render_pass_post,
        image_views_post,
        data
    );

    // Triangle Synchronization
    containers::vector<vulkan::VkCommandBuffer> command_buffers_triangle(data->allocator());
    containers::vector<vulkan::VkSemaphore> render_finished_triangle(data->allocator());

    // Post Synchronization
    containers::vector<vulkan::VkSemaphore> image_acquired(data->allocator());
    containers::vector<vulkan::VkSemaphore> render_finished_post(data->allocator());
    containers::vector<CommandTracker> command_trackers_post(data->allocator());
    containers::unordered_map<uint32_t, uint32_t> progress_post(data->allocator());
    uint32_t current_frame = 0;

    for(int index = 0; index < app.swapchain_images().size(); index++) {
        command_buffers_triangle.push_back(app.GetCommandBuffer());
        render_finished_triangle.push_back(vulkan::CreateSemaphore(&app.device()));
        image_acquired.push_back(vulkan::CreateSemaphore(&app.device()));
        render_finished_post.push_back(vulkan::CreateSemaphore(&app.device()));
        command_trackers_post.push_back({
            containers::make_unique<vulkan::VkCommandBuffer>(
                data->allocator(),
                app.GetCommandBuffer()
            ),
            containers::make_unique<vulkan::VkFence>(
                data->allocator(),
                vulkan::CreateFence(&app.device(), 1)
            ),
            containers::make_unique<vulkan::DescriptorSet>(
                data->allocator(),
                buildDescriptorSet(app, sampler, image_views_triangle[index])
            )
        });
    }

    VkClearValue clear_color = {1.0f, 1.0f, 1.0f, 1.0f};

    uint32_t image_index;

    while(!data->WindowClosing()) {
        app.device()->vkWaitForFences(
            app.device(),
            1,
            &command_trackers_post[current_frame].rendering_fence->get_raw_object(),
            VK_TRUE,
            UINT64_MAX
        );

        app.device()->vkAcquireNextImageKHR(
            app.device(),
            app.swapchain().get_raw_object(),
            UINT64_MAX,
            image_acquired[current_frame].get_raw_object(),
            static_cast<VkFence>(VK_NULL_HANDLE),
            &image_index
        );

        if (progress_post.find(image_index) != progress_post.end()) {
            auto frame_index = progress_post[image_index];
            app.device()->vkWaitForFences(
                app.device(),
                1,
                &command_trackers_post[current_frame].rendering_fence->get_raw_object(),
                VK_TRUE,
                UINT64_MAX
            );
            progress_post.erase(image_index);
        }

        app.device()->vkResetFences(
            app.device(),
            1,
            &command_trackers_post[current_frame].rendering_fence->get_raw_object()
        );

        VkRenderPassBeginInfo pass_begin_triangle {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            nullptr,
            render_pass_triangle,
            framebuffers_triangle[current_frame].get_raw_object(),
            {{0, 0}, {app.swapchain().width(), app.swapchain().height()}},
            1,
            &clear_color
        };

        VkRenderPassBeginInfo pass_begin_post {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            nullptr,
            render_pass_post,
            framebuffers_post[current_frame].get_raw_object(),
            {{0, 0}, {app.swapchain().width(), app.swapchain().height()}},
            1,
            &clear_color
        };

        auto& tri_cmd_buf = command_buffers_triangle[current_frame];

        app.BeginCommandBuffer(&tri_cmd_buf);
        /*vulkan::RecordImageLayoutTransition(
            app.swapchain_images()[current_frame],
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            VK_IMAGE_LAYOUT_UNDEFINED,
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            &tri_cmd_buf
        );*/

        tri_cmd_buf->vkCmdBeginRenderPass(tri_cmd_buf, &pass_begin_triangle, VK_SUBPASS_CONTENTS_INLINE);
        tri_cmd_buf->vkCmdBindPipeline(tri_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_triangle);
        tri_cmd_buf->vkCmdDraw(tri_cmd_buf, 3, 1, 0, 0);
        tri_cmd_buf->vkCmdEndRenderPass(tri_cmd_buf);
        LOG_ASSERT(==, data->logger(), VK_SUCCESS,
            app.EndAndSubmitCommandBuffer(
                &tri_cmd_buf,
                &app.render_queue(),
                {},
                {},
                {render_finished_triangle[current_frame].get_raw_object()},
                VK_NULL_HANDLE
            )
        );

        auto& post_cmd_buf = command_trackers_post[current_frame].command_buffer;
        vulkan::VkCommandBuffer& post_ref_cmd = *post_cmd_buf;

        app.BeginCommandBuffer(post_cmd_buf.get());
        vulkan::RecordImageLayoutTransition(
            app.swapchain_images()[image_index],
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            VK_IMAGE_LAYOUT_UNDEFINED,
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            post_cmd_buf.get()
        );

        post_ref_cmd->vkCmdBeginRenderPass(post_ref_cmd, &pass_begin_post, VK_SUBPASS_CONTENTS_INLINE);
        post_ref_cmd->vkCmdBindDescriptorSets(
            post_ref_cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            post_pipeline_layout,
            0,
            1,
            &command_trackers_post[current_frame].descriptor_set.get()->raw_set(),
            0,
            nullptr
        );
        post_ref_cmd->vkCmdBindPipeline(post_ref_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_post);
        screen.Draw(&post_ref_cmd);
        post_ref_cmd->vkCmdEndRenderPass(post_ref_cmd);
        LOG_ASSERT(==, data->logger(), VK_SUCCESS,
            app.EndAndSubmitCommandBuffer(
                &post_ref_cmd,
                &app.render_queue(),
                {
                    image_acquired[current_frame].get_raw_object(),
                    render_finished_triangle[current_frame].get_raw_object()
                },
                {
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                },
                {render_finished_post[current_frame].get_raw_object()},
                command_trackers_post[current_frame].rendering_fence->get_raw_object()
            )
        );

        progress_post.insert({image_index, current_frame});

        VkSemaphore signal_semaphores[] = {render_finished_post[current_frame].get_raw_object()};
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

        current_frame = (current_frame + 1) % app.swapchain_images().size();

        /*app.device()->vkWaitForFences(
            app.device(),
            1,
            &command_trackers[current_frame].rendering_fence->get_raw_object(),
            VK_TRUE,
        UINT64_MAX);

        app.device()->vkAcquireNextImageKHR(
            app.device(),
            app.swapchain().get_raw_object(),
            UINT64_MAX,
            image_acquired[current_frame].get_raw_object(),
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

        if (images_in_progress.find(image_index) != images_in_progress.end()) {
            auto frame_for_index = images_in_progress[image_index];
            app.device()->vkWaitForFences(
                app.device(),
                1,
                &command_trackers[frame_for_index].rendering_fence->get_raw_object(),
                VK_TRUE,
            UINT64_MAX);
            images_in_progress.erase(image_index);
        }

        app.device()->vkResetFences(
            app.device(),
            1,
            &command_trackers[current_frame].rendering_fence->get_raw_object()
        );

        auto& cmd_buf = command_trackers[current_frame].command_buffer;
        vulkan::VkCommandBuffer& ref_cmd_buf = *cmd_buf;

        app.BeginCommandBuffer(cmd_buf.get());
        vulkan::RecordImageLayoutTransition(
            app.swapchain_images()[image_index],
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            VK_IMAGE_LAYOUT_UNDEFINED,
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            cmd_buf.get()
        );

        ref_cmd_buf->vkCmdBeginRenderPass(ref_cmd_buf, &pass_begin, VK_SUBPASS_CONTENTS_INLINE);
        ref_cmd_buf->vkCmdBindPipeline(ref_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        ref_cmd_buf->vkCmdDraw(ref_cmd_buf, 3, 1, 0, 0);
        ref_cmd_buf->vkCmdEndRenderPass(ref_cmd_buf);
        LOG_ASSERT(==, data->logger(), VK_SUCCESS,
            app.EndAndSubmitCommandBuffer(
                cmd_buf.get(),
                &app.render_queue(),
                {image_acquired[current_frame].get_raw_object()},
                {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
                {render_finished[current_frame].get_raw_object()},
                command_trackers[current_frame].rendering_fence->get_raw_object()
            )
        );

        images_in_progress.insert({image_index, current_frame});

        VkSemaphore signal_semaphores[] = {render_finished[current_frame].get_raw_object()};
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

        current_frame = (current_frame + 1) % app.swapchain_images().size();*/
    }

    app.device()->vkDeviceWaitIdle(app.device());
    data->logger()->LogInfo("Application Shutdown");

    return 0;
}
