out vec4 OutputColor;

uniform sampler2D gbuf_position;
uniform sampler2D gbuf_normal;
uniform sampler2D gbuf_diffuse;
uniform sampler2D gbuf_specular;

struct Light {
    vec3 position;
    vec3 direction;

    mat4 transform;
    mat4 inverseTransform;

    int shadowMapSize;
    float nearBoundary;
    float farBoundary;

    float strength;
    vec3 color;
    vec3 volumeOffset;
    vec3 volumeSize;
    mat4 inverseVolumeTransform;

    mat4 depthBiasMVPNear;
    mat4 depthBiasMVPMid;
    mat4 depthBiasMVPFar;

    vec2 nearBiasRange;
    vec2 midBiasRange;
    vec2 farBiasRange;

    float shadowTransitionZone;
};

uniform vec2 u_ViewportSize;

uniform mat4 u_View;
uniform vec3 u_ViewerPos;
uniform Light u_Light;
uniform sampler2D u_shadowMap;
uniform bool u_RenderShadows;

// Retreived from maps
vec3 MappedFragmentPos = vec3(0.0);
vec3 MappedFragmentNormal = vec3(0.0);
vec3 MappedFragmentPosClipSpace = vec3(0.0);
float SpecularFactor = 0.0;

#require GetClosestPointOnEmitter
#require IsPointInVolume
#require GetLightDirection

// bool IsPointInLightCone(vec3 point) {
//     float cone_dist = dot(point - u_Light.position, u_Light.coneDirection);

//     if (cone_dist < 0.0 || cone_dist > u_Light.height) {
//         return false;
//     }

//     float cone_radius = (cone_dist / u_Light.height) * u_Light.baseRadius;
//     float orth_distance = length((point - u_Light.position) - cone_dist * u_Light.coneDirection);

//     return (orth_distance < cone_radius);
// }

float random(vec2 p) {
    vec2 K1 = vec2(
        23.14069263277926, // e^pi (Gelfond's constant)
         2.665144142690225 // 2^sqrt(2) (Gelfondâ€“Schneider constant)
    );
    return fract( cos( dot(p,K1) ) * 12345.6789 );
}

bool InRange(vec2 uv) {
    return uv.x >= 0.f && uv.x <= 0.5 && uv.y >= 0.f && uv.y <= 0.5;
}

float QueryMap(in sampler2D shadowMap, vec2 xy, vec2 offset) {
    if (!InRange(xy)) {
        return 1.f;
    }
    xy += offset;
    return texture(shadowMap, xy).r;
}

// The shadow map is 2048x2048 as it contains 3 mappings
float GetAttenuationAtPoint(vec4 shadowCoord, vec2 offset, float bias, int samples, bool useJitter) {
    float pixel = 1.f / float(u_Light.shadowMapSize);
    vec3 shadowCoordNorm = shadowCoord.xyz / shadowCoord.w;
    shadowCoordNorm.x *= 0.5;
    shadowCoordNorm.y *= 0.5;

    // Do a 3x3 filtering
    float shadowAttenuation = 0.f;

    vec3 lightDirection = GetLightDirection(MappedFragmentPos);

    int numSamples = 0;
    for (int dy = -samples; dy <= samples; dy++) {
        for (int dx = -samples; dx <= samples; dx++) {
            vec2 uv = shadowCoordNorm.xy + vec2(float(dx) * pixel, float(dy) * pixel);
            // Shift to choose the right location for the map sampling

            // Jitter uv

            // if (useJitter) {
            //     uv.x += ((random(uv) - 0.5)) * pixel;
            //     uv.y += ((random(uv) - 0.5)) * pixel;
            // }

            shadowAttenuation += QueryMap(u_shadowMap, uv, offset) + bias < shadowCoord.z ? 0.f : 1.f;
            numSamples += 1;
        }
    }
    return shadowAttenuation / float(numSamples);
}

float GetShadowAttenuation() {
    if (!u_RenderShadows || u_Light.shadowMapSize == 0) return 1.0;
    vec4 shadowCoordNear = u_Light.depthBiasMVPNear * vec4(MappedFragmentPos, 1);
    vec4 shadowCoordMid = u_Light.depthBiasMVPMid * vec4(MappedFragmentPos, 1);
    vec4 shadowCoordFar = u_Light.depthBiasMVPFar * vec4(MappedFragmentPos, 1);

    vec3 lightDirection = GetLightDirection(MappedFragmentPos);
    float lerpVal = (1.0 - dot(MappedFragmentNormal, lightDirection));
    float farBias = mix(u_Light.farBiasRange.x, u_Light.farBiasRange.y, lerpVal);
    float midBias = mix(u_Light.midBiasRange.x, u_Light.midBiasRange.y, lerpVal);
    float nearBias = mix(u_Light.nearBiasRange.x, u_Light.nearBiasRange.y, lerpVal);
    // return min(GetAttenuationAtPoint(i, shadowCoordNear, 0.0, 0.0),
    //     GetAttenuationAtPoint(i, shadowCoordFar, 0.5, 0.001));

    // return GetAttenuationAtPoint(i, shadowCoordFar, 0.5, 0.001);
    // return GetAttenuationAtPoint(i, shadowCoordFar, 0.5, farBias);

    // vec4 shadowCoordNearNorm = shadowCoordNear / shadowCoordNear.w;
    // shadowCoordNearNorm.x *= 0.5;

    float nearAtten = GetAttenuationAtPoint(shadowCoordNear, vec2(0.0, 0.0), nearBias, 2, false);
    float midAtten = GetAttenuationAtPoint(shadowCoordMid, vec2(0.5, 0.0), midBias, 1, false);
    float farAtten = GetAttenuationAtPoint(shadowCoordFar, vec2(0.0, 0.5), farBias, 1, false);

    float z = abs(MappedFragmentPosClipSpace.z);
    if (z < u_Light.nearBoundary - u_Light.shadowTransitionZone) {
        return nearAtten;
    }
    else if (z < u_Light.nearBoundary) {
        return mix(nearAtten, midAtten, clamp((z - (u_Light.nearBoundary - u_Light.shadowTransitionZone)) / u_Light.shadowTransitionZone, 0.0, 1.0));
    }
    else if (z < u_Light.farBoundary - u_Light.shadowTransitionZone) {
        return midAtten;
    }
    else if (z < u_Light.farBoundary) {
        return mix(midAtten, farAtten, clamp((z - (u_Light.farBoundary - u_Light.shadowTransitionZone)) / u_Light.shadowTransitionZone, 0.0, 1.0));
    }
    else {
        return farAtten;
    }
}

vec3 GetLightColorWithFalloff() {
    return u_Light.strength * u_Light.color;
}

vec3 GetDiffuseAccumulation() {
    float shadow = GetShadowAttenuation();
    vec3 lightDirection = GetLightDirection(MappedFragmentPos);
    float lightAngle = max(dot(MappedFragmentNormal, lightDirection), 0.0);
    return shadow * lightAngle * GetLightColorWithFalloff();
}

vec3 GetSpecularAccumulation() {
    vec3 specularAccum = vec3(0.0);
    float shadow = GetShadowAttenuation();
    vec3 lightDirection = GetLightDirection(MappedFragmentPos);
    vec3 viewDirection = normalize(u_ViewerPos - MappedFragmentPos);
    float lightAngle = max(dot(MappedFragmentNormal, lightDirection), 0.0);
    if (lightAngle > 0.0) {
        // Halfway vector.
        vec3 h = normalize(viewDirection + lightAngle);
        float n_dot_h = max(dot(MappedFragmentNormal, h), 0.0);

        if (n_dot_h == 0.0 && SpecularFactor <= 0.0) return specularAccum;
        specularAccum += shadow * pow(n_dot_h, SpecularFactor) * GetLightColorWithFalloff();
    }
    return specularAccum;
}

void main()
{
    // retrieve data from G-buffer
    // OutputColor = vec4(FragmentTexCoords.x, FragmentTexCoords.y, 1.0, 1.0);
    // return;
    // if (u_Light.shadowMapSize == 0) {
    //     OutputColor = vec4(1, 0, 0, 1.0);
    //     return;
    // }
    vec2 FragmentTexCoords = gl_FragCoord.xy / u_ViewportSize;
    vec4 texPos = texture(gbuf_position, FragmentTexCoords);
    if (texPos.a < 0.5) {
        OutputColor = vec4(0, 0, 0, 0);
        return;
    }
    MappedFragmentPos = texPos.rgb;

    // OutputColor = vec4(MappedFragmentPos / 200.0, 1.0);
    // return;

    MappedFragmentNormal = texture(gbuf_normal, FragmentTexCoords).rgb;
    float RenderFlag = texture(gbuf_normal, FragmentTexCoords).a;

    MappedFragmentPosClipSpace = vec3(u_View * vec4(MappedFragmentPos, 1.0));
    SpecularFactor = texture(gbuf_specular, FragmentTexCoords).a;

    // This is fully additive factors on top of ambient base color
    vec4 fullDiffuse = texture(gbuf_diffuse, FragmentTexCoords);
    vec3 Diffuse = fullDiffuse.rgb;
    float alpha = fullDiffuse.a;
    vec3 Specular = texture(gbuf_specular, FragmentTexCoords).rgb;

    if (RenderFlag < 0.9f) {
        // Anything less than this, we keep as full.
        OutputColor = fullDiffuse;
        return;
    }

    if (!IsPointInVolume(MappedFragmentPos)) {
        OutputColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 diffuseAccum = GetDiffuseAccumulation();
    vec3 specularAccum = GetSpecularAccumulation();
    float r = distance(MappedFragmentPos, GetClosestPointOnEmitter(MappedFragmentPos));


    if (abs(r) < 0.001) {
        OutputColor = vec4(
            (Diffuse * diffuseAccum + Specular * specularAccum), alpha);
    }
    else {
        OutputColor = vec4(
            (Diffuse * diffuseAccum + Specular * specularAccum) / (r * r), alpha);
    }

    // float shadow = GetShadowAttenuation();
    // vec3 lightDirection = GetLightDirection(MappedFragmentPos);
    // float lightAngle = max(dot(MappedFragmentNormal, lightDirection), 0.0);
    // OutputColor = vec4(specularAccum, alpha);
    // vec3 transformedPoint = vec3(u_Light.inverseTransform * vec4(MappedFragmentPos, 1.0));
    // float d = distance(transformedPoint, u_Light.position);
    // OutputColor = vec4(d / 200.0 + 0.3, d / 200.0 + 0.3, d / 200.0 + 0.3, 1.0);
}

