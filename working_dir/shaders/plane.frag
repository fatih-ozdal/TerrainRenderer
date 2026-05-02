#version 430

#define OUT_FBO     layout(location = 0)
#define U_LIGHT_POS layout(location = 2)
#define U_CAM_POS   layout(location = 3)

in layout(location = 0) vec3 fNormal;
in layout(location = 1) vec3 fWorldPos;
in layout(location = 2) vec2 fUV;

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
    vec3 R = reflect(-V, N);

    vec3  baseColor = texture(albedo, fUV).rgb;
    vec3  envColor  = textureLod(skyTex, equirectUV(R), 0.0).rgb;

    vec3  lDir = normalize(lPos - fWorldPos);
    float diff = max(dot(N, lDir), 0.0);

    // Mostly diffuse, small environment reflection on top
    vec3  color = mix(baseColor * (0.2 + diff * 0.8), envColor, 0.05);
    float lum   = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fboColor    = vec4(color, log(max(lum, 1e-5)));
}
