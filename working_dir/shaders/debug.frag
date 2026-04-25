#version 430
/*
	File Name	: color.vert
	Author		: Bora Yalciner
	Description	:

		Basic fragment shader that just outputs
		color to the FBO
*/

// Definitions
// These locations must match between vertex/fragment shaders
#define IN_UV		layout(location = 0)
#define IN_NORMAL	layout(location = 1)
#define IN_COLOR	layout(location = 2)

// This output must match to the COLOR_ATTACHMENTi (where 'i' is this location)
#define OUT_FBO		layout(location = 0)

// This must match GL_TEXTUREi (where 'i' is this binding)
#define T_ALBEDO	layout(binding = 0)

// This must match the first parameter of glUniform...() calls
#define U_MODE		layout(location = 0)

// Input
in IN_UV	 vec2 fUV;
in IN_NORMAL vec3 fNormal;

// Output
// This parameter goes to the framebuffer
out OUT_FBO vec4 fboColor;

// Uniforms
U_MODE uniform uint uMode;

// Textures
uniform T_ALBEDO sampler2D tAlbedo;

void main(void)
{
	uint mode = uMode;
	switch(mode)
	{
		// Pure Red
		case 0: fboColor = vec4(1, 0, 0, 1); break;
		// Vertex Normals. Normal axes by definition is between [-1, 1])
		// Color is in between [0, 1]) so we adjust here for that
		case 1: fboColor = vec4((fNormal + 1) * 0.5, 1); break;
		// UV
		case 2: fboColor = vec4(fUV, 0, 1); break;
		// Texture Mapping without shading.
		case 3: fboColor = texture2D(tAlbedo, fUV); break;
		// If mode is wrong, put pure white.
		default: fboColor = vec4(1); break;
	}
}