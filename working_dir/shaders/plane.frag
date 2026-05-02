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

    // Fixed gray for lighting test — swap back to texture() after confirming
    // vec3 albedoColor = vec3(0.5, 0.5, 0.5);
    vec3 albedoColor = texture(albedo, fUV).rgb;

    // IBL diffuse: blurred sky at normal direction
    vec3 envDiffuse = textureLod(skyTex, equirectUV(N), 4.0).rgb;

    // IBL specular: sky at reflection direction (sharp, mip 0)
    vec3 R        = reflect(-V, N);
    vec3 envSpec  = textureLod(skyTex, equirectUV(R), 0.0).rgb;

    // Fresnel-Schlick for specular weight
    float cosTheta = clamp(dot(V, N), 0.0, 1.0);
    float fresnel  = 0.04 + 0.96 * pow(1.0 - cosTheta, 5.0);

    // Point light diffuse
    vec3  lDir = normalize(lPos - fWorldPos);
    float diff = clamp(dot(N, lDir), 0.0, 1.0);

    vec3 color = albedoColor * (envDiffuse + diff * 0.3) + envSpec * fresnel * 0.4;
    float lum  = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fboColor   = vec4(color, log(max(lum, 1e-5)));
}
