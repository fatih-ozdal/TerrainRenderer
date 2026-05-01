#pragma once

#include <string>
#include <cassert>

// OGL Loader
#include <glad/glad.h>
// 3D Linear Algebra
#include <glm/glm.hpp>
#include <glm/ext.hpp>
// Window System
#include <GLFW/glfw3.h>
// GUI
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <iostream>

// Window Callbacks
void WindowPositionCallback(GLFWwindow* wnd, int x, int y);
void WindowSizeCallback(GLFWwindow* wnd, int x, int y);
void MouseMoveCallback(GLFWwindow*, double x, double y);
void MouseButtonCallback(GLFWwindow*, int button, int action, int);
void MouseScrollCallback(GLFWwindow*, double dx, double dy);
void FramebufferChangeCallback(GLFWwindow*, int w, int h);
void KeyboardCallback(GLFWwindow*, int button, int scancode, int action, int mode);

struct CallbackPointersGLFW
{
    GLFWcursorposfun       mMoveCallback = MouseMoveCallback;
    GLFWmousebuttonfun     mButtonCallback = MouseButtonCallback;
    GLFWscrollfun          mScrollCallback = MouseScrollCallback;
    GLFWkeyfun             keyCallback = KeyboardCallback;
    GLFWframebuffersizefun fboCallback = FramebufferChangeCallback;
    GLFWwindowposfun       winPosCallback = WindowPositionCallback;
    GLFWwindowsizefun      winSizeCallback = WindowSizeCallback;
};

struct CamTransform
{
    //glm::vec3 gaze = glm::vec3(0.0f, 0.0f, 0.0f);
    //glm::vec3 pos = glm::vec3(0.0f, 0.0f, 5.0f);
    //glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::quat rotation = glm::identity<glm::quat>();
    glm::vec3 translation = glm::vec3(0, 0, 5);
};

struct WindowParams
{
    glm::ivec2 pos;
    glm::ivec2 size;
    glm::ivec2 fbSize;
};

struct OrbitCam
{
    float yaw      = 0.0f;
    float pitch    = 0.3f;
    float distance = 40.0f;
};

struct GLState
{
    GLFWwindow*  window = nullptr;
    GLuint       renderPipeline = 0u;

    WindowParams notFullScreenWndParams;

    // Data from callbacks
    WindowParams curWndParams;
    // Camera
    CamTransform cam;
    // Mouse
    double   prevMouseX = 0.0;
    double   prevMouseY = 0.0;
    bool     mouseToggle = false;
    //
    bool     fullscreen = false;
    bool     wireframe = false;
    float    heightScale = 1.0f;
    //
    uint32_t vertexPerPatch = 2;

    // Basic point light
    // Very narrow angle to give a high contrast shading
    glm::vec3 lightPos = glm::vec3(10, 6, 50);

    // Render mode
    uint32_t mode = 1;

    // Plane state
    glm::vec3 planePos   = glm::vec3(0, 15, 0);
    glm::quat planeRot   = glm::identity<glm::quat>();
    float     planeSpeed = 0.0f;

    // Orbit camera
    OrbitCam orbitCam;

    // Additional input state
    bool isRightButtonPressed = false;
    bool altKeyHeld           = false;

    // Constructors, Movement & Destructor
            GLState(const char* const windowName,
                    int width, int height,
                    CallbackPointersGLFW);
             GLState(const GLState&) = delete;
             GLState(GLState&&) = delete;
    GLState& operator=(const GLState&) = delete;
    GLState& operator=(GLState&&) = delete;
             ~GLState();


};

struct ShaderGL
{
    enum Type
    {
        VERTEX = GL_VERTEX_SHADER,
        FRAGMENT = GL_FRAGMENT_SHADER
    };

    GLuint      shaderId = 0;

    // Constructors, Movement & Destructor
              ShaderGL(Type t, const std::string& path);
              ShaderGL(const ShaderGL&) = delete;
              ShaderGL(ShaderGL&&);
    ShaderGL& operator=(const ShaderGL&) = delete;
    ShaderGL& operator=(ShaderGL&&);
              ~ShaderGL();
};

struct PlaneGenParams
{
    glm::vec2   rangeX;
    glm::vec2   rangeZ;
    glm::uvec2  vertexCount;
};

struct MeshGL
{
    // These intake Ids must match to the vertex shader
    // That is used currently.
    static constexpr GLuint IN_POS = 0;
    static constexpr GLuint IN_NORMAL = 1;
    static constexpr GLuint IN_UV = 2;
    static constexpr GLuint IN_COLOR = 3;

    GLuint vBufferId = 0;
    GLuint iBufferId = 0;
    GLuint vaoId = 0;
    GLuint indexCount = 0;
    // Constructors, Movement & Destructor
            MeshGL(const PlaneGenParams& params);
            MeshGL(const std::string& objPath);
            MeshGL(const MeshGL&) = delete;
            MeshGL(MeshGL&&);
    MeshGL& operator=(const MeshGL&) = delete;
    MeshGL& operator=(MeshGL&&);
            ~MeshGL();
};

struct TextureGL
{
    enum SampleMode
    {
        NEAREST = GL_NEAREST_MIPMAP_NEAREST,
        LINEAR = GL_LINEAR_MIPMAP_LINEAR
    };

    enum EdgeResolve
    {
        CLAMP = GL_CLAMP_TO_EDGE,
        REPEAT = GL_REPEAT,
        MIRROR = GL_MIRRORED_REPEAT
    };

    GLuint  textureId = 0;
    int     width = 0;
    int     height = 0;
    int     channelCount = 0;

    // Constructors, Movement & Destructor
               TextureGL(const std::string& texPath,
                         SampleMode, EdgeResolve,
                         bool doLinearize = true);
               TextureGL(const TextureGL&) = delete;
               TextureGL(TextureGL&&);
    TextureGL& operator=(const TextureGL&) = delete;
    TextureGL& operator=(TextureGL&&);
               ~TextureGL();
};

struct GeoDataDTED
{
    glm::dvec2         latRange;
    glm::dvec2         lonRange;
    glm::uvec2         dimensions;
    std::vector<float> heightValues;
    glm::vec2          minMax;

    // Constructors, Movement & Destructor
                 GeoDataDTED(const std::string& fName);
                 GeoDataDTED(const GeoDataDTED&) = default;
                 GeoDataDTED(GeoDataDTED&&) = default;
    GeoDataDTED& operator=(const GeoDataDTED&) = default;
    GeoDataDTED& operator=(GeoDataDTED&&) = default;
                 ~GeoDataDTED() = default;
    // Access
    float  operator()(uint32_t x, uint32_t y) const;
    float& operator()(uint32_t x, uint32_t y);
};

struct UserInterface
{
    // Has no state, we just use the destructor to
    // properly de-init Imgui

    // Constructors, Movement & Destructor
                   UserInterface(const GLState& state);
                   UserInterface(const UserInterface&) = delete;
                   UserInterface(UserInterface&&) = delete;
    UserInterface& operator=(const UserInterface&) = delete;
    UserInterface& operator=(UserInterface&&) = delete;
                   ~UserInterface();
    //
    void BeginFrame();
    void EndFrame();
};

// Inline Definitions
inline ShaderGL::ShaderGL(ShaderGL&& other)
    : shaderId(other.shaderId)
{
    other.shaderId = 0;
}

inline ShaderGL& ShaderGL::operator=(ShaderGL&& other)
{
    assert(this != &other);
    shaderId = other.shaderId;
    other.shaderId = 0;
    return *this;
}

inline ShaderGL::~ShaderGL()
{
    if(shaderId) glDeleteProgram(shaderId);
}

inline MeshGL::MeshGL(MeshGL&& other)
    : vBufferId(other.vBufferId)
    , iBufferId(other.iBufferId)
    , vaoId(other.vaoId)
{
    other.vBufferId = 0;
    other.iBufferId = 0;
    other.vaoId = 0;
}

inline MeshGL& MeshGL::operator=(MeshGL&& other)
{
    assert(this != &other);
    vBufferId = other.vBufferId;
    iBufferId = other.iBufferId;
    vaoId = other.vaoId;
    other.vBufferId = 0;
    other.iBufferId = 0;
    other.vaoId = 0;
    return *this;
}

inline MeshGL::~MeshGL()
{
    if(vaoId) glDeleteVertexArrays(1, &vaoId);
    if(vBufferId) glDeleteBuffers(1, &vBufferId);
    if(iBufferId) glDeleteBuffers(1, &iBufferId);
}

inline TextureGL::TextureGL(TextureGL&& other)
    : textureId(other.textureId)
{
    other.textureId = 0;
}

inline TextureGL& TextureGL::operator=(TextureGL&& other)
{
    assert(this != &other);
    if (textureId) glDeleteTextures(1, &textureId);
    textureId = other.textureId;
    other.textureId = 0;
    return *this;
}

inline TextureGL::~TextureGL()
{
    if(textureId) glDeleteTextures(1, &textureId);
}

inline float GeoDataDTED::operator()(uint32_t x, uint32_t y) const
{
    uint32_t index = y * dimensions[0] + x;
    assert(index < heightValues.size());
    return heightValues[index];
}

inline float& GeoDataDTED::operator()(uint32_t x, uint32_t y)
{
    uint32_t index = y * dimensions[0] + x;
    assert(index < heightValues.size());
    return heightValues[index];
}

inline UserInterface::UserInterface(const GLState& state)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    ImGui_ImplGlfw_InitForOpenGL(state.window, true);
    ImGui_ImplOpenGL3_Init();
}

inline UserInterface::~UserInterface()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

inline void UserInterface::BeginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

inline void UserInterface::EndFrame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}