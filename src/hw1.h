#pragma once

#include "utility.h"

#include <glm/ext.hpp> // for matrix calculation

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>

class ThreadPool;
struct GeoDataDTED;
struct GLState;
struct ShaderGL;

// ==================== //
//   HW-1 Related Code  //
// ==================== //
struct TerrainMesh
{
    static constexpr GLuint IN_POS = 0;
    static constexpr GLuint IN_NORMAL = 1;
    //
    GLuint    vBufferId = 0;
    GLuint    iBufferId = 0;
    GLuint    vaoId = 0;
    uint32_t  indexCount = 0;
    glm::vec2 yMinMax = glm::vec2(0);

    // Constructors & Destructor
    TerrainMesh() = default;
    TerrainMesh(std::vector<glm::vec3> vertices,
                std::vector<glm::vec3> normals,
                std::vector<unsigned int> indices,
                glm::vec2 yMinMax);
    TerrainMesh(const TerrainMesh&) = delete;
    TerrainMesh(TerrainMesh&&) = delete;
    TerrainMesh& operator=(const TerrainMesh&) = delete;
    TerrainMesh& operator=(TerrainMesh&&);
    ~TerrainMesh();
};

struct TerrainMeshGenerationParams
{
    // XYZ scale parameters (X, Z) will be used as is
    // Z will be clamped according to the DTED height min/max
    glm::vec2 rangeX;
    glm::vec2 rangeY;
    glm::vec2 rangeZ;
    // Terrain will be genertated as patches,
    // DTED data is quite large (3601 x 3601 height values)
    // So we do not use it directly but give a range into the data
    // DTED control points will be sampled wrt. to these parameters
    glm::uvec2 patchStartOffset;
    glm::uvec2 patchCount;
    glm::uvec2 controlPointPerPatch;
    glm::uvec2 vertexPerPatch;
    glm::uvec2 controlPointSkip = glm::uvec2(0);
};

TerrainMesh GenerateTerrainBSpline(ThreadPool& threadPool,
                                   const GeoDataDTED& dted,
                                   const TerrainMeshGenerationParams& params,
                                   const std::string& cachePath = "");

TerrainMesh GenerateTerrainBezier(ThreadPool& threadPool,
                                  const GeoDataDTED& dted,
                                  const TerrainMeshGenerationParams& params,
                                  const std::string& cachePath = "");

struct HW1
{
    TerrainMesh terrainMesh;
    GeoDataDTED terrainDTED;
    ShaderGL    vShader;
    ShaderGL    fShader;
    ShaderGL    tonemapVert;
    ShaderGL    tonemapFrag;
    ShaderGL    planeVert;
    ShaderGL    planeFrag;
    //
    MeshGL      planeMeshBody;
    MeshGL      planeMeshHelix;
    MeshGL      planeMeshGlass;
    MeshGL      planeMeshCable;
    TextureGL   planeBaseAlbedo;   // used by body and cables
    TextureGL   planeHelixAlbedo;  // used by helix
    //
    float       helixAngle = 0.0f;
    double      prevTime   = 0.0;
    //
    GLuint      hdrFboId = 0;
    GLuint      hdrColorTex = 0;
    GLuint      hdrDepthRbo = 0;
    glm::uvec2  hdrFboSize = glm::uvec2(0);
    //
    GLuint      fullscreenVao = 0;
    GLuint      fullscreenVbo = 0;
    GLuint      fullscreenIbo = 0;
    //
    float       middle_gray = 0.18f;
    float       LWhite = 1.0f;
    //
    TerrainMeshGenerationParams params;
    //
    uint32_t    curVertexPerPatch = 0;
    //
    GLState&    state;
    ThreadPool& threadPool;

    // Constructors & Destructor
         HW1(ThreadPool& threadPool,
             GLState& state);
         HW1(const HW1&) = delete;
         HW1(HW1&&) = delete;
    HW1& operator=(const HW1&) = delete;
    HW1& operator=(HW1&&) = delete;
         ~HW1();

    void RecreateHDRFBO(int w, int h);
    void DrawPlane(const glm::mat4x4& view, const glm::mat4x4& proj);
    void Work();
};