/* Copyright 2020 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#version 450

layout(binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstant {
    float width;
    float height;
} pushConstant;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 out_color;

float norm[25] = float[](
    0.00407830342, 0.01835236541, 0.0489396411, 0.01835236541, 0.00407830342,
    0.01835236541, 0.0489396411, 0.08564437194, 0.0489396411, 0.01835236541,
    0.0489396411, 0.08564437194, 0.10277324633, 0.08564437194, 0.0489396411,
    0.01835236541, 0.0489396411, 0.08564437194, 0.0489396411, 0.01835236541,
    0.00407830342, 0.01835236541, 0.0489396411, 0.01835236541, 0.00407830342
);

void main() {
    vec4 tex_color = vec4(0.0, 0.0, 0.0, 0.0);

    for(int i=0; i<5; i++) {
        for(int j=0; j<5; j++) {
            vec2 tex_coord = fragTexCoord + vec2(i-2, j-2) / vec2(pushConstant.width, pushConstant.height);
            tex_color += texture(texSampler, tex_coord) * norm[i * 5 + j];
        }
    }

    out_color = vec4(tex_color.rgb, 1.0);
}
