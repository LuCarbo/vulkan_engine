#version 460

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragLightSpacePos;

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

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int debugView;
    int materialType; // 0 = Solid Terrain/Prop, 1 = Animated Water, 2 = Skybox Dome
    float pad0;
    float pad1;
} pc;

layout(location = 0) out vec4 outColor;

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float lenSq = dot(v, v);
    return (lenSq > 1e-12) ? (v * inversesqrt(lenSq)) : fallback;
}

vec3 elevationRamp(float t) {
    vec3 deepBlue = vec3(0.05, 0.14, 0.58);
    vec3 teal = vec3(0.06, 0.64, 0.68);
    vec3 yellow = vec3(0.95, 0.86, 0.22);
    vec3 red = vec3(0.90, 0.18, 0.12);

    if (t < (1.0 / 3.0)) {
        return mix(deepBlue, teal, clamp(t * 3.0, 0.0, 1.0));
    } else if (t < (2.0 / 3.0)) {
        return mix(teal, yellow, clamp((t - (1.0 / 3.0)) * 3.0, 0.0, 1.0));
    }
    return mix(yellow, red, clamp((t - (2.0 / 3.0)) * 3.0, 0.0, 1.0));
}

float computeShadowPCF(vec4 lightSpacePos, float NdotL) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv = projCoords.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }
    if (NdotL <= 0.001) {
        return 0.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float bias = max(0.0012 * (1.0 - NdotL), 0.00025);
    float shadowSum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadowSum += texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * texelSize, projCoords.z - bias));
        }
    }
    return shadowSum / 9.0;
}

void main() {
    // Crisp flat-shaded normal via screen-space derivatives with Vulkan Y-down orientation guard
    vec3 rawNormal = cross(dFdx(fragWorldPos), dFdy(fragWorldPos));
    vec3 flatNormal = safeNormalize(rawNormal, vec3(0.0, 1.0, 0.0));
    vec3 viewDir = safeNormalize(ubo.cameraPosTime.xyz - fragWorldPos, vec3(0.0, 0.0, 1.0));
    if (dot(flatNormal, viewDir) < 0.0) {
        flatNormal = -flatNormal;
    }

    vec3 horizonColor = vec3(0.72, 0.85, 0.98);
    vec3 sunDir = safeNormalize(ubo.sunDirectionIntensity.xyz, vec3(0.0, 1.0, 0.0));

    // Skybox Dome
    if (pc.materialType == 2) {
        if (pc.debugView == 0) {
            vec3 rayDir = safeNormalize(fragWorldPos - ubo.cameraPosTime.xyz, vec3(0.0, 1.0, 0.0));
            vec3 zenithColor = vec3(0.16, 0.36, 0.72);
            vec3 groundHazeColor = vec3(0.20, 0.25, 0.32);

            vec3 skyColor;
            if (rayDir.y >= 0.0) {
                float zenithMix = sqrt(clamp(rayDir.y, 0.0, 1.0));
                skyColor = mix(horizonColor, zenithColor, zenithMix);
            } else {
                float groundMix = sqrt(clamp(-rayDir.y, 0.0, 1.0));
                skyColor = mix(horizonColor, groundHazeColor, groundMix);
            }

            float sunCos = dot(rayDir, sunDir);
            float sunHalo = smoothstep(0.90, 0.996, sunCos);
            float sunDisc = smoothstep(0.998, 0.9994, sunCos);
            vec3 warmSun = vec3(1.0, 0.90, 0.68) * ubo.sunColorAmbient.rgb;
            skyColor += warmSun * (sunHalo * 0.45 + sunDisc * 1.15);

            outColor = vec4(skyColor, 1.0);
        } else if (pc.debugView == 1) {
            outColor = vec4(flatNormal * 0.5 + 0.5, 1.0);
        } else if (pc.debugView == 2) {
            outColor = vec4(0.85, 0.90, 1.0, 1.0);
        } else {
            outColor = vec4(0.05, 0.05, 0.12, 1.0);
        }
        return;
    }

    // Terrain / Props (0) and Water (1)
    vec3 L = sunDir;
    float NdotL = max(dot(flatNormal, L), 0.0);

    vec3 skyAmbient = vec3(0.42, 0.56, 0.78);
    vec3 groundAmbient = vec3(0.22, 0.20, 0.16);
    vec3 ambient = mix(groundAmbient, skyAmbient, flatNormal.y * 0.5 + 0.5) * ubo.sunColorAmbient.a;

    vec3 H = safeNormalize(L + viewDir, flatNormal);
    float shininess = (pc.materialType == 1) ? 48.0 : 16.0;
    float specStrength = (pc.materialType == 1) ? 0.65 : 0.08;
    float NdotH = max(dot(flatNormal, H), 0.0);
    float specBase = (NdotH > 0.0) ? pow(NdotH, shininess) : 0.0;
    float spec = specBase * specStrength * step(0.01, NdotL);

    float shadowFactor = computeShadowPCF(fragLightSpacePos, NdotL);

    switch (pc.debugView) {
        case 1: {
            outColor = vec4(flatNormal * 0.5 + 0.5, 1.0);
            break;
        }
        case 2: {
            outColor = vec4(vec3(0.12 + 0.88 * shadowFactor), 1.0);
            break;
        }
        case 3: {
            float elev = clamp((fragWorldPos.y + 4.0) / 22.0, 0.0, 1.0);
            vec3 elevColor = elevationRamp(elev);
            outColor = vec4(mix(elevColor, vec3(gl_FragCoord.z), 0.20), 1.0);
            break;
        }
        case 0:
        default: {
            vec3 directSun = ubo.sunColorAmbient.rgb * ubo.sunDirectionIntensity.w * NdotL * shadowFactor;
            vec3 lit = fragColor * (ambient + directSun) + (spec * shadowFactor) * ubo.sunColorAmbient.rgb;
            float dist = length(ubo.cameraPosTime.xyz - fragWorldPos);
            float fogFactor = smoothstep(75.0, 260.0, dist) * 0.55;
            lit = mix(lit, horizonColor, fogFactor);
            outColor = vec4(lit, 1.0);
            break;
        }
    }
}
