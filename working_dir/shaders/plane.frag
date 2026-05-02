#version 430

#define OUT_FBO     layout(location = 0)
#define U_LIGHT_POS layout(location = 2)
#define U_CAM_POS   layout(location = 3)

in vec3 fNormal;
in vec3 fWorldPos;
in vec2 fUV;

out OUT_FBO vec4 fboColor;

layout(binding = 0) uniform sampler2D albedo;
layout(binding = 1) uniform sampler2D skyTex;
layout(binding = 2) uniform sampler2D roughnessTex;

U_LIGHT_POS uniform vec3 lPos;
U_CAM_POS   uniform vec3 camPos;

const float PI = 3.14159265359;

vec2 equirectUV(vec3 dir)
{
    float phi   = atan(dir.z, dir.x);
    float theta = acos(clamp(dir.y, -1.0, 1.0));
    return vec2((phi + PI) / (2.0 * PI), theta / PI);
}

void main()
{
    vec3 N = normalize(fNormal);
    vec3 V = normalize(camPos - fWorldPos);
    vec3 R = reflect(-V, N);

    vec3  albedoColor = texture(albedo, fUV).rgb;
    float roughness   = texture(roughnessTex, fUV).r;

    // IBL diffuse: blurred sky at normal direction
    vec3 envDiffuse = textureLod(skyTex, equirectUV(N), 4.0).rgb;

    // IBL specular: mip level from roughness (rough=blurry, smooth=sharp)
    vec3 envSpec = textureLod(skyTex, equirectUV(R), roughness * 6.0).rgb;

    // Fresnel scaled down by roughness^2 (rough surfaces lose specularity)
    float cosTheta   = clamp(dot(V, N), 0.0, 1.0);
    float fresnel    = 0.04 + 0.96 * pow(1.0 - cosTheta, 5.0);
    float specWeight = fresnel * (1.0 - roughness * roughness);

    // Point light diffuse
    vec3  lDir = normalize(lPos - fWorldPos);
    float diff = clamp(dot(N, lDir), 0.0, 1.0);

    vec3 color = albedoColor * (envDiffuse + diff * 0.3) + envSpec * specWeight * 0.4;
    float lum  = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fboColor   = vec4(color, log(max(lum, 1e-5)));
}
