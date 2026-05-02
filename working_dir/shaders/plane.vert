#version 430

#define IN_POS    layout(location = 0)
#define IN_NORMAL layout(location = 1)
#define IN_UV     layout(location = 2)

#define U_MODEL  layout(location = 0)
#define U_VIEW   layout(location = 1)
#define U_PROJ   layout(location = 2)
#define U_NORMAL layout(location = 3)

in IN_POS    vec3 inPos;
in IN_NORMAL vec3 inNormal;
in IN_UV     vec2 inUV;

U_MODEL  uniform mat4 model;
U_VIEW   uniform mat4 view;
U_PROJ   uniform mat4 proj;
U_NORMAL uniform mat3 normalMatrix;

out gl_PerVertex { vec4 gl_Position; };

out layout(location = 0) vec3 fNormal;
out layout(location = 1) vec3 fWorldPos;
out layout(location = 2) vec2 fUV;

void main()
{
    vec4 worldPos = model * vec4(inPos, 1.0);
    fNormal   = normalMatrix * inNormal;
    fWorldPos = worldPos.xyz;
    fUV       = inUV;
    gl_Position = proj * view * worldPos;
}
