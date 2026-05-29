#version 450

// Vertex inputs
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

// Push constant block — small (≤ 128 bytes) is portable.
layout(push_constant) uniform PC {
    mat4 viewProj;     // 64 bytes
    mat4 model;        // 64 bytes
} pc;

layout(location = 0) out vec3 vColor;

void main() {
    gl_Position = pc.viewProj * pc.model * vec4(inPos, 1.0);
    vColor      = inColor;
}
