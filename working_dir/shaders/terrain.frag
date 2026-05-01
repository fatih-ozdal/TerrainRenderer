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
#define IN_NORMAL	layout(location = 0)
#define IN_HEIGHT	layout(location = 1)
#define IN_POS		layout(location = 2)

// This output must match to the COLOR_ATTACHMENTi (where 'i' is this location)
#define OUT_FBO		layout(location = 0)

// This must match GL_TEXTUREi (where 'i' is this binding)
//#define T_ALBEDO	layout(binding = 0)

// This must match the first parameter of glUniform...() calls
#define U_MODE		layout(location = 0)
#define U_RANGE		layout(location = 1)
#define U_LIGHT_POS layout(location = 2)

// Input
in IN_NORMAL vec3  fNormal;
in IN_HEIGHT float fHeight;
in IN_POS	 vec3  fWorldPos;

// Output
// This parameter goes to the framebuffer
out OUT_FBO vec4 fboColor;

// Uniforms
U_MODE uniform uint uMode;
U_RANGE uniform vec2 yRange;
U_LIGHT_POS uniform vec3 lPos;

// Textures
// No textures

const uint TERRAIN_GRADIENT_COUNT = 5;
const vec3 TERRAIN_COLORS[TERRAIN_GRADIENT_COUNT + 1] =
{
	vec3(0.229, 0.765, 0.049),	// Green
	vec3(0.329, 0.565, 0.149),	// Green
	vec3(0.666, 0.424, 0.027),	// Brown
	vec3(0.478, 0.227, 0.027),	// Brown
	vec3(1.000, 1.000, 1.000),  // Snow
	vec3(1.000, 1.000, 1.000)   // Duplicated to skip bounds check
};

void main(void)
{
	// Terrain color calculation
	float colorTexel = fHeight - yRange[0];
	colorTexel /= yRange[1] - yRange[0];
	colorTexel = clamp(colorTexel, 0, 1);		 // 0-1
	colorTexel *= float(TERRAIN_GRADIENT_COUNT); // 0-N

	uint colorIndex = uint(colorTexel);
	float frac = colorTexel - floor(colorTexel);
	vec3 color = mix(TERRAIN_COLORS[colorIndex],
				     TERRAIN_COLORS[colorIndex + 1], frac);

	// Shading (Lambertian Diffuse)
	vec3 lDir = normalize(lPos - fWorldPos);
	float factor = clamp(dot(fNormal, lDir), 0, 1);
	factor += 0.1; // Ambient term

	uint mode = uMode;
	switch(mode)
	{
		// Shaded
		case 0: fboColor = vec4(color * factor, 1); break;
		// Terrain Colors
		case 1: fboColor = vec4(color, 1); break;
		// Vertex Normals. Normal axes; by definition, is between [-1, 1])
		// Color is in between [0, 1]) so we adjust here for that
		case 2: fboColor = vec4((fNormal + 1) * 0.5, 1); break;
		// Texture Mapping without shading.
		//case 3: fboColor = texture2D(tAlbedo, fUV); break;
		// If mode is wrong, put pure white.
		default: fboColor = vec4(1); break;
	}
	fboColor.a = log(dot(fboColor.rgb, vec3(0.2126, 0.7152, 0.0722)) + 1e-5);
}
