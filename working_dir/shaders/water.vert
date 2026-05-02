#version 430

#define IN_POS    layout(location = 0)
#define IN_NORMAL layout(location = 1)
#define IN_UV     layout(location = 2)

#define U_MODEL  layout(location = 0)
#define U_VIEW   layout(location = 1)
#define U_PROJ   layout(location = 2)
#define U_NORMAL layout(location = 3)
#define U_TIME   layout(location = 4)

in IN_POS    vec3 inPos;
in IN_NORMAL vec3 inNormal;
in IN_UV     vec2 inUV;

U_MODEL  uniform mat4 model;
U_VIEW   uniform mat4 view;
U_PROJ   uniform mat4 proj;
U_NORMAL uniform mat3 normalMatrix;
U_TIME   uniform float uTime;

out layout(location = 0) vec3 fNormal;
out layout(location = 1) vec3 fWorldPos;

out gl_PerVertex { vec4 gl_Position; };

const float AMPLITUDE = 1.5;
const float FREQ_X    = 0.03;
const float FREQ_Z    = 0.02;
const float SPEED     = 0.8;

void main()
{
    float phase = FREQ_X * inPos.x + FREQ_Z * inPos.z + SPEED * uTime;
    float y     = AMPLITUDE * cos(phase);

    float dydx = -AMPLITUDE * FREQ_X * sin(phase);
    float dydz = -AMPLITUDE * FREQ_Z * sin(phase);

    vec3 localNormal = normalize(vec3(-dydx, 1.0, -dydz));

    vec4 worldPos = model * vec4(inPos.x, y, inPos.z, 1.0);
    fWorldPos = worldPos.xyz;
    fNormal   = normalMatrix * localNormal;

    gl_Position = proj * view * worldPos;
}
