#version 430

#define IN_POS layout(location = 0)

in IN_POS vec2 inPos;

out vec2 fNDC;

void main()
{
    fNDC = inPos;
    // z = w so after perspective divide depth = 1.0 (far plane)
    gl_Position = vec4(inPos, 1.0, 1.0);
}
