#version 430

#define OUT_FBO     layout(location = 0)
#define U_LIGHT_POS layout(location = 2)

in vec3 fNormal;
in vec3 fWorldPos;
in vec2 fUV;

out OUT_FBO vec4 fboColor;

layout(binding = 0) uniform sampler2D albedo;

U_LIGHT_POS uniform vec3 lPos;

void main()
{
    vec3 color = texture(albedo, fUV).rgb;
    vec3 lDir  = normalize(lPos - fWorldPos);
    float diff = clamp(dot(normalize(fNormal), lDir), 0.0, 1.0);
    fboColor   = vec4(color * (diff + 0.1), 1.0);
    fboColor.a = log(dot(fboColor.rgb, vec3(0.2126, 0.7152, 0.0722)) + 1e-5);
}
