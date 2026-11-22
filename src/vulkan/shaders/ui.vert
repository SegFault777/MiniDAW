#version 450
// MiniDAW Vulkan UI: every primitive is a triangle in pixel coordinates (see Draw.h for the vertex format).
layout(push_constant) uniform Push { vec2 viewport; } pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inShape;   // hx, hy, r, s
layout(location = 3) in vec4 inColor;   // unorm, straight alpha

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vShape;
layout(location = 2) out vec4 vColor;

void main() {
    vUV = inUV;
    vShape = inShape;
    vColor = inColor;
    gl_Position = vec4(inPos / pc.viewport * 2.0 - 1.0, 0.0, 1.0);   // Vulkan clip space has y pointing down, like pixels
}
