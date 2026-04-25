#include "utility.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <glm/glm.hpp>

#include <cstdio>
#include <cstdlib>
#include <bit>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <charconv>
#include <array>

void SetupGLFWErrorCallback();
void SetupOpenGLErrorCallback();
void APIENTRY PrintOpenGLError(GLenum, GLenum, GLuint, GLenum,
                               GLsizei, const GLchar*, const void*);

void SetupGLFWCallbacks(GLFWwindow* w, const CallbackPointersGLFW& cb)
{
    if(cb.mMoveCallback) glfwSetCursorPosCallback(w, cb.mMoveCallback);
    if(cb.mButtonCallback) glfwSetMouseButtonCallback(w, cb.mButtonCallback);
    if(cb.mScrollCallback) glfwSetScrollCallback(w, cb.mScrollCallback);
    if(cb.fboCallback) glfwSetFramebufferSizeCallback(w, cb.fboCallback);
    if(cb.keyCallback) glfwSetKeyCallback(w, cb.keyCallback);
    if(cb.winPosCallback) glfwSetWindowPosCallback(w, cb.winPosCallback);
    if(cb.winSizeCallback) glfwSetWindowSizeCallback(w, cb.winSizeCallback);
}

GLState::GLState(const char* const windowName,
                 int w, int h,
                 CallbackPointersGLFW callbacks)
    : curWndParams
    {
        .pos    = glm::ivec2(0),
        .size   = glm::ivec2(0),
        .fbSize = glm::ivec2(w, h)
    }
{
    if(!glfwInit())
    {
        const char* err; glfwGetError(&err);
        std::fprintf(stderr,
                     "Unable to init GLFW!\n"
                     "Reason: %s\n"
                     "Terminating...\n",
                     err);
        std::exit(EXIT_FAILURE);
    }
    SetupGLFWErrorCallback();

    // Misc.
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GL_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GL_TRUE);
    glfwWindowHint(GLFW_REFRESH_RATE, GLFW_DONT_CARE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    // OGL Context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    // Set Debug Context for error reporting
    // Hopefully it will have minimal performance impact
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    //
    // Render buffer
    glfwWindowHint(GLFW_RED_BITS, 8); glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8); glfwWindowHint(GLFW_ALPHA_BITS, 8);
    // Depth buffer
    glfwWindowHint(GLFW_DEPTH_BITS, 24); glfwWindowHint(GLFW_STENCIL_BITS, 8);
    //

    window = glfwCreateWindow(int(w), int(h), windowName,
                              NULL, NULL);
    if(!window)
    {
        // Since the callback is set it should've printed the error
        // terminate the system
        glfwTerminate();
        std::exit(EXIT_FAILURE);
    }
    glfwGetWindowPos(window, &curWndParams.pos[0], &curWndParams.pos[1]);
    glfwGetWindowSize(window, &curWndParams.size[0], &curWndParams.size[1]);

    // Register callbacks
    SetupGLFWCallbacks(window, callbacks);
    // Give this class as pointer to the windowing system
    // This is safe since GLState cannot be move/copied etc.
    glfwSetWindowUserPointer(window, this);

    // Make the OGL context current for this window
    // After this call, all OGL APIs will act on this window
    glfwMakeContextCurrent(window);

    // Force vsync
    glfwSwapInterval(1);

    // Now we can load the OGL functions, function that loads OGL
    // functions is
    if(!gladLoadGL())
    {
        std::fprintf(stderr, "Unable to load OpenGL functions!");
        std::exit(EXIT_FAILURE);
    }

    // Now setup the OGL error callback
    SetupOpenGLErrorCallback();

    // Print OGL Status, this may be important.
    // When on laptop window may be using integrated GPU or
    // wrong version of OpenGL generated etc.
    std::printf("Window Initialized.\n");
    std::printf("GLFW   : %s\n", glfwGetVersionString());
    std::printf("\n");
    std::printf("Renderer Information...\n");
    std::printf("OpenGL : %s\n", glGetString(GL_VERSION));
    std::printf("GLSL   : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    std::printf("Device : %s\n", glGetString(GL_RENDERER));
    std::printf("\n");

    // Create shader pipeline
    glGenProgramPipelines(1, &renderPipeline);
    glBindProgramPipeline(renderPipeline);

    // All done! Happy rendering.
}

GLState::~GLState()
{
    if(renderPipeline) glDeleteProgramPipelines(1, &renderPipeline);
    if(window) glfwDestroyWindow(window);
    glfwTerminate();
}

ShaderGL::ShaderGL(Type t, const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if(!file)
    {
        std::printf("Unable to open shader file at \"%s\".\n",
                    path.c_str());
        std::exit(EXIT_FAILURE);
    }
    GLint sourceSize = GLint(file.seekg(0, std::ios::end).tellg());
    std::vector<GLchar> source(size_t(sourceSize + 1), '\0');
    file.seekg(0, std::ios::beg);
    file.read(source.data(), sourceSize);

    // Create temporary shader
    GLuint shaderGL = glCreateShader(t);
    GLchar* srcPtr = source.data();
    glShaderSource(shaderGL, 1, &srcPtr, &sourceSize);
    glCompileShader(shaderGL);
    GLint isCompiled = GL_FALSE;
    glGetShaderiv(shaderGL, GL_COMPILE_STATUS, &isCompiled);
    if(isCompiled == GL_FALSE)
    {
        // We do not need to print the compilation error
        // OpenGL debug callback will automatically handle it
        std::fprintf(stderr, "Unable to compile shader \"%s\"\n",
                     path.c_str());

        GLint errLen = 0;
        glGetShaderiv(shaderGL, GL_INFO_LOG_LENGTH, &errLen);
        std::vector<char> errLog(size_t(errLen + 1), '\0');
        glGetShaderInfoLog(shaderGL, errLen, &errLen, errLog.data());

        // Use our own print here
        PrintOpenGLError(GL_DEBUG_SOURCE_SHADER_COMPILER,
                         GL_DEBUG_TYPE_ERROR, 0,
                         GL_DEBUG_SEVERITY_HIGH,
                         errLen, errLog.data(), nullptr);

        std::exit(EXIT_FAILURE);
    }

    // Attach this to openGL "program"
    // (which represents entirity of the programmable rasterizer pipeline)
    shaderId = glCreateProgram();
    glProgramParameteri(shaderId, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(shaderId, shaderGL);
    glLinkProgram(shaderId);
    GLint isLinked = GL_FALSE;
    glGetProgramiv(shaderId, GL_LINK_STATUS, &isLinked);
    if(isLinked == GL_FALSE)
    {
        // We do not need to print the compilation error
        // OpenGL debug callback will automatically handle it
        std::fprintf(stderr, "Unable to link shader \"%s\"\n",
                     path.c_str());

        GLint errLen = 0;
        glGetProgramiv(shaderId, GL_INFO_LOG_LENGTH, &errLen);
        std::vector<char> errLog(size_t(errLen + 1), '\0');
        glGetProgramInfoLog(shaderId, errLen, &errLen, errLog.data());
        // Use our own print here
        PrintOpenGLError(GL_DEBUG_SOURCE_SHADER_COMPILER,
                         GL_DEBUG_TYPE_ERROR, 0,
                         GL_DEBUG_SEVERITY_HIGH,
                         errLen, errLog.data(), nullptr);
        std::exit(EXIT_FAILURE);
    }
    // After linking, we can detach the shader.
    // Actual compiled assembly will be stayed inside the pipeline (program).
    glDetachShader(shaderId, shaderGL);
    glDeleteShader(shaderGL);

    static const char* const VertexStr = "Vertex";
    static const char* const FragmentStr = "Fragment";
    const char* shaderTypeStr = nullptr;
    switch(t)
    {
        case ShaderGL::VERTEX:      shaderTypeStr = VertexStr; break;
        case ShaderGL::FRAGMENT:    shaderTypeStr = FragmentStr; break;
        default:
        {
            std::fprintf(stderr, "Unkown Shader Type while compiling \"%s\"!",
                         path.c_str());
            std::exit(EXIT_FAILURE);
        }
    }
    std::printf("%s Shader \"%s\" is compiled succesfully.\n",
                shaderTypeStr, path.c_str());
}

// For mesh multiple index hashing
struct ObjKeyType
{
    uint32_t posIndex;
    uint32_t uvIndex;
    uint32_t normalIndex;

    //auto operator==(const ObjKeyType& other) const
    //{
    //    return (posIndex == other.posIndex &&
    //            uvIndex == other.uvIndex &&
    //            normalIndex == other.normalIndex);
    //}

    auto operator<=>(const ObjKeyType&) const = default;
};

template<>
struct std::hash<ObjKeyType>
{
    std::uint64_t operator()(const ObjKeyType& k) const
    {
        return (k.posIndex * 7741ull +
                k.normalIndex * 5113ull +
                k.uvIndex * 9157ull);
    }
};

MeshGL::MeshGL(const std::string& objPath)
{
    // ===================== //
    //  PARSE WAVEFRONT OBJ  //
    // ===================== //
    std::from_chars_result result;
    std::ifstream file(objPath);
    if(!file)
    {
        std::fprintf(stderr, "Unable to open obj file \"%s\"\n",
                     objPath.c_str());
        std::exit(EXIT_FAILURE);
    }
    //
    std::vector<glm::vec3> positions; positions.reserve(512);
    std::vector<glm::vec3> normals;   normals.reserve(512);
    std::vector<glm::vec2> uvs;       uvs.reserve(512);
    std::vector<uint32_t> indices;    indices.reserve(512);
    //
    std::unordered_map<ObjKeyType, uint32_t> indexHashes;
    indexHashes.reserve(1024 * 1024);

    // Only find the index stuff
    uint32_t indexCounter = 0;
    std::string line;
    while(std::getline(file, line))
    {
        if(line.starts_with("v "))
        {
            const char* ptr = line.data();
            size_t s0 = line.find_first_of(' ') + 1;
            size_t s1 = line.find_first_of(' ', s0) + 1;
            size_t s2 = line.find_first_of(' ', s1) + 1;
            size_t s3 = line.size();

            glm::vec3 pos;
            result = std::from_chars(ptr + s0, ptr + s1, pos[0]); assert(result.ec == std::errc());
            result = std::from_chars(ptr + s1, ptr + s2, pos[1]); assert(result.ec == std::errc());
            result = std::from_chars(ptr + s2, ptr + s3, pos[2]); assert(result.ec == std::errc());
            positions.push_back(pos);
        }
        else if(line.starts_with("f "))
        {
            std::string_view lView = line;
            auto ParseTriplet = [&](size_t start, size_t end)
            {
                std::string_view localView = lView.substr(start, end - start);
                const char* ptr = localView.data();
                size_t s0 = 0;
                size_t s1 = localView.find_first_of('/', s0) + 1;
                size_t s2 = localView.find_first_of('/', s1) + 1;
                size_t s3 = end - start;
                uint32_t pId = std::numeric_limits<uint32_t>::max();
                uint32_t uvId = std::numeric_limits<uint32_t>::max();
                uint32_t nId = std::numeric_limits<uint32_t>::max();
                // Pos
                {
                    result = std::from_chars(ptr + s0, ptr + s1, pId);
                    assert(result.ec == std::errc());
                }
                // These two are optional
                // UV
                if(s1 != s2)
                {
                    result = std::from_chars(ptr + s1, ptr + s2, uvId);
                    assert(result.ec == std::errc());
                }
                // Normal
                if(s1 != s3)
                {
                    result = std::from_chars(ptr + s2, ptr + s3, nId);
                    assert(result.ec == std::errc());
                }
                return ObjKeyType
                {
                    // Data of OBJ file is 1-indexed, so convert.
                    .posIndex = pId - 1,
                    .uvIndex = uvId - 1,
                    .normalIndex = nId - 1
                };
            };

            size_t s0 = line.find_first_of(' ') + 1;
            size_t s1 = line.find_first_of(' ', s0) + 1;
            size_t s2 = line.find_first_of(' ', s1) + 1;
            size_t s3 = line.size();
            auto pack0 = ParseTriplet(s0, s1);
            auto pack1 = ParseTriplet(s1, s2);
            auto pack2 = ParseTriplet(s2, s3);

            auto insertR = indexHashes.emplace(pack0, indexCounter);
            if(insertR.second) indexCounter++;
            indices.push_back(insertR.first->second);

            insertR = indexHashes.emplace(pack1, indexCounter);
            if(insertR.second) indexCounter++;
            indices.push_back(insertR.first->second);

            insertR = indexHashes.emplace(pack2, indexCounter);
            if(insertR.second) indexCounter++;
            indices.push_back(insertR.first->second);
        }
        else if(line.starts_with("vt "))
        {
            const char* ptr = line.data();
            size_t s0 = line.find_first_of(' ') + 1;
            size_t s1 = line.find_first_of(' ', s0) + 1;
            size_t s2 = line.size();

            glm::vec2 uv;
            result = std::from_chars(ptr + s0, ptr + s1, uv[0]); assert(result.ec == std::errc());
            result = std::from_chars(ptr + s1, ptr + s2, uv[1]); assert(result.ec == std::errc());
            uvs.push_back(uv);
        }
        else if(line.starts_with("vn "))
        {
            const char* ptr = line.data();
            size_t s0 = line.find_first_of(' ') + 1;
            size_t s1 = line.find_first_of(' ', s0) + 1;
            size_t s2 = line.find_first_of(' ', s1) + 1;
            size_t s3 = line.size();

            glm::vec3 normal;
            result = std::from_chars(ptr + s0, ptr + s1, normal[0]); assert(result.ec == std::errc());
            result = std::from_chars(ptr + s1, ptr + s2, normal[1]); assert(result.ec == std::errc());
            result = std::from_chars(ptr + s2, ptr + s3, normal[2]); assert(result.ec == std::errc());
            normals.push_back(normal);
        }
    }
    // Convert the data to single indexed mode
    bool warnNormalsZero = false;
    bool warnUVsZero = false;
    std::vector<glm::vec3> linPositions; linPositions.resize(indexHashes.size());
    std::vector<glm::vec3> linNormals;   linNormals.resize(indexHashes.size());
    std::vector<glm::vec2> linUVs;       linUVs.resize(indexHashes.size());
    for(const auto& entry : indexHashes)
    {
        uint32_t i = entry.second;
        linPositions[i] = positions[entry.first.posIndex];
        // If these are not available just write zero
        if(entry.first.uvIndex != std::numeric_limits<uint32_t>::max())
            linUVs[i] = uvs[entry.first.uvIndex];
        else
        {
            warnUVsZero = true;
            linUVs[i] = glm::vec2(0);
        }
        //
        if(entry.first.uvIndex != std::numeric_limits<uint32_t>::max())
            linNormals[i] = normals[entry.first.normalIndex];
        else
        {
            warnNormalsZero = true;
            linNormals[i] = glm::vec3(0);
        }
    }
    // Delete tmp buffers
    positions = std::vector<glm::vec3>();
    normals = std::vector<glm::vec3>();
    uvs = std::vector<glm::vec2>();

    if(warnNormalsZero)
        std::printf("[WARNING]: Obj file \"%s\" has some of its "
                    "normals are not present. These are written as zero!\n",
                    objPath.c_str());
    if(warnUVsZero)
        std::printf("[WARNING]: Obj file \"%s\" has some of its "
                    "uvs are not present. These are written as zero!\n",
                    objPath.c_str());

    // ===================== //
    //   GEN BUFFER AND VAO  //
    // ===================== //
    // Gen buffer
    std::array<size_t, 3> sizes = {};
    sizes[0] = linPositions.size() * sizeof(glm::vec3);
    sizes[1] = linNormals.size() * sizeof(glm::vec3);
    sizes[2] = linUVs.size() * sizeof(glm::vec2);
    //
    std::array<size_t, 4> offsets = {};
    offsets[0] = 0;
    for(uint32_t i = 1; i < 4; i++)
    {
        // This may not be necessary, but align the data to 256-byte boundaries
        size_t alignedSize = (sizes[i - 1] + 255) / 256 * 256;
        offsets[i] = offsets[i - 1] + alignedSize;
    }

    // Vertices
    glGenBuffers(1, &vBufferId);
    glBindBuffer(GL_ARRAY_BUFFER, vBufferId);
    glBufferStorage(GL_ARRAY_BUFFER, GLintptr(offsets.back()), nullptr, GL_DYNAMIC_STORAGE_BIT);
    // Load the data
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(offsets[0]), GLsizei(sizes[0]), linPositions.data());
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(offsets[1]), GLsizei(sizes[1]), linNormals.data());
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(offsets[2]), GLsizei(sizes[2]), linUVs.data());
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

    // UV (tightly packed vec2)
    glBindVertexBuffer(2, vBufferId, GLintptr(offsets[2]), GLsizei(sizeof(glm::vec2)));
    glEnableVertexAttribArray(2);
    glVertexAttribFormat(2, 2, GL_FLOAT, false, 0);

    glVertexAttribBinding(0, IN_POS);
    glVertexAttribBinding(1, IN_NORMAL);
    glVertexAttribBinding(2, IN_UV);
    // Above API calls are understandable but to use index draw calls
    // we need to bind an element array buffer (aka. index buffer)
    // to make the vao to store indices so that we can call draw elements call
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iBufferId);

    std::printf("Obj file \"%s\" is loaded succesfully.\n",
                objPath.c_str());

    indexCount = uint32_t(indices.size());
    assert(indexCount % 3 == 0);
}

TextureGL::TextureGL(const std::string& texPath,
                     SampleMode sampleMode, EdgeResolve edgeResolveMode,
                     bool doLinearize)
{
    stbi_set_flip_vertically_on_load(1);
    std::FILE* f = fopen(texPath.c_str(), "rb");
    if(!f)
    {
        std::fprintf(stderr, "Unable to open image \"%s\"\n", texPath.c_str());
        std::exit(EXIT_FAILURE);
    }
    //
    bool is16Bit = stbi_is_16_bit_from_file(f);
    bool isHDR   = stbi_is_hdr_from_file(f);

    void* rawPixels = nullptr;
         if(is16Bit) rawPixels = stbi_load_from_file_16(f, &width, &height, &channelCount, 0);
    else if(isHDR)   rawPixels = stbi_loadf_from_file(f, &width, &height, &channelCount, 0);
    else             rawPixels = stbi_load_from_file(f, &width, &height, &channelCount, 0);
    //
    if(!rawPixels)
    {
        std::fprintf(stderr, "Unable to read image \"%s\"\n", texPath.c_str());
        std::exit(EXIT_FAILURE);
    }

    // Mipmap count calculation
    uint32_t mipCount = uint32_t(std::max(width, height));
    mipCount = (sizeof(GLsizei) * CHAR_BIT) - uint32_t(std::countl_zero(mipCount));
    //
    GLenum internalFormatSized = 0;
    GLenum internalFormat = 0;
    GLenum pixType = (is16Bit) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
           pixType = (isHDR)   ? GL_FLOAT : pixType;
    switch(channelCount)
    {
        case 1:
            internalFormatSized = (is16Bit) ? GL_R16 : GL_R8;
            internalFormatSized = (isHDR) ? GL_R32F : internalFormatSized;
            internalFormat = GL_RED;
            break;
        case 2:
            internalFormatSized = (is16Bit) ? GL_RG16 : GL_RG8;
            internalFormatSized = (isHDR) ? GL_RG32F : internalFormatSized;
            internalFormat = GL_RG;
            break;
        case 3:
            internalFormatSized = (is16Bit) ? GL_RGB16 : GL_RGB8;
            internalFormatSized = (isHDR) ? GL_RGB32F : internalFormatSized;
            internalFormat = GL_RGB;
            break;
        case 4:
            internalFormatSized = (is16Bit) ? GL_RGBA16 : GL_RGBA8;
            internalFormatSized = (isHDR) ? GL_RGBA32F : internalFormatSized;
            internalFormat = GL_RGBA;
            break;
        default:
        {
            stbi_image_free(rawPixels);
            std::fprintf(stderr, "Unkown image type!\n");
            std::exit(EXIT_FAILURE);
        }
    }

    if(doLinearize && !isHDR)
    {
        static auto GammaCorrect = [](auto pix, float factor)
        {
            using T = decltype(pix);
            float linVal = pow(float(pix) / factor, 2.2f);
            return T(std::round(linVal * factor));
        };

        uint8_t* pixels = static_cast<uint8_t*>(rawPixels);
        for(int j = 0; j < height; j++)
        for(int i = 0; i < width; i++)
        for(int c = 0; c < channelCount; c++)
        {
            int linIndex = (j * width + i) * channelCount;
            if(is16Bit)
            {
                uint16_t* pixels = static_cast<uint16_t*>(rawPixels);
                pixels[linIndex + c] = GammaCorrect(pixels[linIndex + c], 65535.0f);
            }
            else
            {
                uint8_t* pixels = static_cast<uint8_t*>(rawPixels);
                pixels[linIndex + c] = GammaCorrect(pixels[linIndex + c], 255.0f);
            }
        }
    }


    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexStorage2D(GL_TEXTURE_2D, GLsizei(mipCount), internalFormatSized, width, height);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, internalFormat,
                    pixType, rawPixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, edgeResolveMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, edgeResolveMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampleMode);
    if(sampleMode == NEAREST)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(rawPixels);
}

GeoDataDTED::GeoDataDTED(const std::string& fName)
{
    // Helper Functions
    static constexpr double MINUTE_FACTOR = double(1) / double(60);
    static constexpr double SECOND_FACTOR = MINUTE_FACTOR * (double(1) / double(60));
    static auto ASCIIToInt = [](std::byte numChar)
    {
        return uint32_t(numChar) - 48;
    };
    static auto ParseLatLon = [](std::ifstream& file)
    {
        std::array<std::byte, 8> encoded;
        file.read(reinterpret_cast<char*>(encoded.data()), 8);
        //
        uint32_t degrees = (ASCIIToInt(encoded[2]) * 1 +
                            ASCIIToInt(encoded[1]) * 10 +
                            ASCIIToInt(encoded[0]) * 100);
        uint32_t minutes = (ASCIIToInt(encoded[4]) * 1 +
                            ASCIIToInt(encoded[3]) * 10);
        uint32_t seconds = (ASCIIToInt(encoded[6]) * 1 +
                            ASCIIToInt(encoded[5]) * 10);
        // Add values in inverse order from small to large to get somewhat
        // higher precision maybe?
        double result = 0.0;
        result += double(seconds) * SECOND_FACTOR;
        result += double(minutes) * MINUTE_FACTOR;
        result += double(degrees);

        char hemisphere = char(encoded[7]);
        if(hemisphere == 'S' || hemisphere == 'W')
            result *= double(-1);
        return result;
    };
    static auto ReadBCD4Digit = [](std::ifstream& file)
    {
        std::array<std::byte, 4> encoded;
        file.read(reinterpret_cast<char*>(encoded.data()), 4);
        uint32_t result = (ASCIIToInt(encoded[3]) * 1 +
                           ASCIIToInt(encoded[2]) * 10 +
                           ASCIIToInt(encoded[1]) * 100 +
                           ASCIIToInt(encoded[0]) * 1000);
        return result;
    };

    std::ifstream file(fName, std::ios::binary);
    if(!file)
    {
        std::fprintf(stderr, "Unable to open DTED file \"%s\"\n",
                     fName.c_str());
        std::exit(EXIT_FAILURE);
    }

    // Implementation of the specification below
    // https://everyspec.com/MIL-PRF/MIL-PRF-080000-99999/MIL-PRF-89020B_25316/
    //
    // Section 3.9.2
    // DTED file consists of
    //  - User Header Label (UHL)
    //  - Data Set Identification Label (DSI)
    //  - Accuracy Description Record (ACC)
    //  - Series of data records
    //
    // Data records are 16-bit integer in meters in BIG ENDIAN format
    //
    // Section 3.12.c | User Header Label (UHL)
    std::array<char, 4> fourCC;
    file.read(fourCC.data(), 4); fourCC.back() = '\0';
    if(std::strncmp(fourCC.data(), "UHL", 3))
    {
        std::fprintf(stderr, "Wrong FOURCC Code in DTED file \"%s\". "
                     "Was %s, should be \"UHL\"\n",
                     fName.c_str(), fourCC.data());
        std::exit(EXIT_FAILURE);
    }
    // Next 8 bytes is bottom left (South West) longitude
    // It has a binary coded decimal structure.
    lonRange[0] = ParseLatLon(file); // First is Lon
    latRange[0] = ParseLatLon(file); // Second is Lat
    double lonInterval = double(ReadBCD4Digit(file)) * SECOND_FACTOR;
    double latInterval = double(ReadBCD4Digit(file)) * SECOND_FACTOR;
    // Read count parameters
    static constexpr size_t LAT_LON_COUNT_START = 47;
    file.seekg(LAT_LON_COUNT_START);
    dimensions[1] = ReadBCD4Digit(file);
    dimensions[0] = ReadBCD4Digit(file);
    // Finalize the extents in (lon,lat)
    latRange[1] = latRange[0] + double(dimensions[0] - 1) * latInterval;
    lonRange[1] = lonRange[0] + double(dimensions[1] - 1) * lonInterval;
    // We don't care about the rest on UHL
    // Section 3.12.d | Data Set Idendtification Record (DSI)
    // We completely skip this
    // Section 3.12.e | Accuracy Description Record (ACC)
    // We completely skip this
    static constexpr size_t DATA_RECORD_START_OFFSET = 3428;
    file.seekg(DATA_RECORD_START_OFFSET);

    // Section 3.12.f | Data Record
    // Data is recorded as scanlines, each scanline has its own record
    minMax[0] = FLT_MAX;
    minMax[1] = -FLT_MAX;
    uint32_t totalPoints = dimensions[0] * dimensions[1];
    heightValues.resize(totalPoints);
    //
    for(uint32_t y = 0; y < dimensions[1]; y++)
    {
        // TODO: Bulk read each scanline then do the endianness convert
        static constexpr std::byte DATA_RECORD_SENTINEL = std::byte(170); // 252 in octal
        char sentinel; file.read(&sentinel, 1);
        if(std::byte(sentinel) != DATA_RECORD_SENTINEL)
        {
            std::fprintf(stderr, "Wrong data sentinel in DTED file \"%s\", "
                         "scanline: %d. Was %d, should be %d.\n",
                         fName.c_str(), y, int(sentinel),
                         int(DATA_RECORD_SENTINEL));
            std::exit(EXIT_FAILURE);
        }
        // Skip local lat/lon counts (it was all zero on the file that I've tested)
        // But we need to add these to checksum afaik.
        std::byte sectionHeader[7];  file.read(reinterpret_cast<char*>(sectionHeader), 7);
        uint32_t checksum = (uint32_t(sectionHeader[0]) + uint32_t(sectionHeader[1]) +
                             uint32_t(sectionHeader[2]) + uint32_t(sectionHeader[3]) +
                             uint32_t(sectionHeader[4]) + uint32_t(sectionHeader[5]) +
                             uint32_t(sectionHeader[6]));
        checksum += uint32_t(DATA_RECORD_SENTINEL);

        for(uint32_t x = 0; x < dimensions[0]; x++)
        {
            std::byte data[2]; file.read(reinterpret_cast<char*>(data), 2);
            checksum += uint32_t(data[0]) + uint32_t(data[1]);
            uint16_t elevUInt = (uint16_t(data[0]) << uint16_t(8)) | uint16_t(data[1]);

            // Data is 1s-complement?
            bool isNeg = (elevUInt & 0x8000) == 0x8000;
            elevUInt = elevUInt & 0x7FFF;
            //
            int16_t elev = (isNeg) ? int16_t(elevUInt) * int16_t(-1) : int16_t(elevUInt);
            float elevFloat = float(elev);
            heightValues[y * dimensions[0] + x] = elevFloat;
            minMax[0] = std::min(minMax[0], elevFloat);
            minMax[1] = std::max(minMax[1], elevFloat);
        }
        // Read the checksum
        std::byte checksumData[4]; file.read(reinterpret_cast<char*>(checksumData), 4);
        uint32_t checksumFile = 0;
        checksumFile |= uint32_t(checksumData[0]) << 24u;
        checksumFile |= uint32_t(checksumData[1]) << 16u;
        checksumFile |= uint32_t(checksumData[2]) << 8u;
        checksumFile |= uint32_t(checksumData[3]) << 0u;
        if(checksumFile != checksum)
        {
            std::fprintf(stderr, "Wrong checksum in DTED file \"%s\", "
                         "scanline %d. Was %d, should be %d.\n",
                         fName.c_str(), y, checksum, checksumFile);
            std::exit(EXIT_FAILURE);
        }
    }
    // Finally done...
}

void SetupGLFWErrorCallback()
{
    // Local function as lambda, should not capture anything
    // for it to be usable with C API of GLFW.
    static auto PrintErr = [](int errorCode, const char* err)
    {
        std::fprintf(stderr, "[ERR][GLFW]:[%d]: \"%s\"\n", errorCode, err);
    };
    //
    glfwSetErrorCallback(PrintErr);
}

void APIENTRY PrintOpenGLError(GLenum source, GLenum type,
                               GLuint id, GLenum severity,
                               GLsizei, const GLchar* message,
                               const void*)
{
    const char* srcStr = nullptr;
    switch(source)
    {
        case GL_DEBUG_SOURCE_API:               srcStr = "API";             break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:     srcStr = "WINDOW";          break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER:   srcStr = "SHADER_COMPILER"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:       srcStr = "THIRD_PARTY";     break;
        case GL_DEBUG_SOURCE_APPLICATION:       srcStr = "APPLICATION";     break;
        case GL_DEBUG_SOURCE_OTHER:             srcStr = "OTHER";           break;
        default:                                srcStr = "UNKNOWN";         break;
    }
    //
    const char* errStr = nullptr;
    switch(type)
    {
        case GL_DEBUG_TYPE_ERROR:               errStr = "ERROR";               break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: errStr = "DEPRECATED_BEHAVIOR"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  errStr = "UNDEFINED_BEHAVIOR";  break;
        case GL_DEBUG_TYPE_PORTABILITY:         errStr = "PORTABILITY";         break;
        case GL_DEBUG_TYPE_PERFORMANCE:         errStr = "PERFORMANCE";         break;
        case GL_DEBUG_TYPE_OTHER:               errStr = "OTHER";               break;
        default:                                errStr = "UNKNOWN";             break;
    }

    // Do not print if debug type is other, it prints information and
    // may be mistaken as error
    if(type == GL_DEBUG_TYPE_OTHER) return;

    //
    const char* severityStr = nullptr;
    switch(severity)
    {
        case GL_DEBUG_SEVERITY_LOW:     severityStr = "LOW";     break;
        case GL_DEBUG_SEVERITY_MEDIUM:  severityStr = "MEDIUM";  break;
        case GL_DEBUG_SEVERITY_HIGH:    severityStr = "HIGH";    break;
        default:                        severityStr = "UNKNOWN"; break;
    }

    std::fprintf(stderr,
                 "======== OGL-INFO ========\n"
                 "[%d]:[%s]:[%s]:[%s]\n"
                 "%s\n"
                 "===========================\n",
                 id, srcStr, errStr, severityStr, message);
};

void SetupOpenGLErrorCallback()
{
    // Add Callback
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(PrintOpenGLError, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                          GL_DONT_CARE, 0, nullptr,
                          GL_TRUE);
}
