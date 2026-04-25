#include <cstdio>
#include <array>

#include "thread_pool.h"

#include <glm/ext.hpp> // for matrix calculation

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>

// ============== //
//  HW1-CODE Here //
// ============== //
#include "hw1.h"

static constexpr double DefaultSensitivity = 0.0025;

void WindowPositionCallback(GLFWwindow* wnd, int x, int y)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));

    state.curWndParams.pos[0] = x;
    state.curWndParams.pos[1] = y;
}

void WindowSizeCallback(GLFWwindow* wnd, int x, int y)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));
    state.curWndParams.size[0] = x;
    state.curWndParams.size[1] = y;
}

void MouseMoveCallback(GLFWwindow* wnd, double x, double y)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));
    float diffX = static_cast<float>(x - state.prevMouseX);
    float diffY = static_cast<float>(y - state.prevMouseY);

    if(state.mouseToggle)
    {
        float angX = float(-diffX * DefaultSensitivity);
        float angY = float(-diffY * DefaultSensitivity);

        // YAW
        glm::quat deltaYaw = glm::angleAxis(angX, glm::vec3(0, 1, 0));
        //
        // PITCH
        glm::quat deltaPitch = glm::angleAxis(angY, glm::vec3(1, 0, 0));
        //
        glm::quat newRot = deltaYaw * deltaPitch * state.cam.rotation;
        state.cam.rotation = glm::normalize(newRot);
    }
    state.prevMouseX = x;
    state.prevMouseY = y;
}

void MouseButtonCallback(GLFWwindow* wnd, int button, int action, int)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));
    if(button == GLFW_MOUSE_BUTTON_1)
    {
        if(action == GLFW_PRESS)    state.mouseToggle = true;
        if(action == GLFW_RELEASE)  state.mouseToggle = false;
    }
}

void MouseScrollCallback(GLFWwindow* wnd, double dx, double dy)
{
    static constexpr double DefaultZoomPercentage = 1.1;

    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));


    // Zoom to the focus until some threshold
    glm::vec3 look = state.cam.rotation * glm::vec3(0, 0, 1);
    if(look.length() > 0.1f)
    {
        state.cam.translation[2] += float(dy * DefaultZoomPercentage);
    }
}

void FramebufferChangeCallback(GLFWwindow* wnd, int w, int h)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));
    state.curWndParams.fbSize[0] = w;
    state.curWndParams.fbSize[1] = h;
}

void KeyboardCallback(GLFWwindow* wnd, int key, int scancode, int action, int modifier)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));
    uint32_t mode = state.mode;

    glm::vec3 ratio = glm::vec3(0.25f);
    if(key == GLFW_KEY_W)
    {
        state.cam.translation[2] += ratio[0];
    }
    if(key == GLFW_KEY_S)
    {
        state.cam.translation[2] -= ratio[0];
    }
    if(key == GLFW_KEY_A)
    {
        state.cam.translation[0] += ratio[0];
    }
    if(key == GLFW_KEY_D)
    {
        state.cam.translation[0] -= ratio[0];
    }


    if(key == GLFW_KEY_E || key == GLFW_KEY_Q)
    {
        float rotAngle = float(DefaultSensitivity * 10);
        rotAngle = (key == GLFW_KEY_Q) ? -rotAngle : rotAngle;
        //
        glm::quat deltaRoll = glm::angleAxis(rotAngle, glm::vec3(0, 0, 1));
        glm::quat newRot = deltaRoll * state.cam.rotation;
        //
        state.cam.rotation = glm::normalize(newRot);
    }

    if(action != GLFW_RELEASE) return;

    if(key == GLFW_KEY_P) mode = (mode == 2) ? 0 : (mode + 1);
    if(key == GLFW_KEY_O) mode = (mode == 0) ? 2 : (mode - 1);
    if(key == GLFW_KEY_L) state.wireframe = !state.wireframe;

    // Tesselation
    if(key == GLFW_KEY_J) state.vertexPerPatch /= 2;
    if(key == GLFW_KEY_K) state.vertexPerPatch *= 2;
    // Vertex shader height adjust
    if(key == GLFW_KEY_KP_SUBTRACT) state.heightScale /= 2.0f;
    if(key == GLFW_KEY_KP_ADD) state.heightScale *= 2.0f;
    //
    if(key == GLFW_KEY_ENTER)
    {
        state.fullscreen = !state.fullscreen;
        if(state.fullscreen)
            state.notFullScreenWndParams = state.curWndParams;
    }

    state.mode = mode;
}

void ChangeFullscreen(bool& isFullscreen, GLState& state)
{
    if(isFullscreen != state.fullscreen)
    {
        isFullscreen = state.fullscreen;

        // TODO: Reason about this how to do monitor full screen
        // according to the current monitor that the window is in.
        // Get monitor returns null
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        WindowParams wp = state.notFullScreenWndParams;
        if(isFullscreen)
        {
            wp.pos = glm::ivec2(0);
            wp.size = glm::ivec2(mode->width, mode->height);
        }
        glfwSetWindowMonitor(state.window,
                             (isFullscreen) ? monitor : nullptr,
                             wp.pos[0], wp.pos[1],
                             wp.size[0], wp.size[1],
                             mode->refreshRate);
    }
}

int main(int argc, const char* argv[])
{
    GLState state = GLState("Terrain Renderer", 1280, 720,
                            CallbackPointersGLFW());
    ShaderGL vShader = ShaderGL(ShaderGL::VERTEX, "shaders/terrain.vert");
    ShaderGL fShader = ShaderGL(ShaderGL::FRAGMENT, "shaders/terrain.frag");
    // Set unchanged state(s)
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glFrontFace(GL_CCW);

    ThreadPool threadPool({.threadCount = std::thread::hardware_concurrency()});
    bool isFullscreen = state.fullscreen;

    HW1 hw1(threadPool, state);

    // ================ //
    //     GUI Init     //
    // ================ //
    UserInterface ui = UserInterface(state);

    // =============== //
    //   RENDER LOOP   //
    // =============== //
    while(!glfwWindowShouldClose(state.window))
    {
        // Poll inputs from the OS via GLFW
        glfwPollEvents();

        // Set/Reset fullscreen
        ChangeFullscreen(isFullscreen, state);

        // ================ //
        //     GUI START    //
        // ================ //
        ui.BeginFrame();


        // =============== //
        //      HW 1       //
        // =============== //
        hw1.Work();

        // ================ //
        //     GUI WORK     //
        // ================ //
        // You can check the "imgui_demo.cpp" in the project
        // for many GUI widgets and how to use them
        // It is straightforward, also you can check
        // https://github.com/ocornut/imgui/wiki/Getting-Started
        //ImGui::ShowDemoWindow();

        // ================ //
        //      GUI END     //
        // ================ //
        ui.EndFrame();

        // Frame End
        glfwSwapBuffers(state.window);
    }
}
