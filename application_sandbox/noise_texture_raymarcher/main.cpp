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

namespace screen_model {
#include "fullscreen_quad.obj.h"
}
const auto& screen_data = screen_model::model;

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
