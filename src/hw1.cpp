#include "hw1.h"
#include "utility.h"

#include <array>
#include <fstream>
#include <glm/ext.hpp>

#include "thread_pool.h"

// Internal CPU-side terrain data, used between generation and GPU upload
struct TerrainRawData
{
    std::vector<glm::vec3>    vertices;
    std::vector<glm::vec3>    normals;
    std::vector<unsigned int> indices;
    glm::vec2                 yMinMax;
};

static void SaveTerrainCache(const std::string& path, const TerrainRawData& data)
{
    std::ofstream f(path, std::ios::binary);
    if(!f)
    {
        std::printf("[Cache] Warning: could not write terrain cache to \"%s\"\n", path.c_str());
        return;
    }
    uint32_t vertexCount = uint32_t(data.vertices.size());
    uint32_t indexCount  = uint32_t(data.indices.size());
    f.write(reinterpret_cast<const char*>(&vertexCount),         sizeof(vertexCount));
    f.write(reinterpret_cast<const char*>(&indexCount),          sizeof(indexCount));
    f.write(reinterpret_cast<const char*>(&data.yMinMax),        sizeof(data.yMinMax));
    f.write(reinterpret_cast<const char*>(data.vertices.data()), vertexCount * sizeof(glm::vec3));
    f.write(reinterpret_cast<const char*>(data.normals.data()),  vertexCount * sizeof(glm::vec3));
    f.write(reinterpret_cast<const char*>(data.indices.data()),  indexCount  * sizeof(uint32_t));
    std::printf("[Cache] Terrain saved to \"%s\".\n", path.c_str());
}

static TerrainMesh LoadTerrainCache(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    uint32_t vertexCount, indexCount;
    glm::vec2 yMinMax;
    f.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
    f.read(reinterpret_cast<char*>(&indexCount),  sizeof(indexCount));
    f.read(reinterpret_cast<char*>(&yMinMax),     sizeof(yMinMax));

    std::vector<glm::vec3>    vertices(vertexCount);
    std::vector<glm::vec3>    normals(vertexCount);
    std::vector<unsigned int> indices(indexCount);
    f.read(reinterpret_cast<char*>(vertices.data()), vertexCount * sizeof(glm::vec3));
    f.read(reinterpret_cast<char*>(normals.data()),  vertexCount * sizeof(glm::vec3));
    f.read(reinterpret_cast<char*>(indices.data()),  indexCount  * sizeof(uint32_t));

    std::printf("[Cache] Terrain loaded from \"%s\".\n", path.c_str());
    return TerrainMesh(std::move(vertices), std::move(normals), std::move(indices), yMinMax);
}

TerrainMesh GenerateTerrainBSpline(ThreadPool& threadPool,
                                   const GeoDataDTED& dted,
                                   const TerrainMeshGenerationParams& params,
                                   const std::string& cachePath)
{
    using namespace glm;
    // Bounds check
    uvec2 cpCount = params.patchCount + uvec2(3);
    uvec2 start = params.patchStartOffset;
    uvec2 end = start + cpCount * (params.controlPointSkip + uvec2(1));
    if(dted.dimensions[0] < start[0] || dted.dimensions[1] < start[1] ||
       dted.dimensions[0] < end[0] || dted.dimensions[1] < end[1])
    {
        std::fprintf(stderr, "Unable generate terrain, since patch parameters exceed dted size!\n");
        std::exit(EXIT_FAILURE);
    }
    if(params.controlPointPerPatch[0] != 4 || params.controlPointPerPatch[1] != 4)
    {
        std::fprintf(stderr, "Unable generate terrain, b-spline control point size must be 4!\n");
        std::exit(EXIT_FAILURE);
    }
        //
    dvec2 vertexDelta = dvec2(1) / dvec2(params.vertexPerPatch - uvec2(1));
    dvec2 cpDelta = dvec2(1) / dvec2(cpCount - uvec2(1));
    // Pre-generate polynomial table (Pv_x x Pv_y * CP_x * CP_y)
    // (Pv = vertex per patch, CP = control point per patch
    uint32_t coeffTableSize = (params.vertexPerPatch[1] * params.vertexPerPatch[0] *
                               params.controlPointPerPatch[1] * params.controlPointPerPatch[0]);
    std::vector<vec3> polynomialTable; polynomialTable.reserve(coeffTableSize);
    for(uint32_t tI = 0; tI < params.vertexPerPatch[1]; tI++)
    for(uint32_t sI = 0; sI < params.vertexPerPatch[0]; sI++)
    {
        static constexpr mat4 COEFF_MATRIX = mat4(-1,  3, -3, 1,
                                                   3, -6,  0, 4,
                                                  -3,  3,  3, 1,
                                                   1,  0,  0, 0);
        static constexpr mat4 COEFF_MATRIX_NORM = COEFF_MATRIX / float(6);

        float t = float(vertexDelta[1] * double(tI));
        float t2 = t * t;
        float t3 = t2 * t;
        //
        float s = float(vertexDelta[0] * double(sI));
        float s2 = s * s;
        float s3 = s2 * s;
        //
        vec4 tV = vec4(t3, t2, t, 1);
        vec4 sV = vec4(s3, s2, s, 1);
        // Derivatives
        vec4 dQdtV = vec4(3 * t2, 2 * t, 1, 0);
        vec4 dQdsV = vec4(3 * s2, 2 * s, 1, 0);
        //
        vec4 tR = tV * COEFF_MATRIX_NORM;
        vec4 sR = sV * COEFF_MATRIX_NORM;
        vec4 dQdtR = dQdtV * COEFF_MATRIX_NORM;
        vec4 dQdsR = dQdsV * COEFF_MATRIX_NORM;
        //
        static const auto Push = [&](uint32_t j)
        {
            polynomialTable.push_back(vec3(tR[j] * sR[0], dQdtR[j] * sR[0], tR[j] * dQdsR[0]));
            polynomialTable.push_back(vec3(tR[j] * sR[1], dQdtR[j] * sR[1], tR[j] * dQdsR[1]));
            polynomialTable.push_back(vec3(tR[j] * sR[2], dQdtR[j] * sR[2], tR[j] * dQdsR[2]));
            polynomialTable.push_back(vec3(tR[j] * sR[3], dQdtR[j] * sR[3], tR[j] * dQdsR[3]));
        };
        Push(0);
        Push(1);
        Push(2);
        Push(3);
    }

    // Pre-allocate the vertex/normal/index arrays
    std::vector<vec3> vertices;
    std::vector<vec3> normals;
    std::vector<unsigned int> indices;
    uvec2 vCount2D = params.vertexPerPatch * params.patchCount;
    uvec2 boxCount2D = (params.vertexPerPatch - uvec2(1)) * params.patchCount;
    vertices.resize(vCount2D[0] * vCount2D[1]);
    normals.resize(vCount2D[0] * vCount2D[1]);
    indices.resize(boxCount2D[0] * boxCount2D[1] * 6);

    std::atomic_uint32_t patchCounter = 0;
    uint32_t totalPatchCount = params.patchCount[1] * params.patchCount[0];
    threadPool.SubmitDetachedBlocks(totalPatchCount, [&](uint32_t start, uint32_t end)
    {
        for(uint32_t patchId = start; patchId < end; patchId++)
        {
            uint32_t pY = patchId / params.patchCount[0];
            uint32_t pX = patchId % params.patchCount[0];
            uint32_t patchVertexOffset = patchId * (params.vertexPerPatch[1] * params.vertexPerPatch[0]);
            uint32_t patchIndexOffset = (patchId * 6 *
                                         (params.vertexPerPatch[1] - 1) *
                                         (params.vertexPerPatch[0] - 1));

            uint32_t patchCounterOut = patchCounter.fetch_add(1);
            // We can get away with printf although it is technically not thread safe
            // all OS have it implemented in a thread safe manner.
            std::printf("Generating Patch [%d / %d]\r",
                        patchCounterOut, params.patchCount[1] * params.patchCount[0]);

            // For each point in patch
            for(uint32_t tI = 0; tI < params.vertexPerPatch[1]; tI++)
            for(uint32_t sI = 0; sI < params.vertexPerPatch[0]; sI++)
            {
                // For each control point
                glm::dvec3 vertex = glm::dvec3(0);
                glm::dvec3 dqdt = glm::dvec3(0);
                glm::dvec3 dqds = glm::dvec3(0);
                for(uint32_t j = 0; j < 4; j++)
                for(uint32_t i = 0; i < 4; i++)
                {
                    // Normalize height
                    uvec2 cpOffset = uvec2(pX, pY) + uvec2(i, j);
                    uvec2 cpGlobalOffset = cpOffset;
                    cpGlobalOffset *= (params.controlPointSkip + uvec2(1));
                    cpGlobalOffset += uvec2(params.patchStartOffset);

                    float height = dted(cpGlobalOffset[0], cpGlobalOffset[1]);
                    height -= dted.minMax[0];
                    height /= dted.minMax[1] - dted.minMax[0];
                    height *= params.rangeY[1] - params.rangeY[0];
                    height += params.rangeY[0];
                    // Normalize XZ coordinates
                    vec2 cpXZ = vec2(cpOffset) * vec2(cpDelta);
                    // Fit to the range
                    cpXZ[0] *= params.rangeX[1] - params.rangeX[0];
                    cpXZ[0] += params.rangeX[0];
                    cpXZ[1] *= params.rangeZ[1] - params.rangeZ[0];
                    cpXZ[1] += params.rangeZ[0];
                    vec3 cp = vec3(cpXZ[0], height, cpXZ[1]);
                    //
                    static constexpr uint32_t CP_COUNT_1D = 16;
                    uint32_t vIndex = tI * params.vertexPerPatch[0] + sI;
                    uint32_t cpIndex = j * 4 + i;
                    uint32_t coeffIndex = vIndex * CP_COUNT_1D + cpIndex;

                    vec3 coeffs = polynomialTable[coeffIndex];
                    vertex += cp * coeffs[0];
                    dqdt += cp * coeffs[1];
                    dqds += cp * coeffs[2];
                }
                uint32_t writeIndex = patchVertexOffset + tI * params.vertexPerPatch[0] + sI;
                vertices[writeIndex] = vec3(vertex);
                normals[writeIndex] = vec3(glm::normalize(glm::cross(dqdt, dqds)));
            }

            // Index calculation
            for(uint32_t tI = 0; tI < (params.vertexPerPatch[1] - 1); tI++)
            for(uint32_t sI = 0; sI < (params.vertexPerPatch[0] - 1); sI++)
            {
                // 0, 1, (n + 1), | 0, (n + 1), n
                uint32_t i = (tI + 0) * params.vertexPerPatch[0] + sI;
                uint32_t n = i + params.vertexPerPatch[0];
                //
                uint32_t writeIndex = patchIndexOffset + (tI * (params.vertexPerPatch[0] - 1) + sI) * 6;

                indices[writeIndex + 0] = patchVertexOffset + i + 0;
                indices[writeIndex + 1] = patchVertexOffset + n + 1;
                indices[writeIndex + 2] = patchVertexOffset + i + 1;
                //
                indices[writeIndex + 3] = patchVertexOffset + i + 0;
                indices[writeIndex + 4] = patchVertexOffset + n + 0;
                indices[writeIndex + 5] = patchVertexOffset + n + 1;

            }
        }
    });
    threadPool.Wait();

    // Get minmax vertex for gradient
    vec2 yMinMax = vec2(FLT_MAX, -FLT_MAX);
    for(vec3& v : vertices)
    {
        yMinMax[0] = std::min(yMinMax[0], v.y);
        yMinMax[1] = std::max(yMinMax[1], v.y);
    }

    TerrainRawData raw{std::move(vertices), std::move(normals), std::move(indices), yMinMax};
    if(!cachePath.empty()) SaveTerrainCache(cachePath, raw);
    return TerrainMesh(std::move(raw.vertices), std::move(raw.normals),
                       std::move(raw.indices), raw.yMinMax);
}

TerrainMesh GenerateTerrainBezier(ThreadPool& threadPool,
                                  const GeoDataDTED& dted,
                                  const TerrainMeshGenerationParams& params,
                                  const std::string& cachePath)
{
    using namespace glm;
    // Bounds check
    uvec2 start = params.patchStartOffset;
    uvec2 end = start + params.controlPointPerPatch * params.patchCount;
    if(dted.dimensions[0] < start[0] || dted.dimensions[1] < start[1] ||
       dted.dimensions[0] < end[0] || dted.dimensions[1] < end[1])
    {
        std::fprintf(stderr, "Unable generate terrain, since patch parameters exceed dted size!\n");
        std::exit(EXIT_FAILURE);
    }
    // TODO: Enforce tangent continutiy via generating a planar control point between edges
    // Generate Combination for bernstein polynomials
    static auto Combination = [](uint32_t n, uint32_t k)
    {
        if(k == 0) return uint32_t(1);
        if(k > n / 2) k = n - k;

        //assert(n != 0);
        uint64_t nom = 1;
        for(uint64_t i = 0; i < k; i++) nom *= (n - i);
        //
        uint64_t denom = 1;
        for(uint64_t i = 0; i < k; i++) denom *= (i + 1);

        return uint32_t(nom / denom);
    };
    std::vector<float> combinationM(params.controlPointPerPatch[1]);
    for(uint32_t j = 0; j < params.controlPointPerPatch[1]; j++)
        combinationM[j] = float(Combination(params.controlPointPerPatch[1] - 1, j));
    std::vector<float> combinationN(params.controlPointPerPatch[0]);
    for(uint32_t i = 0; i < params.controlPointPerPatch[0]; i++)
        combinationN[i] = float(Combination(params.controlPointPerPatch[0] - 1, i));
    //
    dvec2 vertexDelta = dvec2(1) / dvec2(params.vertexPerPatch - uvec2(1));
    dvec2 cpDelta = dvec2(1) / (dvec2(params.controlPointPerPatch - uvec2(1)) * dvec2(params.patchCount));
    // Pre-generate polynomial table (Pv_x x Pv_y * CP_x * CP_y)
    // (Pv = vertex per patch, CP = control point per patch
    uint32_t coeffTableSize = (params.vertexPerPatch[1] * params.vertexPerPatch[0] *
                               params.controlPointPerPatch[1] * params.controlPointPerPatch[0]);;
    std::vector<vec3> polynomialTable; polynomialTable.reserve(coeffTableSize);
    for(uint32_t tI = 0; tI < params.vertexPerPatch[1]; tI++)
    for(uint32_t sI = 0; sI < params.vertexPerPatch[0]; sI++)
    for(uint32_t j = 0; j < params.controlPointPerPatch[1]; j++)
    for(uint32_t i = 0; i < params.controlPointPerPatch[0]; i++)
    {
        double t = vertexDelta[1] * double(tI);
        double s = vertexDelta[0] * double(sI);
        // Polynomials
        uint32_t jInv = params.controlPointPerPatch[1] - j - 1;
        double bM0 = std::pow(t, j);
        double bM1 = std::pow(double(1) - t, jInv);
        double bM = double(combinationM[j]) * bM0 * bM1;
        //
        uint32_t iInv = params.controlPointPerPatch[0] - i - 1;
        double bN0 = std::pow(s, i);
        double bN1 = std::pow(double(1) - s, iInv);
        double bN = double(combinationN[i]) * bN0 * bN1;

        // Derivatives (for normals)
        double bMdt = j * std::pow(t, j - 1) * bM1;
        bMdt -= jInv * std::pow(double(1) - t, jInv - 1) * bM0;
        bMdt *= double(combinationM[j]);
        //
        double bNds = i * std::pow(s, i - 1) * bN1;
        bNds -= iInv * std::pow(double(1) - s, iInv - 1) * bN0;
        bNds *= double(combinationN[i]);

        vec3 coeffs = vec3(bM * bN,    // polynomial
                            bMdt * bN,    // dQdt
                            bM * bNds); // dQds
        polynomialTable.push_back(coeffs);
    }

    // Pre-allocate the vertex/normal/index arrays
    std::vector<vec3> vertices;
    std::vector<vec3> normals;
    std::vector<unsigned int> indices;
    uvec2 vCount2D = params.vertexPerPatch * params.patchCount;
    uvec2 boxCount2D = (params.vertexPerPatch - uvec2(1)) * params.patchCount;
    vertices.resize(vCount2D[0] * vCount2D[1]);
    normals.resize(vCount2D[0] * vCount2D[1]);
    indices.resize(boxCount2D[0] * boxCount2D[1] * 6);

    std::atomic_uint32_t patchCounter = 0;
    uint32_t totalPatchCount = params.patchCount[1] * params.patchCount[0];
    threadPool.SubmitDetachedBlocks(totalPatchCount, [&](uint32_t start, uint32_t end)
    {
        for(uint32_t patchId = start; patchId < end; patchId++)
        {
            uint32_t pY = patchId / params.patchCount[0];
            uint32_t pX = patchId % params.patchCount[0];
            uint32_t patchVertexOffset = patchId * (params.vertexPerPatch[1] * params.vertexPerPatch[0]);
            uint32_t patchIndexOffset = (patchId * 6 *
                                         (params.vertexPerPatch[1] - 1) *
                                         (params.vertexPerPatch[0] - 1));

            uint32_t patchCounterOut = patchCounter.fetch_add(1);
            // We can get away with printf although it is technically not thread safe
            // all OS have it implemented in a thread safe manner.
            std::printf("Generating Patch [%d / %d]\r",
                        patchCounterOut, params.patchCount[1] * params.patchCount[0]);

            ivec2 cpLocalOffset = ivec2(pX, pY) * ivec2(params.controlPointPerPatch) - ivec2(pX, pY);
            ivec2 cpGlobalOffset = cpLocalOffset + ivec2(params.patchStartOffset);

            // For each point in patch
            for(uint32_t tI = 0; tI < params.vertexPerPatch[1]; tI++)
            for(uint32_t sI = 0; sI < params.vertexPerPatch[0]; sI++)
            {
                // For each control point
                glm::dvec3 vertex = glm::dvec3(0);
                glm::dvec3 dqdt = glm::dvec3(0);
                glm::dvec3 dqds = glm::dvec3(0);
                for(uint32_t j = 0; j < params.controlPointPerPatch[1]; j++)
                    for(uint32_t i = 0; i < params.controlPointPerPatch[0]; i++)
                    {
                        // Normalize height
                        float height = dted(cpGlobalOffset[0] + i, cpGlobalOffset[1] + j);
                        height -= dted.minMax[0];
                        height /= dted.minMax[1] - dted.minMax[0];
                        height *= params.rangeY[1] - params.rangeY[0];
                        height += params.rangeY[0];
                        // Normalize XZ coordinates
                        vec2 cpXZ = (vec2(cpLocalOffset) + vec2(i, j)) * vec2(cpDelta);
                        // Fit to the range
                        cpXZ[0] *= params.rangeX[1] - params.rangeX[0];
                        cpXZ[0] += params.rangeX[0];
                        cpXZ[1] *= params.rangeZ[1] - params.rangeZ[0];
                        cpXZ[1] += params.rangeZ[0];
                        vec3 cp = vec3(cpXZ[0], height, cpXZ[1]);
                        //
                        uint32_t cpCount = params.controlPointPerPatch[1] * params.controlPointPerPatch[0];
                        uint32_t vIndex = tI * params.vertexPerPatch[0] + sI;
                        uint32_t cpIndex = j * params.controlPointPerPatch[0] + i;
                        uint32_t coeffIndex = vIndex * cpCount + cpIndex;

                        vec3 coeffs = polynomialTable.at(coeffIndex);
                        vertex += cp * coeffs[0];
                        dqdt += cp * coeffs[1];
                        dqds += cp * coeffs[2];
                    }

                uint32_t writeIndex = patchVertexOffset + tI * params.vertexPerPatch[0] + sI;
                vertices[writeIndex] = vec3(vertex);
                normals[writeIndex] = vec3(glm::normalize(glm::cross(dqdt, dqds)));
            }
            // Index calculation
            for(uint32_t tI = 0; tI < (params.vertexPerPatch[1] - 1); tI++)
            for(uint32_t sI = 0; sI < (params.vertexPerPatch[0] - 1); sI++)
            {
                // 0, 1, (n + 1), | 0, (n + 1), n
                uint32_t i = (tI + 0) * params.vertexPerPatch[0] + sI;
                uint32_t n = i + params.vertexPerPatch[0];
                //
                uint32_t writeIndex = patchIndexOffset + (tI * (params.vertexPerPatch[0] - 1) + sI) * 6;

                indices[writeIndex + 0] = patchVertexOffset + i + 0;
                indices[writeIndex + 1] = patchVertexOffset + n + 1;
                indices[writeIndex + 2] = patchVertexOffset + i + 1;
                //
                indices[writeIndex + 3] = patchVertexOffset + i + 0;
                indices[writeIndex + 4] = patchVertexOffset + n + 0;
                indices[writeIndex + 5] = patchVertexOffset + n + 1;

            }
        }
    });
    threadPool.Wait();

    // Get minmax vertex for gradient
    vec2 yMinMax = vec2(FLT_MAX, -FLT_MAX);
    for(vec3& v : vertices)
    {
        yMinMax[0] = std::min(yMinMax[0], v.y);
        yMinMax[1] = std::max(yMinMax[1], v.y);
    }

    TerrainRawData raw{std::move(vertices), std::move(normals), std::move(indices), yMinMax};
    if(!cachePath.empty()) SaveTerrainCache(cachePath, raw);
    return TerrainMesh(std::move(raw.vertices), std::move(raw.normals),
                       std::move(raw.indices), raw.yMinMax);
}

TerrainMesh::TerrainMesh(std::vector<glm::vec3> vertices,
                         std::vector<glm::vec3> normals,
                         std::vector<unsigned int> indices,
                         glm::vec2 yMinMax)
    : yMinMax(yMinMax)
{
    // ===================== //
    //   GEN BUFFER AND VAO  //
    // ===================== //
    // Gen buffer
    std::array<size_t, 3> sizes = {};
    sizes[0] = vertices.size() * sizeof(glm::vec3);
    sizes[1] = normals.size() * sizeof(glm::vec3);
    //
    std::array<size_t, 4> offsets = {};
    offsets[0] = 0;
    for(uint32_t i = 1; i < 4; i++)
    {
        // This may not be necessary, but align the data to 256-byte boundaries
        size_t alignedSize = (sizes[i - 1] + 255) / 256 * 256;
        offsets[i] = offsets[i - 1] + alignedSize;
    }
    glGenBuffers(1, &vBufferId);
    glBindBuffer(GL_ARRAY_BUFFER, vBufferId);
    glBufferStorage(GL_ARRAY_BUFFER, GLintptr(offsets.back()), nullptr, GL_DYNAMIC_STORAGE_BIT);
    // Load the data
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(offsets[0]), GLsizei(sizes[0]), vertices.data());
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(offsets[1]), GLsizei(sizes[1]), normals.data());
    // Indices
    glGenBuffers(1, &iBufferId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iBufferId);
    glBufferStorage(GL_ELEMENT_ARRAY_BUFFER, GLsizei(indices.size() * sizeof(uint32_t)),
                    indices.data(), GL_DYNAMIC_STORAGE_BIT);

    // VAO
    glGenVertexArrays(1, &vaoId);
    glBindVertexArray(vaoId);
    // Pos (tightly packed vec3)
    glBindVertexBuffer(0, vBufferId, GLintptr(offsets[0]), GLsizei(sizeof(glm::vec3)));
    glEnableVertexAttribArray(0);
    glVertexAttribFormat(0, 3, GL_FLOAT, false, 0);
    // Normal (tightly packed vec3)
    glBindVertexBuffer(1, vBufferId, GLintptr(offsets[1]), GLsizei(sizeof(glm::vec3)));
    glEnableVertexAttribArray(1);
    glVertexAttribFormat(1, 3, GL_FLOAT, false, 0);
    //
    glVertexAttribBinding(0, IN_POS);
    glVertexAttribBinding(1, IN_NORMAL);
    // Above API calls are understandable but to use index draw calls
    // we need to bind an element array buffer (aka. index buffer)
    // to make the vao to store indices so that we can call draw elements call
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iBufferId);

    indexCount = uint32_t(indices.size());
    assert(indexCount % 3 == 0);

    glBindVertexArray(0);
}

TerrainMesh& TerrainMesh::operator=(TerrainMesh&& other)
{
    assert(&other != this);
    std::swap(vBufferId, other.vBufferId);
    std::swap(iBufferId, other.iBufferId);
    std::swap(vaoId, other.vaoId);
    std::swap(indexCount, other.indexCount);
    std::swap(yMinMax, other.yMinMax);
    return *this;
}

TerrainMesh::~TerrainMesh()
{
    if(vBufferId) glDeleteBuffers(1, &vBufferId);
    if(iBufferId) glDeleteBuffers(1, &iBufferId);
    if(vaoId) glDeleteVertexArrays(1, &vaoId);
}


void HW1::RecreateHDRFBO(int w, int h)
{
    if(hdrFboId)
    {
        glDeleteFramebuffers(1, &hdrFboId);
        glDeleteTextures(1, &hdrColorTex);
        glDeleteRenderbuffers(1, &hdrDepthRbo);
    }

    glGenTextures(1, &hdrColorTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);

    glGenRenderbuffers(1, &hdrDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, hdrDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &hdrFboId);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFboId);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hdrDepthRbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    hdrFboSize = glm::uvec2(w, h);
}

HW1::~HW1()
{
    if(hdrFboId)      glDeleteFramebuffers(1, &hdrFboId);
    if(hdrColorTex)   glDeleteTextures(1, &hdrColorTex);
    if(hdrDepthRbo)   glDeleteRenderbuffers(1, &hdrDepthRbo);
    if(fullscreenVao) glDeleteVertexArrays(1, &fullscreenVao);
    if(fullscreenVbo) glDeleteBuffers(1, &fullscreenVbo);
    if(fullscreenIbo) glDeleteBuffers(1, &fullscreenIbo);
}

HW1::HW1(ThreadPool& threadPool,
         GLState& state)
    : vShader(ShaderGL::VERTEX, "shaders/terrain.vert")
    , fShader(ShaderGL::FRAGMENT, "shaders/terrain.frag")
    , tonemapVert(ShaderGL::VERTEX, "shaders/tonemap.vert")
    , tonemapFrag(ShaderGL::FRAGMENT, "shaders/tonemap.frag")
    , planeVert(ShaderGL::VERTEX, "shaders/plane.vert")
    , planeFrag(ShaderGL::FRAGMENT, "shaders/plane.frag")
    , skyVert(ShaderGL::VERTEX, "shaders/sky.vert")
    , skyFrag(ShaderGL::FRAGMENT, "shaders/sky.frag")
    , glassFrag(ShaderGL::FRAGMENT, "shaders/glass.frag")
    , waterVert(ShaderGL::VERTEX,   "shaders/water.vert")
    , waterFrag(ShaderGL::FRAGMENT, "shaders/water.frag")
    , planeMeshBody("meshes/plane_body.obj")
    , planeMeshHelix("meshes/plane_helix.obj")
    , planeMeshGlass("meshes/plane_glass.obj")
    , planeMeshCable("meshes/plane_cable.obj")
    , waterMesh(PlaneGenParams{
          .rangeX = glm::vec2(-500.0f, 500.0f),
          .rangeZ = glm::vec2(-500.0f, 500.0f),
          .vertexCount = glm::uvec2(128)
      })
    , planeBaseAlbedo("textures/plane_base_albedo.jpg", TextureGL::LINEAR, TextureGL::REPEAT)
    , planeBaseRoughness("textures/plane_base_roughness.jpg", TextureGL::LINEAR, TextureGL::REPEAT, false)
    , planeHelixAlbedo("textures/plane_helix_albedo.jpg", TextureGL::LINEAR, TextureGL::REPEAT)
    , planeHelixRoughness("textures/plane_helix_roughness.jpg", TextureGL::LINEAR, TextureGL::REPEAT, false)
    , skyHDR("textures/citrus_orchard_puresky_2k.hdr", TextureGL::LINEAR, TextureGL::REPEAT, false)
    , terrainShoreAlbedo("textures/shore_diff_2k.jpg",  TextureGL::LINEAR, TextureGL::REPEAT)
    , terrainShoreRough ("textures/shore_rough_2k.jpg", TextureGL::LINEAR, TextureGL::REPEAT, false)
    , terrainGrassAlbedo("textures/grass_diff_2k.jpg",  TextureGL::LINEAR, TextureGL::REPEAT)
    , terrainGrassRough ("textures/grass_rough_2k.jpg", TextureGL::LINEAR, TextureGL::REPEAT, false)
    , terrainRockAlbedo ("textures/rock_diff_2k.jpg",   TextureGL::LINEAR, TextureGL::REPEAT)
    , terrainRockRough  ("textures/rock_rough_2k.jpg",  TextureGL::LINEAR, TextureGL::REPEAT, false)
    , terrainSnowAlbedo ("textures/snow_diff_2k.jpg",   TextureGL::LINEAR, TextureGL::REPEAT)
    , terrainSnowRough  ("textures/snow_rough_2k.jpg",  TextureGL::LINEAR, TextureGL::REPEAT, false)
    , terrainDTED("geo/n36_e029_1arc_v3.dt2")
    , params
    {
        .rangeX = glm::vec2(-500, 500),
        .rangeY = glm::vec2(-50, 50),
        .rangeZ = glm::vec2(-500, 500),
        //
        .patchStartOffset = glm::uvec2(0),
        .patchCount = glm::uvec2(256),
        // This is ignored on the B-spline code
        .controlPointPerPatch = glm::uvec2(4), // 4 CP is cubic spline
        .vertexPerPatch = glm::uvec2(0),
        .controlPointSkip = glm::uvec2(10)
    }
    , curVertexPerPatch(0)
    , state(state)
    , threadPool(threadPool)
{
    static constexpr float quadVerts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };
    static constexpr unsigned int quadIdx[] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &fullscreenVao);
    glGenBuffers(1, &fullscreenVbo);
    glGenBuffers(1, &fullscreenIbo);

    glBindVertexArray(fullscreenVao);
    glBindBuffer(GL_ARRAY_BUFFER, fullscreenVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fullscreenIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
    glBindVertexArray(0);

    prevTime = glfwGetTime();

    glm::ivec2 fbSize = state.curWndParams.fbSize;
    if(fbSize[0] > 0 && fbSize[1] > 0)
        RecreateHDRFBO(fbSize[0], fbSize[1]);
}

void HW1::DrawPlane(const glm::mat4x4& view, const glm::mat4x4& proj, const glm::vec3& camPos)
{
    static constexpr GLuint U_MODEL   = 0;
    static constexpr GLuint U_VIEW    = 1;
    static constexpr GLuint U_PROJ    = 2;
    static constexpr GLuint U_NORMAL  = 3;
    static constexpr GLuint U_LIGHT   = 2;
    static constexpr GLuint U_CAM_POS = 3;

    glm::mat4x4 planeWorld = glm::translate(glm::identity<glm::mat4x4>(), state.planePos)
                           * glm::toMat4(state.planeRot);

    glUseProgramStages(state.renderPipeline, GL_VERTEX_SHADER_BIT,   planeVert.shaderId);
    glUseProgramStages(state.renderPipeline, GL_FRAGMENT_SHADER_BIT, planeFrag.shaderId);

    // Bind sky HDR at unit 1 for IBL diffuse in plane.frag
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, skyHDR.textureId);
    glActiveTexture(GL_TEXTURE0);

    auto drawComponent = [&](const MeshGL& mesh, const glm::mat4x4& localTransform)
    {
        glm::mat4x4 model     = planeWorld * localTransform;
        glm::mat3x3 normalMat = glm::inverseTranspose(glm::mat3(model));
        glActiveShaderProgram(state.renderPipeline, planeVert.shaderId);
        glUniformMatrix4fv(U_MODEL,  1, false, glm::value_ptr(model));
        glUniformMatrix4fv(U_VIEW,   1, false, glm::value_ptr(view));
        glUniformMatrix4fv(U_PROJ,   1, false, glm::value_ptr(proj));
        glUniformMatrix3fv(U_NORMAL, 1, false, glm::value_ptr(normalMat));
        glActiveShaderProgram(state.renderPipeline, planeFrag.shaderId);
        glUniform3fv(U_LIGHT,   1, glm::value_ptr(state.lightPos));
        glUniform3fv(U_CAM_POS, 1, glm::value_ptr(camPos));
        glBindVertexArray(mesh.vaoId);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    };

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, planeBaseAlbedo.textureId);
    drawComponent(planeMeshBody, glm::identity<glm::mat4x4>());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, planeHelixAlbedo.textureId);
    drawComponent(planeMeshHelix, glm::translate(glm::identity<glm::mat4x4>(), glm::vec3(0.0f, 0.0f, 13.719f))
                                * glm::rotate(glm::identity<glm::mat4x4>(), helixAngle, glm::vec3(0.0f, 0.0f, 1.0f)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, planeBaseAlbedo.textureId);
    drawComponent(planeMeshCable, glm::translate(glm::identity<glm::mat4x4>(), glm::vec3(0.0f, 3.644f, -10.638f)));

    // Cockpit glass: HDR reflection with Fresnel
    glUseProgramStages(state.renderPipeline, GL_FRAGMENT_SHADER_BIT, glassFrag.shaderId);
    {
        glm::mat4x4 localT    = glm::translate(glm::identity<glm::mat4x4>(), glm::vec3(0.0f, 2.733f, -1.489f));
        glm::mat4x4 model     = planeWorld * localT;
        glm::mat3x3 normalMat = glm::inverseTranspose(glm::mat3(model));
        glActiveShaderProgram(state.renderPipeline, planeVert.shaderId);
        glUniformMatrix4fv(U_MODEL,  1, false, glm::value_ptr(model));
        glUniformMatrix4fv(U_VIEW,   1, false, glm::value_ptr(view));
        glUniformMatrix4fv(U_PROJ,   1, false, glm::value_ptr(proj));
        glUniformMatrix3fv(U_NORMAL, 1, false, glm::value_ptr(normalMat));
        glActiveShaderProgram(state.renderPipeline, glassFrag.shaderId);
        glUniform3fv(2, 1, glm::value_ptr(camPos));  // location 2 = camPos in glass.frag
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, skyHDR.textureId);
        glBindVertexArray(planeMeshGlass.vaoId);
        glDrawElements(GL_TRIANGLES, planeMeshGlass.indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }
}

void HW1::Work()
{
    if(params.vertexPerPatch != glm::uvec2(state.vertexPerPatch))
    {
        params.vertexPerPatch = glm::uvec2(state.vertexPerPatch);

        std::string cachePath = "geo/cache/terrain_"
                              + std::to_string(params.vertexPerPatch[0]) + "x"
                              + std::to_string(params.vertexPerPatch[1])
                              + "_r" + std::to_string(int(params.rangeX[1]))
                              + "_" + std::to_string(int(params.rangeY[1]))
                              + ".bin";

        if(std::ifstream(cachePath))
            terrainMesh = LoadTerrainCache(cachePath);
        else
            terrainMesh = GenerateTerrainBSpline(threadPool, terrainDTED, params, cachePath);
    }

    // Delta time + plane movement
    double currentTime = glfwGetTime();
    float dt = float(currentTime - prevTime);
    prevTime = currentTime;

    glm::vec3 planeForward = state.planeRot * glm::vec3(0, 0, 1);
    state.planePos += planeForward * state.planeSpeed * dt;
    helixAngle += dt * 10.0f;

    // Object-common matrices
    float aspectRatio = float(state.curWndParams.fbSize[0]) / float(state.curWndParams.fbSize[1]);
    glm::mat4x4 proj = glm::perspective(glm::radians(50.0f), aspectRatio,
                                        2.0f, 5000.0f);

    // Orbit camera: position around plane via yaw/pitch/distance
    float az = state.orbitCam.yaw;
    float el = state.orbitCam.pitch;
    float d  = state.orbitCam.distance;
    glm::vec3 cameraPos = state.planePos + glm::vec3(d * std::cos(el) * std::sin(az),
                                                     d * std::sin(el),
                                                    -d * std::cos(el) * std::cos(az));
    glm::mat4x4 view = glm::lookAt(cameraPos, state.planePos, glm::vec3(0, 1, 0));

    // Resize HDR FBO if window size changed
    {
        glm::uvec2 newSize(state.curWndParams.fbSize[0], state.curWndParams.fbSize[1]);
        if(newSize != hdrFboSize && newSize[0] > 0 && newSize[1] > 0)
            RecreateHDRFBO(int(newSize[0]), int(newSize[1]));
    }

    // HDR FBO pass
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFboId);
    glViewport(0, 0,
               state.curWndParams.fbSize[0],
               state.curWndParams.fbSize[1]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Sky: draw first, no depth writes so terrain/plane overwrite it
    {
        static constexpr GLuint U_INV_PROJ     = 0;
        static constexpr GLuint U_INV_VIEW_ROT = 1;

        glm::mat4x4 invProj    = glm::inverse(proj);
        glm::mat3x3 invViewRot = glm::transpose(glm::mat3(view)); // view rotation is orthonormal

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glUseProgramStages(state.renderPipeline, GL_VERTEX_SHADER_BIT,   skyVert.shaderId);
        glUseProgramStages(state.renderPipeline, GL_FRAGMENT_SHADER_BIT, skyFrag.shaderId);
        glActiveShaderProgram(state.renderPipeline, skyFrag.shaderId);
        glUniformMatrix4fv(U_INV_PROJ,     1, false, glm::value_ptr(invProj));
        glUniformMatrix3fv(U_INV_VIEW_ROT, 1, false, glm::value_ptr(invViewRot));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, skyHDR.textureId);

        glBindVertexArray(fullscreenVao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }

    glEnable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, state.wireframe ? GL_LINE : GL_FILL);

    // Bind shaders and related uniforms / textures
    // Move this somewhere proper later.
    // These must match the uniform "location" at the shader(s).
    // Vertex
    static constexpr GLuint U_TRANSFORM_MODEL = 0;
    static constexpr GLuint U_TRANSFORM_VIEW = 1;
    static constexpr GLuint U_TRANSFORM_PROJ = 2;
    static constexpr GLuint U_TRANSFORM_NORMAL = 3;
    static constexpr GLuint U_VERTEX_HEIGHT_SCALE = 4;
    // Fragment
    static constexpr GLuint U_MODE      = 0;
    static constexpr GLuint U_RANGE     = 1;
    static constexpr GLuint U_LIGHT_POS = 2;
    static constexpr GLuint U_TEX_SCALE = 3;

    // glActiveShaderProgram makes "glUniform" family of commands
    // to effect the selected shader
    glUseProgramStages(state.renderPipeline, GL_VERTEX_SHADER_BIT, vShader.shaderId);
    glActiveShaderProgram(state.renderPipeline, vShader.shaderId);
    {
        // Don't move the triangle
        glm::mat4x4 model = glm::identity<glm::mat4x4>();
        // Normal local->world matrix of the object
        glm::mat3x3 normalMatrix = glm::inverseTranspose(model);
        glUniformMatrix4fv(U_TRANSFORM_MODEL, 1, false, glm::value_ptr(model));
        glUniformMatrix4fv(U_TRANSFORM_VIEW, 1, false, glm::value_ptr(view));
        glUniformMatrix4fv(U_TRANSFORM_PROJ, 1, false, glm::value_ptr(proj));
        glUniformMatrix3fv(U_TRANSFORM_NORMAL, 1, false, glm::value_ptr(normalMatrix));
        glUniform1f(U_VERTEX_HEIGHT_SCALE, state.heightScale);
    }
    // Bind terrain albedo textures: shore=0, grass=1, rock=2, snow=3
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, terrainShoreAlbedo.textureId);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, terrainGrassAlbedo.textureId);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, terrainRockAlbedo.textureId);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, terrainSnowAlbedo.textureId);

    // Fragment shader
    glUseProgramStages(state.renderPipeline, GL_FRAGMENT_SHADER_BIT, fShader.shaderId);
    glActiveShaderProgram(state.renderPipeline, fShader.shaderId);
    {
        glUniform1ui(U_MODE,      state.mode);
        glUniform2fv(U_RANGE,     1, glm::value_ptr(terrainMesh.yMinMax));
        glUniform3fv(U_LIGHT_POS, 1, glm::value_ptr(state.lightPos));
        glUniform1f (U_TEX_SCALE, 1.0f / 25.0f);
    }
    // Bind VAO
    glBindVertexArray(terrainMesh.vaoId);
    // Draw call!
    glDrawElements(GL_TRIANGLES, terrainMesh.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Water plane: cosine wave animation, HDR reflection
    {
        static constexpr GLuint WU_MODEL   = 0;
        static constexpr GLuint WU_VIEW    = 1;
        static constexpr GLuint WU_PROJ    = 2;
        static constexpr GLuint WU_NORMAL  = 3;
        static constexpr GLuint WU_TIME    = 4;
        static constexpr GLuint WU_CAM_POS = 2;

        glm::mat4x4 waterModel = glm::translate(glm::identity<glm::mat4x4>(),
                                                glm::vec3(0.0f, -30.0f, 0.0f));
        glm::mat3x3 waterNormal = glm::inverseTranspose(glm::mat3(waterModel));

        glUseProgramStages(state.renderPipeline, GL_VERTEX_SHADER_BIT,   waterVert.shaderId);
        glUseProgramStages(state.renderPipeline, GL_FRAGMENT_SHADER_BIT, waterFrag.shaderId);

        glActiveShaderProgram(state.renderPipeline, waterVert.shaderId);
        glUniformMatrix4fv(WU_MODEL,  1, false, glm::value_ptr(waterModel));
        glUniformMatrix4fv(WU_VIEW,   1, false, glm::value_ptr(view));
        glUniformMatrix4fv(WU_PROJ,   1, false, glm::value_ptr(proj));
        glUniformMatrix3fv(WU_NORMAL, 1, false, glm::value_ptr(waterNormal));
        glUniform1f(WU_TIME, float(currentTime));

        glActiveShaderProgram(state.renderPipeline, waterFrag.shaderId);
        glUniform3fv(WU_CAM_POS, 1, glm::value_ptr(cameraPos));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, skyHDR.textureId);

        glBindVertexArray(waterMesh.vaoId);
        glDrawElements(GL_TRIANGLES, waterMesh.indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    DrawPlane(view, proj, cameraPos);

    // Unbind HDR FBO, generate mipmaps to compute avg log luminance in 1x1 mip alpha
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColorTex);
    glGenerateMipmap(GL_TEXTURE_2D);

    // Tonemap pass: Reinhard extended, writes to default FBO
    static constexpr GLuint U_MIDDLE_GRAY = 0;
    static constexpr GLuint U_LWHITE      = 1;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glUseProgramStages(state.renderPipeline, GL_VERTEX_SHADER_BIT,   tonemapVert.shaderId);
    glUseProgramStages(state.renderPipeline, GL_FRAGMENT_SHADER_BIT, tonemapFrag.shaderId);
    glActiveShaderProgram(state.renderPipeline, tonemapFrag.shaderId);
    glUniform1f(U_MIDDLE_GRAY, middle_gray);
    glUniform1f(U_LWHITE, LWhite);

    glBindVertexArray(fullscreenVao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}
