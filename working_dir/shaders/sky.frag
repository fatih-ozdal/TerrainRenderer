#version 430

#define OUT_FBO        layout(location = 0)
#define U_INV_PROJ     layout(location = 0)
#define U_INV_VIEW_ROT layout(location = 1)

in vec2 fNDC;

out OUT_FBO vec4 fboColor;

layout(binding = 0) uniform sampler2D skyTex;

U_INV_PROJ     uniform mat4 invProj;
U_INV_VIEW_ROT uniform mat3 invViewRot;

void main()
{
    // Unproject NDC xy to view-space direction (z=-1: near plane, pointing into the scene)
    vec4 viewDir = invProj * vec4(fNDC, -1.0, 1.0);
    viewDir.xyz /= viewDir.w;

    // Rotate to world space (no translation — sky is infinitely far)
    vec3 worldDir = normalize(invViewRot * viewDir.xyz);

    // Equirectangular UV
    // theta: polar angle from zenith via acos, range [0, PI] (0=up, PI=down)
    // phi:   azimuth via atan2, range [-PI, PI]
    
    const float PI = 3.14159265359;
    float phi   = atan(worldDir.z, worldDir.x);
    float theta = acos(clamp(worldDir.y, -1.0, 1.0));
    vec2 uv = vec2((phi + PI) / (2.0 * PI), theta / PI);

    vec3 color = textureLod(skyTex, uv, 0.0).rgb;
    float lum  = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fboColor   = vec4(color, log(max(lum, 1e-5)));
}
