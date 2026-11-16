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
    vec4 psxParams;             // x = enabled, y = internalWidth, z = internalHeight, w = ditherStrength
    vec4 psxConfig;             // x = vertexSnapStrength, y = affineWarping, z = color15Bit, w = fogFar
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int debugView;
    int materialType; // 0 = Solid Terrain/Prop, 1 = Animated Water, 2 = Skybox Dome
    float pad0;
    float pad1;
} pc;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragLightSpacePos;
noperspective layout(location = 4) out vec3 fragColorAffine;
noperspective layout(location = 5) out vec2 fragUVAffine;

void main() {
    vec4 worldPos = pc.model * vec4(inPos, 1.0);

    // Stylized multi-wave vertex displacement for animated water
    if (pc.materialType == 1) {
        float t = ubo.cameraPosTime.w * ubo.waterParams.x;
        float amp = ubo.waterParams.y;
        float wave1 = sin(worldPos.x * 0.45 + t * 1.4 + worldPos.z * 0.25);
        float wave2 = cos(worldPos.z * 0.55 - t * 1.1 + worldPos.x * 0.30);
        worldPos.y += (wave1 + 0.65 * wave2) * amp;
    }

    fragWorldPos = worldPos.xyz;
    fragColor = inColor;
    fragUV = inUV;
    fragLightSpacePos = ubo.lightSpaceMatrix * worldPos;
    fragColorAffine = inColor;
    fragUVAffine = inUV;

    vec4 clipPos = ubo.viewProj * worldPos;
    if (ubo.psxParams.x > 0.5 && ubo.psxConfig.x > 0.001 && pc.materialType != 2 && clipPos.w > 0.0001) {
        vec2 grid = max(ubo.psxParams.yz * 0.5, vec2(1.0));
        vec2 ndc = clipPos.xy / clipPos.w;
        vec2 snappedNdc = floor(ndc * grid + 0.5) / grid;
        clipPos.xy = mix(ndc, snappedNdc, ubo.psxConfig.x) * clipPos.w;
    }
    gl_Position = clipPos;
}
