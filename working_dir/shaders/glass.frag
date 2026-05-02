#version 430

#define OUT_FBO   layout(location = 0)
#define U_CAM_POS layout(location = 2)

in layout(location = 0) vec3 fNormal;
in layout(location = 1) vec3 fWorldPos;

out OUT_FBO vec4 fboColor;

layout(binding = 0) uniform sampler2D skyTex;

U_CAM_POS uniform vec3 camPos;

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

    vec3 envColor  = textureLod(skyTex, equirectUV(R), 0.0).rgb;
    vec3 glassTint = vec3(0.05, 0.08, 0.15);

    // Mostly reflection with a dark blue-gray tint
    vec3  color = mix(glassTint, envColor, 0.9);
    float lum   = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fboColor    = vec4(color, log(max(lum, 1e-5)));
}
