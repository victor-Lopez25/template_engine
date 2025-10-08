#version 460

layout(location = 0) in vec4 vertColor;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec4 color;

layout(set=2, binding=0) uniform sampler2D texSampler;

void main() {
    color = texture(texSampler, uv) * vertColor;
}