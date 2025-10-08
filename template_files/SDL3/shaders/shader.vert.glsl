#version 450

layout (location = 0) in vec2 position;

layout (location = 0) out vec4 outColor;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    if(gl_VertexIndex == 0) {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);
    } else if(gl_VertexIndex == 1) {
        outColor = vec4(0.0, 1.0, 0.0, 1.0);
    } else if(gl_VertexIndex == 2) {
        outColor = vec4(0.0, 0.0, 1.0, 1.0);
    }
}