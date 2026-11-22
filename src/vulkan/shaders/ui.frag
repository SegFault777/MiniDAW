#version 450
// Keep in step with SoftRaster.cpp, the CPU twin of this shader that the tests use.
layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vShape;
layout(location = 2) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    float s = vShape.w;
    float cov = 1.0;
    if (s >= 0.0) {                                   // rounded rectangle, signed distance field (s > 0: outline)
        vec2 q = abs(vUV) - (vShape.xy - vec2(vShape.z));
        float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - vShape.z;
        cov = clamp(0.5 - d, 0.0, 1.0);
        if (s > 0.0) cov *= clamp(0.5 + d + s, 0.0, 1.0);
    } else if (s > -1.5) {                            // glyph from the atlas
        cov = texture(uAtlas, vUV).r;
    } else if (s > -2.5) {                            // anti-aliased line
        cov = clamp((1.0 - abs(vUV.y)) * vShape.x, 0.0, 1.0);
    }
    outColor = vec4(vColor.rgb, vColor.a * cov);
}
