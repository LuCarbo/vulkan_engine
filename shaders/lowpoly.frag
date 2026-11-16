#version 460

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragLightSpacePos;
noperspective layout(location = 4) in vec3 fragColorAffine;
noperspective layout(location = 5) in vec2 fragUVAffine;

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

vec3 applyPsxDitherAndQuantize(vec3 color, vec2 fragCoord, float ditherStrength, bool quantize15Bit) {
    const int bayer4x4[16] = int[16](
        -4,  0, -3,  1,
         2, -2,  3, -1,
        -3,  1, -4,  0,
         3, -1,  2, -2
    );
    ivec2 px = ivec2(fragCoord) & 3;
    float dither = (float(bayer4x4[px.y * 4 + px.x]) / 32.0) * (ditherStrength * 0.35);
    vec3 c = clamp(color + vec3(dither), 0.0, 1.0);
    if (quantize15Bit) {
        c = floor(c * 31.0 + 0.5) / 31.0;
    }
    return c;
}

float samplePsxTexelPattern(vec2 uv, int materialType) {
    float scale = (materialType == 0) ? 64.0 : 48.0;
    ivec2 texel = ivec2(floor(uv * scale));
    int checker = (texel.x ^ texel.y) & 1;
    int subGrid = ((texel.x & 3) == 0 || (texel.y & 3) == 0) ? 1 : 0;
    return 1.0 - float(checker) * 0.08 - float(subGrid) * 0.05;
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

    float bias = max(0.0012 * (1.0 - NdotL), 0.00025);
    if (ubo.psxParams.x > 0.5) {
        vec2 snappedUv = floor(uv * 512.0 + 0.5) / 512.0;
        return texture(shadowMap, vec3(snappedUv, projCoords.z - bias));
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadowSum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadowSum += texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * texelSize, projCoords.z - bias));
        }
    }
    return shadowSum / 9.0;
}

void main() {
    bool psxActive = (ubo.psxParams.x > 0.5);
    bool affineActive = (ubo.psxConfig.y > 0.5);
    bool quantize15Bit = (ubo.psxConfig.z > 0.5);

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

            if (psxActive) {
                skyColor = applyPsxDitherAndQuantize(skyColor, gl_FragCoord.xy, ubo.psxParams.w, quantize15Bit);
            }

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
    float effectiveNdotL = psxActive ? (floor(NdotL * 6.0 + 0.5) / 6.0) : NdotL;

    vec3 skyAmbient = vec3(0.42, 0.56, 0.78);
    vec3 groundAmbient = vec3(0.22, 0.20, 0.16);
    vec3 ambient = mix(groundAmbient, skyAmbient, flatNormal.y * 0.5 + 0.5) * ubo.sunColorAmbient.a;

    vec3 H = safeNormalize(L + viewDir, flatNormal);
    float shininess = (pc.materialType == 1) ? 48.0 : 16.0;
    float specStrength = (pc.materialType == 1) ? 0.65 : 0.08;
    float NdotH = max(dot(flatNormal, H), 0.0);
    float specBase = (NdotH > 0.0) ? pow(NdotH, shininess) : 0.0;
    float spec = specBase * specStrength * step(0.01, NdotL);
    float effectiveSpec = psxActive ? (floor(spec * 4.0 + 0.5) / 4.0) * 0.5 : spec;

    float shadowFactor = computeShadowPCF(fragLightSpacePos, NdotL);

    vec3 baseColor = (psxActive && affineActive) ? fragColorAffine : fragColor;
    vec2 activeUV = (psxActive && affineActive) ? fragUVAffine : fragUV;
    if (psxActive && affineActive) {
        baseColor *= samplePsxTexelPattern(activeUV, pc.materialType);
    }

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
            vec3 directSun = ubo.sunColorAmbient.rgb * ubo.sunDirectionIntensity.w * effectiveNdotL * shadowFactor;
            vec3 lit = baseColor * (ambient + directSun) + (effectiveSpec * shadowFactor) * ubo.sunColorAmbient.rgb;
            float dist = length(ubo.cameraPosTime.xyz - fragWorldPos);
            float fogFar = max(ubo.psxConfig.w, 25.0);
            float fogNear = fogFar * 0.30;
            float rawFog = smoothstep(fogNear, fogFar, dist);
            float fogFactor = psxActive
                ? (floor(rawFog * 8.0 + 0.5) / 8.0) * 0.88
                : smoothstep(75.0, 260.0, dist) * 0.55;
            lit = mix(lit, horizonColor, fogFactor);
            if (psxActive) {
                lit = applyPsxDitherAndQuantize(lit, gl_FragCoord.xy, ubo.psxParams.w, quantize15Bit);
            }
            outColor = vec4(lit, 1.0);
            break;
        }
    }
}
