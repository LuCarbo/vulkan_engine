#version 460

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 lightSpaceMatrix;
    vec4 sunDirectionIntensity; // xyz = normalized dir toward sun, w = intensity
    vec4 sunColorAmbient;       // rgb = sun color, a = ambient strength
    vec4 cameraPosTime;         // xyz = camera world pos, w = elapsed time (s)
    vec4 waterParams;           // x = waveSpeed, y = waveHeight, z = waterLevel, w = reserved
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int debugView;
    int materialType; // 0 = Solid Terrain/Prop, 1 = Animated Water, 2 = Skybox Dome
    float pad0;
    float pad1;
} pc;

void main() {
    gl_Position = ubo.lightSpaceMatrix * pc.model * vec4(inPos, 1.0);
}
