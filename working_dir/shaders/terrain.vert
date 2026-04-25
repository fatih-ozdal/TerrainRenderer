#version 430
/*
	File Name	: generic.vert
	Author		: Bora Yalciner
	Description	:

		Basic vertex transform shader...
		Just multiplies each vertex to MVP matrix
		and sends it to the rasterizer

		It supports custom per-vertex normals
		and texture (uv) coordinates.
*/

// Definitions
#define IN_POS		layout(location = 0)
#define IN_NORMAL	layout(location = 1)

#define OUT_NORMAL	layout(location = 0)
#define OUT_HEIGHT	layout(location = 1)
#define OUT_POS		layout(location = 2)

#define U_TRANSFORM_MODEL	  layout(location = 0)
#define U_TRANSFORM_VIEW	  layout(location = 1)
#define U_TRANSFORM_PROJ	  layout(location = 2)
#define U_TRANSFORM_NORMAL	  layout(location = 3)
#define U_VERTEX_HEIGHT_SCALE layout(location = 4)

// Input
in IN_POS	 vec3 vPos;
in IN_NORMAL vec3 vNormal;

// Output
// This parameter goes to rasterizer
// This is mandatory since we are using modern pipeline
out gl_PerVertex {vec4 gl_Position;};

// These pass through to rasterizer and will be iterpolated at
// fragment positions
out OUT_NORMAL  vec3  fNormal;
out OUT_HEIGHT  float fHeight;
out OUT_POS		vec3  fWorldPos;

// Uniforms
U_TRANSFORM_MODEL	uniform mat4 uModel;
U_TRANSFORM_VIEW	uniform mat4 uView;
U_TRANSFORM_PROJ	uniform mat4 uProjection;
U_TRANSFORM_NORMAL  uniform mat3 uNormalMatrix;
U_VERTEX_HEIGHT_SCALE  uniform float uWeightScale;

void main(void)
{
	vec3 vertexPos = vPos;
	vertexPos.y *= uWeightScale;

	// Height value should be in local space
	// since vertex Y minmax calculated in local space
	fHeight = vPos.y;

	fNormal = normalize(uNormalMatrix * vNormal);
	// World pos

	vec4 worldPos = uModel * vec4(vertexPos.xyz, 1.0f);
	fWorldPos = vec3(worldPos);

	// Rasterizer
	gl_Position = uProjection * uView * worldPos;
}