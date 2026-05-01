#version 430

out vec4 fragColor;

layout(binding = 0) uniform sampler2D hdrTex;

layout(location = 0) uniform float middle_gray;
layout(location = 1) uniform float LWhite;

void main()
{
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(hdrTex, 0));
    vec3 hdrColor = texture(hdrTex, uv).rgb;

    // Read average log luminance from the 1x1 mip alpha channel
    int maxLevel = textureQueryLevels(hdrTex) - 1;
    float avgLogLum = textureLod(hdrTex, vec2(0.5), float(maxLevel)).a;
    float avgLum = max(exp(avgLogLum), 1e-4);

    // Reinhard extended tonemapping
    float lum = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
    float Lscaled = (middle_gray / avgLum) * lum;
    float Lout = Lscaled * (1.0 + Lscaled / (LWhite * LWhite)) / (1.0 + Lscaled);

    vec3 tonemapped = hdrColor * (Lout / max(lum, 1e-5));
    fragColor = vec4(tonemapped, 1.0);
}
