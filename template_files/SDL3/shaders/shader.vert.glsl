#version 460

layout (location = 0) in vec2 position;
layout (location = 1) in vec2 uv;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec2 outUV;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    if((gl_VertexIndex % 3) == 0) {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);
    } else if((gl_VertexIndex % 3) == 1) {
        outColor = vec4(0.0, 1.0, 0.0, 1.0);
    } else if((gl_VertexIndex % 3) == 2) {
        outColor = vec4(0.0, 0.0, 1.0, 1.0);
    }
    outUV = uv;
}