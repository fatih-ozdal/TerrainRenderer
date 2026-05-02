#version 430

#define IN_NORMAL   layout(location = 0)
#define IN_HEIGHT   layout(location = 1)
#define IN_POS      layout(location = 2)
#define OUT_FBO     layout(location = 0)
#define U_MODE      layout(location = 0)
#define U_RANGE     layout(location = 1)
#define U_LIGHT_POS layout(location = 2)
#define U_TEX_SCALE layout(location = 3)

in IN_NORMAL vec3  fNormal;
in IN_HEIGHT float fHeight;
in IN_POS    vec3  fWorldPos;

out OUT_FBO vec4 fboColor;

U_MODE      uniform uint  uMode;
U_RANGE     uniform vec2  yRange;
U_LIGHT_POS uniform vec3  lPos;
U_TEX_SCALE uniform float texScale;

layout(binding = 0) uniform sampler2D shoreAlbedo;
layout(binding = 1) uniform sampler2D grassAlbedo;
layout(binding = 2) uniform sampler2D rockAlbedo;
layout(binding = 3) uniform sampler2D snowAlbedo;

void main(void)
{
    vec3 N  = normalize(fNormal);
    vec2 uv = fWorldPos.xz * texScale;

    float elevation = clamp((fHeight - yRange.x) / (yRange.y - yRange.x), 0.0, 1.0);
    float slope     = 1.0 - clamp(N.y, 0.0, 1.0);

    float snowW  = smoothstep(0.72, 0.88, elevation);
    // Rock on steep slopes + rocky cliffs at the shoreline
    float rockW  = clamp(smoothstep(0.25, 0.55, slope)
                       + smoothstep(0.25, 0.13, elevation), 0.0, 1.0) * (1.0 - snowW);
    float shoreW = smoothstep(0.18, 0.04, elevation) * (1.0 - snowW) * (1.0 - rockW);
    float grassW = max(0.0, 1.0 - snowW - rockW - shoreW);

    vec3 albedo = shoreW * texture(shoreAlbedo, uv).rgb
                + grassW * texture(grassAlbedo, uv).rgb
                + rockW  * texture(rockAlbedo,  uv).rgb
                + snowW  * texture(snowAlbedo,  uv).rgb;

    vec3  lDir        = normalize(lPos - fWorldPos);
    float diff        = max(dot(N, lDir), 0.0);
    vec3  shadedColor = albedo * (0.15 + diff * 0.85);

    // Debug gradient colors (mode 1)
    const uint GRADIENT_COUNT = 5u;
    const vec3 GRADIENT_COLORS[6] = vec3[](
        vec3(0.229, 0.765, 0.049),
        vec3(0.329, 0.565, 0.149),
        vec3(0.666, 0.424, 0.027),
        vec3(0.478, 0.227, 0.027),
        vec3(1.000, 1.000, 1.000),
        vec3(1.000, 1.000, 1.000)
    );
    float t         = elevation * float(GRADIENT_COUNT);
    uint  gi        = uint(t);
    vec3  gradColor = mix(GRADIENT_COLORS[gi], GRADIENT_COLORS[gi + 1u], fract(t));

    switch(uMode)
    {
        case 0:  fboColor = vec4(shadedColor, 1.0); break;
        case 1:  fboColor = vec4(gradColor * (diff + 0.1), 1.0); break;
        case 2:  fboColor = vec4((N + 1.0) * 0.5, 1.0); break;
        default: fboColor = vec4(1.0); break;
    }
    fboColor.a = log(dot(fboColor.rgb, vec3(0.2126, 0.7152, 0.0722)) + 1e-5);
}
