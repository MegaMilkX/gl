#include "log/log.hpp"
#include "filesystem/filesystem.hpp"
#include "platform/win32/gl/glextutil.h"
#include <cassert>
#include "windowsx.h"

#include "profiler/profiler.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef void(*platform_window_resize_cb_t)(int, int);

static HWND s_hWnd;
static HDC s_hdc = 0;
static HGLRC s_gl_context;
static int s_window_posx = CW_USEDEFAULT, s_window_posy = CW_USEDEFAULT;
static int s_window_width = 640, s_window_height = 480;
static int s_windowed_width = 640, s_windowed_height = 480; // Cached size of a window to come back to out of fullscreen
static platform_window_resize_cb_t s_window_resize_cb_f = 0;
static bool s_is_fullscreen = false;
static WINDOWPLACEMENT s_window_placement = { 0 };

static float s_mouse_x = 0, s_mouse_y = 0;
static bool s_is_mouse_locked = false;
static bool s_is_mouse_hidden = false;

uint32_t hsv2rgb(float H, float S, float V) {/*
    if (H > 360 || H < 0 || S>100 || S < 0 || V>100 || V < 0) {
        // TODO: assert?
        return 0xFF000000;
    }*/
    H *= 360.f;
    H = fmodf(H, 360.f);
    float s = S;
    float v = V;
    float C = s * v;
    float X = C * (1 - abs(fmod(H / 60.f, 2) - 1));
    float m = v - C;
    float r, g, b;
    if (H >= 0 && H < 60) {
        r = C, g = X, b = 0;
    }
    else if (H >= 60 && H < 120) {
        r = X, g = C, b = 0;
    }
    else if (H >= 120 && H < 180) {
        r = 0, g = C, b = X;
    }
    else if (H >= 180 && H < 240) {
        r = 0, g = X, b = C;
    }
    else if (H >= 240 && H < 300) {
        r = X, g = 0, b = C;
    }
    else {
        r = C, g = 0, b = X;
    }
    int R = (r + m) * 255;
    int G = (g + m) * 255;
    int B = (b + m) * 255;

    uint32_t ret = 0xFF000000;
    ret |= R << 16;
    ret |= G << 8;
    ret |= B;

    return ret;
}

static BOOL CALLBACK MonitorEnum(HMONITOR hMon, HDC hdc, LPRECT lprcMonitor, LPARAM pData) {
    MONITORINFOEX minf = { 0 };
    minf.cbSize = sizeof(minf);
    if (!GetMonitorInfoA(hMon, &minf)) {
        return TRUE;
    }
    LOG("win32", "Monitor " << minf.szDevice);
    return TRUE;
}

static void toggleFullscreen() {
    s_is_fullscreen = !s_is_fullscreen;

    if (!s_is_fullscreen) {
        RECT monitor_rc = { 0 };
        {
            HMONITOR hMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
            //HMONITOR hMon = MonitorFromWindow(0, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFOEX minf = { 0 };
            minf.cbSize = sizeof(minf);
            if (!GetMonitorInfoA(hMon, &minf)) {
                LOG_ERR("win32", "Failed to get monitor info");
                return;
            }
            monitor_rc = minf.rcMonitor;
        }

        s_window_width = s_windowed_width;
        s_window_height = s_windowed_height;

        DWORD style = WS_OVERLAPPEDWINDOW;
        RECT wr = { 0, 0, s_window_width, s_window_height };
        AdjustWindowRect(&wr, style, FALSE);
        /*
        s_window_posx = monitor_rc.left + (monitor_rc.right - monitor_rc.left) / 2 - (wr.right - wr.left) / 2;
        s_window_posy = monitor_rc.top + (monitor_rc.bottom - monitor_rc.top) / 2 - (wr.bottom - wr.top) / 2;
        if (s_window_posx < monitor_rc.left) {
            s_window_posx = monitor_rc.left;
        }
        if (s_window_posy < monitor_rc.top) {
            s_window_posy = monitor_rc.top;
        }*/

        SetWindowLongPtr(s_hWnd, GWL_STYLE, style);
        //SetWindowPos(s_hWnd, 0/*HWND_TOPMOST*/, s_window_posx, s_window_posy, wr.right - wr.left, wr.bottom - wr.top, SWP_SHOWWINDOW);
        SetWindowPlacement(s_hWnd, &s_window_placement);
    } else {
        GetWindowPlacement(s_hWnd, &s_window_placement);

        RECT rc = { 0 };
        GetWindowRect(s_hWnd, &rc);
        HMONITOR hMon = MonitorFromRect(&rc, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEX minf = { 0 };
        minf.cbSize = sizeof(minf);
        if (!GetMonitorInfoA(hMon, &minf)) {
            LOG_ERR("win32", "Failed to get monitor info");
            return;
        }
        rc = minf.rcMonitor;
        LOG("win32", "Going fullscreen to monitor " << minf.szDevice);
        LOG("win32", "Rectangle left: " << rc.left << " top: " << rc.top << " right: " << rc.right << " bottom: " << rc.bottom);

        SetWindowLongPtr(s_hWnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(s_hWnd, 0/*HWND_TOPMOST*/, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_SHOWWINDOW);
    }
}

static bool dbgShowGBuffer = false;
static bool lMouseButtonPressed = false;
static bool rMouseButtonPressed = false;

static float camera_rotation_x = .0f;
static float camera_rotation_y = .0f;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
            if (s_is_mouse_locked) {
                RECT rc;
                GetWindowRect(s_hWnd, &rc);
                rc.left = rc.right = rc.left + (rc.right - rc.left) * .5f;
                rc.top = rc.bottom = rc.top + (rc.bottom - rc.top) * .5f;
                ClipCursor(&rc);
            }
            if (s_is_mouse_hidden) {
                ShowCursor(false);
            }
        } else if(LOWORD(wParam) == WA_INACTIVE) {
            if (s_is_mouse_hidden) {
                ShowCursor(true);
            }
        }
        break;
    case WM_MOVE: {
        if (!s_is_fullscreen) {
            s_window_posx = LOWORD(lParam);
            s_window_posy = HIWORD(lParam);
        }
        break;
    }
    case WM_SIZE: {
        RECT wr = { 0 };
        GetClientRect(hWnd, &wr);
        s_window_width = wr.right - wr.left;
        s_window_height = wr.bottom - wr.top;
        if (!s_is_fullscreen) {
            s_windowed_width = s_window_width;
            s_windowed_height = s_window_height;
        }
        if (s_window_resize_cb_f) {
            s_window_resize_cb_f(s_window_width, s_window_height);
        }
        break;
    }
    case WM_SYSCOMMAND: {
        if (SC_KEYMENU == (wParam & 0xFFF0) && lParam == VK_RETURN) {
            toggleFullscreen();
        } else {
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
        break;
    }
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_F1:
            dbgShowGBuffer = !dbgShowGBuffer;
            break;
        };
        break;
    case WM_KEYUP:
        break;    
    case WM_LBUTTONDOWN:
        lMouseButtonPressed = true;
        break;
    case WM_LBUTTONUP:
        lMouseButtonPressed = false;
        break;
    case WM_RBUTTONDOWN:
        rMouseButtonPressed = true;
        break;
    case WM_RBUTTONUP:
        rMouseButtonPressed = false;
        break;
    case WM_MBUTTONDOWN:
        break;
    case WM_MBUTTONUP:
        break;
    case WM_MOUSEWHEEL:
        break;
    case WM_MOUSEMOVE: {
        float nx = GET_X_LPARAM(lParam);
        float ny = GET_Y_LPARAM(lParam);
        if (lMouseButtonPressed) {
            camera_rotation_x += (ny - s_mouse_y) * .01f;
            camera_rotation_y += (nx - s_mouse_x) * .01f;
        }
        s_mouse_x = nx;
        s_mouse_y = ny;
        break;
    }
    case WM_CHAR:
        break;
    case WM_INPUT: {
        break;
    }
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static void APIENTRY glDbgCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void*) {
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        LOG_ERR("gl/dbg", std::string(message, length));
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        LOG_WARN("gl/dbg", std::string(message, length));
        break;
    case GL_DEBUG_SEVERITY_LOW:
        LOG_WARN("gl/dbg", std::string(message, length));
        break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
        LOG("gl/dbg", std::string(message, length));
        break;
    }
}

static bool createWindowOpenGl(int width, int height, bool fullscreen) {
    PROF_SCOPE_FN();

    s_is_fullscreen = false;

    WNDCLASSEXA wc_tmp = { 0 };
    wc_tmp.cbSize = sizeof(wc_tmp);
    wc_tmp.lpfnWndProc = DefWindowProc;
    wc_tmp.hInstance = GetModuleHandle(0);
    wc_tmp.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
    wc_tmp.lpszClassName = "MainWindowTmp";
    wc_tmp.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    if (!RegisterClassExA(&wc_tmp)) {
        return 1;
    }
    HWND hWnd_tmp = CreateWindowA(wc_tmp.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, 0, 0, wc_tmp.hInstance, 0);

    PIXELFORMATDESCRIPTOR pfd_tmp =
    {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    // Flags
        PFD_TYPE_RGBA,        // The kind of framebuffer. RGBA or palette.
        32,                   // Colordepth of the framebuffer.
        0, 0, 0, 0, 0, 0,
        0,
        0,
        0,
        0, 0, 0, 0,
        24,                   // Number of bits for the depthbuffer
        8,                    // Number of bits for the stencilbuffer
        0,                    // Number of Aux buffers in the framebuffer.
        PFD_MAIN_PLANE,
        0,
        0, 0, 0
    };
    HDC hdc_tmp = GetDC(hWnd_tmp);
    int pixel_format_tmp = ChoosePixelFormat(hdc_tmp, &pfd_tmp);

    SetPixelFormat(hdc_tmp, pixel_format_tmp, &pfd_tmp);

    HGLRC gl_context_tmp = wglCreateContext(hdc_tmp);
    wglMakeCurrent(hdc_tmp, gl_context_tmp);

    WGLEXTLoadFunctions();

    // Proper context creation
    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(0);
    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
    wc.lpszClassName = "MainWindow";
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(GetModuleHandle(0), "IDI_ICON1");
    if (!RegisterClassExA(&wc)) {
        return false;
    }

    RECT monitor_rc = { 0 };
    {
        HMONITOR hMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        //HMONITOR hMon = MonitorFromWindow(0, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEX minf = { 0 };
        minf.cbSize = sizeof(minf);
        if (!GetMonitorInfoA(hMon, &minf)) {
            LOG_ERR("win32", "Failed to get monitor info");
            return false;
        }
        monitor_rc = minf.rcMonitor;
    }

    s_window_width = width;
    s_window_height = height;
    s_windowed_width = width;
    s_windowed_height = height;
    
    DWORD style = 0;
    style = WS_OVERLAPPEDWINDOW;

    RECT wr = { 0, 0, s_window_width, s_window_height };
    AdjustWindowRect(&wr, style, FALSE);

    s_window_posx = monitor_rc.left + (monitor_rc.right - monitor_rc.left) / 2 - (wr.right - wr.left) / 2;
    s_window_posy = monitor_rc.top + (monitor_rc.bottom - monitor_rc.top) / 2 - (wr.bottom - wr.top) / 2;
    if (s_window_posx < monitor_rc.left) {
        s_window_posx = monitor_rc.left;
    }
    if (s_window_posy < monitor_rc.top) {
        s_window_posy = monitor_rc.top;
    }

    style |= WS_VISIBLE;
    
    s_hWnd = CreateWindowA(wc.lpszClassName, "FX", style, s_window_posx, s_window_posy, wr.right - wr.left, wr.bottom - wr.top, 0, 0, wc.hInstance, 0);
    SetCursor(LoadCursor(NULL, IDC_ARROW));

    if (fullscreen) {
        toggleFullscreen();
    }

    HDC hdc = GetDC(s_hWnd);
    const int attribList[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_COLOR_BITS_ARB, 32,
        WGL_ALPHA_BITS_ARB, 8,
        WGL_DEPTH_BITS_ARB, 24,
        WGL_STENCIL_BITS_ARB, 8,
        WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
        WGL_SAMPLES_ARB, 4,
        0
    };
    UINT num_formats = 0;
    int pixel_format = 0;
    wglChoosePixelFormatARB(hdc, attribList, 0, 1, &pixel_format, &num_formats);

    PIXELFORMATDESCRIPTOR pfd;
    DescribePixelFormat(hdc, pixel_format, sizeof(pfd), &pfd);
    SetPixelFormat(hdc, pixel_format, &pfd);

    int contextAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 6,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    s_gl_context = wglCreateContextAttribsARB(hdc, 0, contextAttribs);

    // Destroy temporary context and window
    wglMakeCurrent(0, 0);
    wglDeleteContext(gl_context_tmp);
    ReleaseDC(hWnd_tmp, hdc_tmp);
    DestroyWindow(hWnd_tmp);

    //
    wglMakeCurrent(hdc, s_gl_context);
    GLEXTLoadFunctions();
    s_hdc = hdc;

    GLint maxAttribs = 0;
    GLint maxDrawBuffers = 0;
    GLint maxUniformBufferBindings = 0;
    GLint maxTextureBufferSz = 0;
    GLint maxTextureImageUnits = 0;
    GLint maxVertexTextureImageUnits = 0;
    GLint maxCombinedTextureImageUnits = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUniformBufferBindings);
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTextureBufferSz);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureImageUnits);
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxVertexTextureImageUnits);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTextureImageUnits);

    LOG("gl", "GL VERSION: " << (char*)glGetString(GL_VERSION));
    LOG("gl", "GL VENDOR: " << (char*)glGetString(GL_VENDOR));
    LOG("gl", "GL RENDERER: " << (char*)glGetString(GL_RENDERER));
    LOG("gl", "GLSL VERSION: " << (char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
    LOG("gl", "GL_MAX_VERTEX_ATTRIBS: " << maxAttribs);
    LOG("gl", "GL_MAX_DRAW_BUFFERS: " << maxDrawBuffers);
    LOG("gl", "GL_MAX_UNIFORM_BUFFER_BINDINGS: " << maxUniformBufferBindings);
    LOG("gl", "GL_MAX_TEXTURE_BUFFER_SIZE: " << maxTextureBufferSz);
    LOG("gl", "GL_MAX_TEXTURE_IMAGE_UNITS: " << maxTextureImageUnits);
    LOG("gl", "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS: " << maxVertexTextureImageUnits);
    LOG("gl", "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: " << maxCombinedTextureImageUnits);

//#ifdef _DEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(&glDbgCallback, 0);
//#endif

    // VSync
    wglSwapIntervalEXT(1);

    // Raw input
    RAWINPUTDEVICE rid[2];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = 0;
    rid[0].hwndTarget = 0;
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06;
    rid[1].dwFlags = 0;
    rid[1].hwndTarget = 0;
    if (RegisterRawInputDevices(rid, 2, sizeof(rid[0])) == FALSE) {
        LOG_ERR("win32", "RegisterRawInputDevices failed!");
        assert(false);
    }
	return true;
}

static bool pollMessages() {
    MSG msg = { 0 };
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        if (msg.message == WM_QUIT) {
            return false;
        }
    }
    return true;
}

inline void glxShaderSource(GLuint shader, const char* string, int len = 0) {
    glShaderSource(shader, 1, &string, len == 0 ? 0 : &len);
}
inline bool glxCompileShader(GLuint shader) {
    PROF_SCOPE_FN();

    glCompileShader(shader);
    GLint res = GL_FALSE;
    int infoLogLen;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &res);
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLen);
    if(infoLogLen > 1)
    {
        std::vector<char> errMsg(infoLogLen + 1);
        glGetShaderInfoLog(shader, infoLogLen, NULL, &errMsg[0]);
        LOG_ERR("glsl", "GLSL compile: " << &errMsg[0]);
    }
    if(res == GL_FALSE)
        return false;
    return true;
}
inline bool glxLinkProgram(GLuint progid) {
    PROF_SCOPE_FN();

    GL_CHECK(glLinkProgram(progid));
    GLint res = GL_FALSE;
    int infoLogLen;
    glGetProgramiv(progid, GL_LINK_STATUS, &res);
    glGetProgramiv(progid, GL_INFO_LOG_LENGTH, &infoLogLen);
    if (infoLogLen > 1)
    {
        std::vector<char> errMsg(infoLogLen + 1);
        glGetProgramInfoLog(progid, infoLogLen, NULL, &errMsg[0]);
        LOG_ERR("glsl", "GLSL link: " << &errMsg[0]);
    }
    if (res != GL_TRUE) {
        LOG_ERR("glsl", "Shader program failed to link");
        return false;
    }
    return true;
}


enum SHADER_TYPE { SHADER_UNKNOWN, SHADER_VERTEX, SHADER_FRAGMENT };
struct SHADER_PART {
    SHADER_TYPE type;
    const char* begin;
    const char* end;
};

inline GLenum glxShaderTypeToGlEnum(SHADER_TYPE type) {
    switch (type)
    {
    case SHADER_VERTEX:
        return GL_VERTEX_SHADER;
    case SHADER_FRAGMENT:
        return GL_FRAGMENT_SHADER;
    default:
        return 0;
    }
}

void glxSetUniform1i(GLuint progid, const char* name, int value) {
    GLint loc = glGetUniformLocation(progid, name);
    if (loc != -1) {
        glUniform1i(loc, value);
    }
}

inline void prepareShaderProgram(GLuint progid) {
    // Set fragment output locations
    {
        glBindFragDataLocation(progid, 0, "outAlbedo");
        glBindFragDataLocation(progid, 1, "outNormal");
        glBindFragDataLocation(progid, 2, "outWorldPos");
        glBindFragDataLocation(progid, 3, "outRoughness");
        glBindFragDataLocation(progid, 4, "outMetallic");
        glBindFragDataLocation(progid, 5, "outEmission");
        glBindFragDataLocation(progid, 6, "outLightness");
        glBindFragDataLocation(progid, 7, "outFinal");
        /*
        GLint count = 0;
        int name_len = 0;
        const int NAME_MAX_LEN = 64;
        char name[NAME_MAX_LEN];
        glGetProgramInterfaceiv(progid, GL_PROGRAM_OUTPUT, GL_ACTIVE_RESOURCES, &count);
        for (int i = 0; i < count; ++i) {
            glGetProgramResourceName(progid, GL_PROGRAM_OUTPUT, i, NAME_MAX_LEN, &name_len, name);
            assert(name_len < NAME_MAX_LEN);
            std::string output_name(name, name + name_len);
            LOG("gl/shader", "Fragment output " << output_name << ": " << i);
            // TODO: glBindFragDataLocation will have no effect until next linkage
            // but before linkage glGetProgramInterfaceiv() will not return any outputs
            glBindFragDataLocation(progid, i, output_name.c_str());
            // TODO:
        }*/
    }

    // Vertex attributes
    {
        GLint count = 0;
        glGetProgramiv(progid, GL_ACTIVE_ATTRIBUTES, &count);
        for (int i = 0; i < count; ++i) {
            const GLsizei bufSize = 32;
            GLchar name[bufSize];
            GLsizei name_len;
            GLint size;
            GLenum type;
            glGetActiveAttrib(progid, (GLuint)i, bufSize, &name_len, &size, &type, name);
            std::string attrib_name(name, name + name_len);
            GLint attr_loc = glGetAttribLocation(progid, attrib_name.c_str());
            LOG("gl/shader", "Vertex attribute " << attrib_name << ": " << attr_loc);
            // TODO:
        }
    }

    // Uniform buffers
    {
        GLint numBlocks = 0;
        glGetProgramiv(progid, GL_ACTIVE_UNIFORM_BLOCKS, &numBlocks);
        for (int i = 0; i < numBlocks; ++i) {
            GLint nameLen = 0;
            glGetActiveUniformBlockiv(progid, i, GL_UNIFORM_BLOCK_NAME_LENGTH, &nameLen);
            std::string name;
            name.resize(nameLen);
            glGetActiveUniformBlockName(progid, i, nameLen, 0, &name[0]);
            GLint binding = 0;
            glGetActiveUniformBlockiv(progid, i, GL_UNIFORM_BLOCK_BINDING, &binding);
            GLint size = 0;
            glGetActiveUniformBlockiv(progid, i, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
            LOG("gl/shader", "Uniform buffer " << name << ", size: " << size << ", binding: " << binding);
            // TODO: Got uniform block name, can now:
            // 1. Check if name is known
            // 2. Check that all fields align with the cpp struct
            // 3. Set common uniform buffer bindings
            // 4. The other ones - just set sequentially
        }
    }

    // Samplers
    {
        glUseProgram(progid);
        glxSetUniform1i(progid, "texDiffuse", 0);
        glxSetUniform1i(progid, "texNormal", 1);
        glxSetUniform1i(progid, "texWorldPos", 2);
        glxSetUniform1i(progid, "texRoughness", 3);
        glxSetUniform1i(progid, "texMetallic", 4);
        glxSetUniform1i(progid, "texEmission", 5);
        glxSetUniform1i(progid, "texLightness", 6);
        glxSetUniform1i(progid, "texDepth", 7);
        glxSetUniform1i(progid, "cubemapEnvironment", 8);
        glxSetUniform1i(progid, "texBrdfLut", 9);
        glxSetUniform1i(progid, "cubemapIrradiance", 10);
        glxSetUniform1i(progid, "cubemapSpecular", 11);
        glxSetUniform1i(progid, "texHdri", 12);        
        glUseProgram(0);
    }

    {
        GLuint block_index = glGetUniformBlockIndex(progid, "ubCommon");
        if (block_index != GL_INVALID_INDEX) {
            glUniformBlockBinding(progid, block_index, 0);
        }
        block_index = glGetUniformBlockIndex(progid, "ubModel");
        if (block_index != GL_INVALID_INDEX) {
            glUniformBlockBinding(progid, block_index, 1);
        }
    }
}

inline GLuint glxCreateShaderProgram(SHADER_PART* parts, size_t count) {
    std::vector<GLuint> shaders;
    shaders.resize(count);
    for (int i = 0; i < count; ++i) {
        GLuint id = glCreateShader(glxShaderTypeToGlEnum(parts[i].type));
        shaders[i] = id;
        glxShaderSource(id, parts[i].begin, (size_t)(parts[i].end - parts[i].begin));
        if (!glxCompileShader(id)) {
            glDeleteShader(id);
            return 0;
        }
    }

    auto fnDeleteShaders = [&shaders, count]() {
        for (int i = 0; i < count; ++i) {
            glDeleteShader(shaders[i]);
        }
    };

    GLuint progid = glCreateProgram();
    for (int i = 0; i < count; ++i) {
        glAttachShader(progid, shaders[i]);
    }

    if (!glxLinkProgram(progid)) {
        glDeleteProgram(progid);
        fnDeleteShaders();
        return 0;
    }

    fnDeleteShaders();

    prepareShaderProgram(progid);
    return progid;
}
inline GLuint glxCreateShaderProgram(const char* vs, const char* fs) {
    GLuint vsid = glCreateShader(GL_VERTEX_SHADER);
    GLuint fsid = glCreateShader(GL_FRAGMENT_SHADER);
    GLuint progid = glCreateProgram();
    glxShaderSource(vsid, vs);
    glxShaderSource(fsid, fs);
    if (!glxCompileShader(vsid)) {
        glDeleteProgram(progid);
        glDeleteShader(fsid);
        glDeleteShader(vsid);
        return 0;
    }
    if (!glxCompileShader(fsid)) {
        glDeleteProgram(progid);
        glDeleteShader(fsid);
        glDeleteShader(vsid);
        return 0;
    }
    glAttachShader(progid, vsid);
    glAttachShader(progid, fsid);
    if (!glxLinkProgram(progid)) {
        glDeleteProgram(progid);
        glDeleteShader(fsid);
        glDeleteShader(vsid);
        return 0;
    }
    glDeleteShader(vsid);
    glDeleteShader(fsid);

    prepareShaderProgram(progid);

    return progid;
}

#include <map>

inline GLuint loadShader(const char* filename) {
    std::string src = fsSlurpTextFile(filename);
    if (src.empty()) {
        LOG_ERR("gl/shader", "Failed to open shader source file " << filename);
        return 0;
    }

    std::vector<SHADER_PART> parts;
    {
        const char* str = src.data();
        size_t len = src.size();

        SHADER_PART part = { SHADER_UNKNOWN, 0, 0 };
        for (int i = 0; i < len; ++i) {
            char ch = str[i];
            if (isspace(ch)) {
                continue;
            }
            if (ch == '#') {
                const char* tok = str + i;
                int tok_len = 0;
                for (int j = i; j < len; ++j) {
                    ch = str[j];
                    if (isspace(ch)) {
                        break;
                    }
                    tok_len++;
                }
                if (strncmp("#vertex", tok, tok_len) == 0) {
                    if (part.type != SHADER_UNKNOWN) {
                        part.end = str + i;
                        parts.push_back(part);
                    }
                    part.type = SHADER_VERTEX;
                } else if(strncmp("#fragment", tok, tok_len) == 0) {
                    if (part.type != SHADER_UNKNOWN) {
                        part.end = str + i;
                        parts.push_back(part);
                    }
                    part.type = SHADER_FRAGMENT;
                } else {
                    continue;
                }
                i += tok_len;
                for (; i < len; ++i) {
                    ch = str[i];
                    if (ch == '\n') {
                        ++i;
                        break;
                    }
                }
                part.begin = str + i;
            }
        }
        if (part.type != SHADER_UNKNOWN) {
            part.end = str + len;
            parts.push_back(part);
        }
    }

    return glxCreateShaderProgram(parts.data(), parts.size());
}

#include "math/gfxm.hpp"

struct UniformBufferCommon {
    gfxm::mat4 matProjection;
    gfxm::mat4 matView;
    gfxm::vec3 cameraPosition;
    float time;
    gfxm::vec2 viewportSize;
    float zNear;
    float zFar;
};
static_assert(
    sizeof(UniformBufferCommon) 
    == sizeof(gfxm::mat4)
    + sizeof(gfxm::mat4)
    + sizeof(gfxm::vec3)
    + sizeof(float)
    + sizeof(gfxm::vec2)
    + sizeof(float)
    + sizeof(float),
    "UniformBufferCommon misaligned"
);

struct UniformBufferModel {
    gfxm::mat4 matModel;
};
static_assert(
    sizeof(UniformBufferModel)
    == sizeof(gfxm::mat4),
    "UniformBufferModel misaligned"
);

#include "vfmt.hpp"
struct VertexAttribute {
    GLuint buffer;
    VFMT::ATTRIBUTE_UID attrib_uid;
    int stride;
    int offset;
};
struct VertexAttributeSet {
    VertexAttribute attributes[16];
    int attribute_count;
};

#include <unordered_map>
struct VertexAttributeLocationSet {
    std::unordered_map<VFMT::ATTRIBUTE_UID, GLint> locations;
};

void getShaderProgramAttributeLocations(GLuint progid, VertexAttributeLocationSet& locSet) {
    GLint count = 0;
    glGetProgramiv(progid, GL_ACTIVE_ATTRIBUTES, &count);
    for (int i = 0; i < count; ++i) {
        const GLsizei bufSize = 32;
        GLchar name[bufSize];
        GLsizei name_len;
        GLint size;
        GLenum type;
        glGetActiveAttrib(progid, (GLuint)i, bufSize, &name_len, &size, &type, name);
        std::string attrib_name(name, name + name_len);
        GLint attr_loc = glGetAttribLocation(progid, attrib_name.c_str());
        LOG("gl/shader", "Vertex attribute " << attrib_name << ": " << attr_loc);
        auto dsc = VFMT::getAttribDescByInputName(attrib_name.c_str());
        if (!dsc) {
            continue;
        }
        locSet.locations[dsc->uid] = attr_loc;
    }
}

GLuint createVertexArrayObject(VertexAttributeSet* vaSet, VertexAttributeLocationSet* locations) {
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    assert(vaSet->attribute_count <= sizeof(vaSet->attributes) / sizeof(vaSet->attributes[0]));
    for (int i = 0; i < vaSet->attribute_count; ++i) {
        auto& attr = vaSet->attributes[i];
        auto dsc = VFMT::getAttribDesc(attr.attrib_uid);
        auto it = locations->locations.find(attr.attrib_uid);
        if (it == locations->locations.end()) {
            continue;
        }
        auto loc = it->second;
        glBindBuffer(GL_ARRAY_BUFFER, attr.buffer);
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, dsc->count, dsc->gl_type, dsc->normalized, attr.stride, (void*)attr.offset);
    }
    glBindVertexArray(0);
    return vao;
}

GLuint glxCreateArrayBuffer(GLsizeiptr size, const void* data, GLenum usage) {
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return vbo;
}

void glxEnableVertexAttrib(GLuint index, GLuint buffer, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLsizei offset) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, size, type, normalized, stride, (void*)offset);
}

GLuint createFramebufferTexture2d(int width, int height, GLint internalFormat) {
    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint createFramebufferDepthTexture2d(int width, int height) {
    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint createCubeMap(int width, int height, GLint internalFormat) {
    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, width, height, 0, GL_RGB, GL_FLOAT, 0);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_LOD, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LOD, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return tex;
}

GLuint createSpecularCubeMap(int width, int height, GLint internalFormat) {
    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, width, height, 0, GL_RGB, GL_FLOAT, 0);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 4);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_LOD, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LOD, 4);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return tex;
}

void cubemapConvolute(GLuint vao_cube, GLuint progid, GLuint cubemap_in, GLuint cubemap_out, int width, int height) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLenum draw_buffers[] = {
        GL_COLOR_ATTACHMENT0
    };
    glDrawBuffers(1, draw_buffers);

    glActiveTexture(GL_TEXTURE0 + 8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_in);
    glViewport(0, 0, width, height);
    glBindVertexArray(vao_cube);
    glFrontFace(GL_CW);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progid);
    gfxm::mat4 views[6] = {
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 1.f,  .0f,  .0f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3(-1.f,  .0f,  .0f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  1.f,  .0f), gfxm::vec3(.0f,  .0f,  1.f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f, -1.f,  .0f), gfxm::vec3(.0f,  .0f, -1.f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  .0f,  1.f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  .0f, -1.f), gfxm::vec3(.0f, -1.f,  .0f)),
    };
    gfxm::mat4 projection = gfxm::perspective(gfxm::radian(90.0f), 1.0f, 0.1f, 10.0f);

    glUniformMatrix4fv(glGetUniformLocation(progid, "matProjection"), 1, GL_FALSE, (float*)&projection);
    for (int i = 0; i < 6; ++i) {
        glUniformMatrix4fv(glGetUniformLocation(progid, "matView"), 1, GL_FALSE, (float*)&views[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemap_out, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glDeleteFramebuffers(1, &fbo);
}

void cubemapPrefilterConvolute(GLuint vao_cube, GLuint progid, GLuint cubemap_in, GLuint cubemap_out, int width, int height) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLenum draw_buffers[] = {
        GL_COLOR_ATTACHMENT0
    };
    glDrawBuffers(1, draw_buffers);

    glActiveTexture(GL_TEXTURE0 + 8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_in);
    glBindVertexArray(vao_cube);
    glFrontFace(GL_CW);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progid);
    gfxm::mat4 views[6] = {
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 1.f,  .0f,  .0f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3(-1.f,  .0f,  .0f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  1.f,  .0f), gfxm::vec3(.0f,  .0f,  1.f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f, -1.f,  .0f), gfxm::vec3(.0f,  .0f, -1.f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  .0f,  1.f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  .0f, -1.f), gfxm::vec3(.0f, -1.f,  .0f)),
    };
    gfxm::mat4 projection = gfxm::perspective(gfxm::radian(90.0f), 1.0f, 0.1f, 10.0f);

    glUniformMatrix4fv(glGetUniformLocation(progid, "matProjection"), 1, GL_FALSE, (float*)&projection);
    const int nMipLevels = 5;
    for (int mip = 0; mip < nMipLevels; ++mip) {
        int mipW = width * powf(.5f, mip);
        int mipH = height * powf(.5f, mip);
        glViewport(0, 0, mipW, mipH);

        float roughness = (float)mip / (float)(nMipLevels - 1);
        glUniform1f(glGetUniformLocation(progid, "roughness"), roughness);
        for (int i = 0; i < 6; ++i) {
            glUniformMatrix4fv(glGetUniformLocation(progid, "matView"), 1, GL_FALSE, (float*)&views[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemap_out, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glDeleteFramebuffers(1, &fbo);
}

void cubemapFromHdri(GLuint vao_cube, GLuint progid, GLuint tex_hdri, GLuint cubemap_out, int width, int height) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLenum draw_buffers[] = {
        GL_COLOR_ATTACHMENT0
    };
    glDrawBuffers(1, draw_buffers);

    glActiveTexture(GL_TEXTURE0 + 12);
    glBindTexture(GL_TEXTURE_2D, tex_hdri);
    glViewport(0, 0, width, height);
    glBindVertexArray(vao_cube);
    glFrontFace(GL_CW);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progid);
    gfxm::mat4 views[6] = {
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 1.f,  .0f,  .0f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3(-1.f,  .0f,  .0f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  1.f,  .0f), gfxm::vec3(.0f,  .0f,  1.f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f, -1.f,  .0f), gfxm::vec3(.0f,  .0f, -1.f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  .0f,  1.f), gfxm::vec3(.0f, -1.f,  .0f)),
        gfxm::lookAt(gfxm::vec3(.0f, .0f, .0f), gfxm::vec3( 0.f,  .0f, -1.f), gfxm::vec3(.0f, -1.f,  .0f)),
    };
    gfxm::mat4 projection = gfxm::perspective(gfxm::radian(90.0f), 1.0f, 0.1f, 10.0f);

    glUniformMatrix4fv(glGetUniformLocation(progid, "matProjection"), 1, GL_FALSE, (float*)&projection);
    for (int i = 0; i < 6; ++i) {
        glUniformMatrix4fv(glGetUniformLocation(progid, "matView"), 1, GL_FALSE, (float*)&views[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemap_out, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glDeleteFramebuffers(1, &fbo);

    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_out);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void glxBindTextureSlot(GLenum target, int slot, GLuint texture) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(target, texture);
}
void glxBindTexture2dSlot(int slot, GLuint texture) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture);
}
void glxBindCubemapSlot(int slot, GLuint cubemap) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
}

GLuint loadRGBATexture(const char* filename) {
    GLuint texture;
    stbi_set_flip_vertically_on_load(true);
    int w, h, comp;
    stbi_uc* data = stbi_load(filename, &w, &h, &comp, 4);
    if (!data) {
        LOG_ERR("gl/textures", "Failed to load texture " << filename);
        return 0;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return texture;
}

GLuint loadRGBTexture(const char* filename) {
    GLuint texture;
    stbi_set_flip_vertically_on_load(true);
    int w, h, comp;
    stbi_uc* data = stbi_load(filename, &w, &h, &comp, 3);
    if (!data) {
        LOG_ERR("gl/textures", "Failed to load texture " << filename);
        return 0;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return texture;
}

GLuint loadSingleChannelTexture(const char* filename) {
    GLuint texture;
    stbi_set_flip_vertically_on_load(true);
    int w, h, comp;
    stbi_uc* data = stbi_load(filename, &w, &h, &comp, 1);
    if (!data) {
        LOG_ERR("gl/textures", "Failed to load texture " << filename);
        return 0;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return texture;
}

struct GlPbrTextures {
    GLuint albedo;
    GLuint normal;
    GLuint roughness;
    GLuint metallic;
    GLuint ao;
    GLuint emission;
};

GlPbrTextures loadPbrTextures(
    const char* albedo,
    const char* normal,
    const char* roughness,
    const char* metallic,
    const char* ao = 0,
    const char* emission = 0
) {
    GlPbrTextures textures = { 0 };
    textures.albedo = loadRGBATexture(albedo);
    textures.normal = loadRGBTexture(normal);
    textures.roughness = loadSingleChannelTexture(roughness);
    if (metallic) {
        textures.metallic = loadSingleChannelTexture(metallic);
    }
    if (ao) {
        textures.ao = loadSingleChannelTexture(ao);
    }
    if (emission) {
        textures.emission = loadRGBTexture(emission);
    }
    return textures;
}

enum DRAW_CMD_TYPE {
    DRAW_CMD_LINEAR,
    DRAW_CMD_INDEXED,
    DRAW_CMD_LINEAR_INSTANCED,
    DRAW_CMD_INDEXED_INSTANCED
};

constexpr int MAX_CUSTOM_UNIFORM_BUFFERS = 16;
constexpr int MAX_DRAW_CMD_TEXTURES = 16;
struct DrawCmd {
    DRAW_CMD_TYPE type;
    GLenum mode;
    GLuint vao;
    GLuint progid;
    GLint offset;
    GLsizei count;
    GLsizei instance_count;
    GLuint ub_model;
    GLuint uniform_buffers[MAX_CUSTOM_UNIFORM_BUFFERS];
    uint32_t num_uniform_buffers;
    GLuint textures[MAX_DRAW_CMD_TEXTURES];
    uint32_t num_textures;
};

#include "sampler_set.hpp"


SamplerArray makeDrawParameters(ShaderProgram* prog, SamplerSet* material_samplers, SamplerSet* frame_samplers, ColorOutputSet* output_textures) {
    SamplerArray params = { 0 };

    for (int i = 0; i < prog->samplerCount(); ++i) {
        const char* name = prog->getSamplerName(i);

        // TODO: Check that the shader does not try to write to an output

        if (material_samplers) {
            auto it = material_samplers->texture_map.find(name);
            if (it != material_samplers->texture_map.end()) {
                params.textures[i].target = it->second.target;
                params.textures[i].texture = it->second.texture;
                continue;
            }
        }
        if (frame_samplers) {
            auto it2 = frame_samplers->texture_map.find(name);
            if (it2 != frame_samplers->texture_map.end()) {
                params.textures[i].target = it2->second.target;
                params.textures[i].texture = it2->second.texture;
                continue;
            }
        }

        LOG_WARN("gl/sampler_set", "Sampler '" << name << "' not provided by either material or frame SamplerSet");
        params.textures[i].target = GL_TEXTURE_2D;
        params.textures[i].texture = 0; // Better a black texture than something random leftover
    }
    params.texture_count = prog->samplerCount();
    return params;
}

void bindSamplers(const SamplerArray* samplers) {
    for (int i = 0; i < samplers->texture_count; ++i) {
        glxBindTextureSlot(samplers->textures[i].target, i, samplers->textures[i].texture);
    }
}

int main() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	LOG("startup", "Hello, World!");
	LOG("startup", "Working dir is: " << fsGetCurrentDirectory().c_str());
    createWindowOpenGl(1280, 720, false);
    /*
    auto progid = loadShader("shaders/geometry.glsl");
    auto prog_skybox = loadShader("shaders/skybox.glsl");
    auto prog_light_direct = loadShader("shaders/light_direct.glsl");
    auto prog_environment = loadShader("shaders/environment.glsl");
    auto prog_compose = loadShader("shaders/compose.glsl");
    auto prog_present = loadShader("shaders/present.glsl");
    auto prog_present_depth = loadShader("shaders/present_depth.glsl");
    */

    const int gbuffer_width = 1280;
    const int gbuffer_height = 720;

    GLuint fbtex_albedo = createFramebufferTexture2d(gbuffer_width, gbuffer_height, GL_RGB);
    GLuint fbtex_normal = createFramebufferTexture2d(gbuffer_width, gbuffer_height, GL_RGB);
    GLuint fbtex_worldpos = createFramebufferTexture2d(gbuffer_width, gbuffer_height, GL_RGB32F);
    GLuint fbtex_roughness = createFramebufferTexture2d(gbuffer_width, gbuffer_height, GL_RED);
    GLuint fbtex_metallic = createFramebufferTexture2d(gbuffer_width, gbuffer_height, GL_RED);
    GLuint fbtex_emission = createFramebufferTexture2d(gbuffer_width, gbuffer_height, GL_RGB);
    GLuint fbtex_lightness = createFramebufferTexture2d(gbuffer_width, gbuffer_height, GL_RGB32F);
    GLuint fbtex_depth = createFramebufferDepthTexture2d(gbuffer_width, gbuffer_height);
    GLuint fbtex_final = createFramebufferTexture2d(gbuffer_width, gbuffer_height, GL_RGB);

    ColorOutputSet outGBuffer = ColorOutputSet()
        .color("outAlbedo", fbtex_albedo)
        .color("outNormal", fbtex_normal)
        .color("outWorldPos", fbtex_worldpos)
        .color("outRoughness", fbtex_roughness)
        .color("outMetallic", fbtex_metallic)
        .color("outEmission", fbtex_emission)
        .color("outLightness", fbtex_lightness)
        .depth(fbtex_depth);
    ColorOutputSet outLighting = ColorOutputSet()
        .color("outLightness", fbtex_lightness);
    ColorOutputSet outSkybox = ColorOutputSet()
        .color("outFinal", fbtex_final)
        .depth(fbtex_depth);
    ColorOutputSet outCompose = ColorOutputSet()
        .color("outFinal", fbtex_final);

    GLuint fbo = makeFbo(&outGBuffer);
    GLuint fbo_lighting = makeFbo(&outLighting);
    GLuint fbo_skybox = makeFbo(&outSkybox);
    GLuint fbo_compose = makeFbo(&outCompose);

    ShaderProgram* prog_geom = loadShaderProgram("shaders/geometry.glsl", &outGBuffer);
    ShaderProgram* prog_skybox = loadShaderProgram("shaders/skybox.glsl", &outCompose);
    ShaderProgram* prog_light_direct = loadShaderProgram("shaders/light_direct.glsl", &outLighting);
    ShaderProgram* prog_environment = loadShaderProgram("shaders/environment.glsl", &outLighting);
    ShaderProgram* prog_compose = loadShaderProgram("shaders/compose.glsl", &outCompose);
    ShaderProgram* prog_present = loadShaderProgram("shaders/present.glsl", 0);
    ShaderProgram* prog_present_depth = loadShaderProgram("shaders/present_depth.glsl", 0);

    auto pbr_textures = loadPbrTextures(
        "textures/foil003/albedo.png",
        "textures/foil003/normal.png",
        "textures/foil003/roughness.png",
        "textures/foil003/metallic.png",
        "textures/foil003/ao.png",
        0
    );

    float vertices[] = {
        -.5f, -.5f, .5f,    .5f, -.5f, .5f,     -.5f,  .5f, .5f,
         .5f, -.5f, .5f,    .5f, .5f,  .5f,     -.5f,  .5f, .5f,

         .5f, -.5f, .5f,    .5f, -.5f, -.5f,     .5f,  .5f, .5f,
         .5f, -.5f, -.5f,   .5f,  .5f, -.5f,     .5f,  .5f, .5f,

         .5f, -.5f, -.5f,  -.5f, -.5f, -.5f,     .5f,  .5f, -.5f,
        -.5f, -.5f, -.5f,  -.5f,  .5f, -.5f,     .5f,  .5f, -.5f,

        -.5f, -.5f, -.5f,  -.5f, -.5f,  .5f,    -.5f,  .5f, -.5f,
        -.5f, -.5f,  .5f,  -.5f,  .5f,  .5f,    -.5f,  .5f, -.5f,

        -.5f,  .5f,  .5f,   .5f,  .5f,  .5f,    -.5f,  .5f, -.5f,
         .5f,  .5f,  .5f,   .5f,  .5f, -.5f,    -.5f,  .5f, -.5f,

        -.5f, -.5f, -.5f,   .5f, -.5f, -.5f,    -.5f, -.5f,  .5f,
         .5f, -.5f, -.5f,   .5f, -.5f,  .5f,    -.5f, -.5f,  .5f
    };
    float normals[] = {
        .0f, .0f, 1.f,  .0f, .0f, 1.f,  .0f, .0f, 1.f,
        .0f, .0f, 1.f,  .0f, .0f, 1.f,  .0f, .0f, 1.f,

        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,
        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,

        .0f, .0f, -1.f,  .0f, .0f, -1.f,  .0f, .0f, -1.f,
        .0f, .0f, -1.f,  .0f, .0f, -1.f,  .0f, .0f, -1.f,

        -1.f, .0f, .0f,  -1.f, .0f, .0f,  -1.f, .0f, .0f,
        -1.f, .0f, .0f,  -1.f, .0f, .0f,  -1.f, .0f, .0f,

        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,
        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,

        .0f, -1.f, .0f,   .0f, -1.f, .0f,   .0f, -1.f, .0f,
        .0f, -1.f, .0f,   .0f, -1.f, .0f,   .0f, -1.f, .0f,
    };
    float tangents[] = {
        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,
        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,

        .0f, .0f, -1.f,  .0f, .0f, -1.f,  .0f, .0f, -1.f,
        .0f, .0f, -1.f,  .0f, .0f, -1.f,  .0f, .0f, -1.f,

        -1.f, .0f, .0f,  -1.f, .0f, .0f,  -1.f, .0f, .0f,
        -1.f, .0f, .0f,  -1.f, .0f, .0f,  -1.f, .0f, .0f,

        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,
        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,

        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,
        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,

        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,
        1.f, .0f, .0f,  1.f, .0f, .0f,  1.f, .0f, .0f,
    };
    float bitangents[] = {
        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,
        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,

        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,
        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,

        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,
        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,

        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,
        .0f, 1.f, .0f,   .0f, 1.f, .0f,   .0f, 1.f, .0f,

        .0f, .0f, -1.f,  .0f, .0f, -1.f,  .0f, .0f, -1.f,
        .0f, .0f, -1.f,  .0f, .0f, -1.f,  .0f, .0f, -1.f,

        .0f, .0f, 1.f,  .0f, .0f, 1.f,  .0f, .0f, 1.f,
        .0f, .0f, 1.f,  .0f, .0f, 1.f,  .0f, .0f, 1.f,
    };
    float uvs[] = {
        .0f, .0f,   1.f, .0f,   .0f, 1.f,
        1.f, .0f,   1.f, 1.f,   .0f, 1.f,

        .0f, .0f,   1.f, .0f,   .0f, 1.f,
        1.f, .0f,   1.f, 1.f,   .0f, 1.f,

        .0f, .0f,   1.f, .0f,   .0f, 1.f,
        1.f, .0f,   1.f, 1.f,   .0f, 1.f,

        .0f, .0f,   1.f, .0f,   .0f, 1.f,
        1.f, .0f,   1.f, 1.f,   .0f, 1.f,

        .0f, .0f,   1.f, .0f,   .0f, 1.f,
        1.f, .0f,   1.f, 1.f,   .0f, 1.f,

        .0f, .0f,   1.f, .0f,   .0f, 1.f,
        1.f, .0f,   1.f, 1.f,   .0f, 1.f
    };
    uint32_t colors[] = {
        0xFF9933CC, 0xFF99CC33, 0xFFCC3366,
        0xFF99CC33, 0xFF9933CC, 0xFFCC3366,

        0xFF9933CC, 0xFF99CC33, 0xFFCC3366,
        0xFF99CC33, 0xFF9933CC, 0xFFCC3366,

        0xFF9933CC, 0xFF99CC33, 0xFFCC3366,
        0xFF99CC33, 0xFF9933CC, 0xFFCC3366,

        0xFF9933CC, 0xFF99CC33, 0xFFCC3366,
        0xFF99CC33, 0xFF9933CC, 0xFFCC3366,

        0xFF9933CC, 0xFF99CC33, 0xFFCC3366,
        0xFF99CC33, 0xFF9933CC, 0xFFCC3366,

        0xFF9933CC, 0xFF99CC33, 0xFFCC3366,
        0xFF99CC33, 0xFF9933CC, 0xFFCC3366
    };

    GLuint vbo_vertices = glxCreateArrayBuffer(sizeof(vertices), vertices, GL_STATIC_DRAW);
    GLuint vbo_normals = glxCreateArrayBuffer(sizeof(normals), normals, GL_STATIC_DRAW);
    GLuint vbo_tangents = glxCreateArrayBuffer(sizeof(tangents), tangents, GL_STATIC_DRAW);
    GLuint vbo_bitangents = glxCreateArrayBuffer(sizeof(bitangents), bitangents, GL_STATIC_DRAW);
    GLuint vbo_uvs = glxCreateArrayBuffer(sizeof(uvs), uvs, GL_STATIC_DRAW);
    GLuint vbo_colors = glxCreateArrayBuffer(sizeof(colors), colors, GL_STATIC_DRAW);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glxEnableVertexAttrib(0, vbo_vertices, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glxEnableVertexAttrib(1, vbo_normals, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glxEnableVertexAttrib(2, vbo_tangents, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glxEnableVertexAttrib(3, vbo_bitangents, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glxEnableVertexAttrib(4, vbo_uvs, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glxEnableVertexAttrib(5, vbo_colors, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
    glBindVertexArray(0);

    UniformBufferCommon ub_common_data;
    UniformBufferModel ub_model_data;

    GLuint ub_model;
    glGenBuffers(1, &ub_model);
    glBindBuffer(GL_UNIFORM_BUFFER, ub_model);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(ub_model_data), &ub_model_data, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    GLuint ub_common;
    glGenBuffers(1, &ub_common);
    glBindBuffer(GL_UNIFORM_BUFFER, ub_common);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(ub_common_data), &ub_common_data, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    
    GLuint vao_screen_triangle;
    {
        float vertices[] = {
            -1.f, -1.f, .0f,    3.f, -1.f, .0f,     -1.f, 3.f, .0f
        };
        GLuint vbo_vertices = glxCreateArrayBuffer(sizeof(vertices), vertices, GL_STATIC_DRAW);

        glGenVertexArrays(1, &vao_screen_triangle);
        glBindVertexArray(vao_screen_triangle);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_vertices);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);
    }

    GLuint cubemap;
    GLuint tex_hdri;
    GLuint cubemap2;
    GLuint cubemap_irradiance;
    GLuint cubemap_specular;
    GLuint tex_brdf;
    {
        const int convoluted_width = 32;
        const int convoluted_height = 32;

        glGenTextures(1, &cubemap);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        const char* paths[] = {
            "cubemaps/Lycksele2/posx.jpg",
            "cubemaps/Lycksele2/negx.jpg",
            "cubemaps/Lycksele2/posy.jpg",
            "cubemaps/Lycksele2/negy.jpg",
            "cubemaps/Lycksele2/posz.jpg",
            "cubemaps/Lycksele2/negz.jpg"
        };
        stbi_set_flip_vertically_on_load(false);
        for (int i = 0; i < 6; ++i) {
            int w, h, comp;
            stbi_uc* data = stbi_load(paths[i], &w, &h, &comp, 3);
            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
                data
            );
            stbi_image_free(data);
        }
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        {
            glGenTextures(1, &tex_hdri);
            
            stbi_set_flip_vertically_on_load(true);
            int width, height, ncomp;
            float* data = stbi_loadf("hdri/studio_small_02_1k.hdr", &width, &height, &ncomp, 3);
            if (data) {
                glBindTexture(GL_TEXTURE_2D, tex_hdri);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glGenerateMipmap(GL_TEXTURE_2D);

                stbi_image_free(data);
            }

            auto prog_hdri_to_cubemap = loadShader("shaders/hdri_to_cubemap.glsl");
            cubemap2 = createCubeMap(512, 512, GL_RGB16F);
            cubemapFromHdri(vao, prog_hdri_to_cubemap, tex_hdri, cubemap2, 512, 512);
        }

        auto prog_brdf = loadShader("shaders/integrate_brdf.glsl");
        tex_brdf = createFramebufferTexture2d(512, 512, GL_RG16F);
        GLuint fbo;
        {
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_brdf, 0);
            GLenum draw_buffers[] = {
                GL_COLOR_ATTACHMENT0,
            };
            glDrawBuffers(1, draw_buffers);
            if (!glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                LOG_ERR("gl/framebuffer", "Framebuffer is incomplete");
                return -1;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        glFrontFace(GL_CCW);
        glUseProgram(prog_brdf);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindVertexArray(vao_screen_triangle);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindVertexArray(0);

        auto prog_convolute = loadShader("shaders/convolute_cubemap.glsl");
        cubemap_irradiance = createCubeMap(convoluted_width, convoluted_height, GL_RGB16F);
        cubemapConvolute(vao, prog_convolute, cubemap2, cubemap_irradiance, convoluted_width, convoluted_height);
    
        auto prog_prefilter_convolute = loadShader("shaders/prefilter_convolute_cubemap.glsl");
        cubemap_specular = createSpecularCubeMap(128, 128, GL_RGB16F);
        cubemapPrefilterConvolute(vao, prog_prefilter_convolute, cubemap2, cubemap_specular, 128, 128);
    }

    SamplerSet samplersGeom = SamplerSet()
        .setSampler("texDiffuse", GL_TEXTURE_2D, pbr_textures.albedo)
        .setSampler("texNormal", GL_TEXTURE_2D, pbr_textures.normal)
        .setSampler("texRoughness", GL_TEXTURE_2D, pbr_textures.roughness)
        .setSampler("texMetallic", GL_TEXTURE_2D, pbr_textures.metallic)
        .setSampler("texAmbientOcclusion", GL_TEXTURE_2D, pbr_textures.ao);
    SamplerSet samplersIBL = SamplerSet()
        .setSampler("texDiffuse", GL_TEXTURE_2D, fbtex_albedo)
        .setSampler("texWorldPos", GL_TEXTURE_2D, fbtex_worldpos)
        .setSampler("texNormal", GL_TEXTURE_2D, fbtex_normal)
        .setSampler("texRoughness", GL_TEXTURE_2D, fbtex_roughness)
        .setSampler("texMetallic", GL_TEXTURE_2D, fbtex_metallic)
        .setSampler("texBrdfLut", GL_TEXTURE_2D, tex_brdf)
        .setSampler("cubemapIrradiance", GL_TEXTURE_CUBE_MAP, cubemap_irradiance)
        .setSampler("cubemapSpecular", GL_TEXTURE_CUBE_MAP, cubemap_specular);
    SamplerSet samplersCompose = SamplerSet()
        .setSampler("texDiffuse", GL_TEXTURE_2D, fbtex_albedo)
        .setSampler("texNormal", GL_TEXTURE_2D, fbtex_normal)
        .setSampler("texLightness", GL_TEXTURE_2D, fbtex_lightness)
        .setSampler("texEmission", GL_TEXTURE_2D, fbtex_emission)
        .setSampler("texDepth", GL_TEXTURE_2D, fbtex_depth);
    SamplerSet samplersSkybox = SamplerSet()
        .setSampler("cubemapEnvironment", GL_TEXTURE_CUBE_MAP, cubemap2);

    SamplerArray saGeom = makeDrawParameters(prog_geom, &samplersGeom, 0, &outGBuffer);
    SamplerArray saIBL = makeDrawParameters(prog_environment, &samplersIBL, 0, &outLighting);
    SamplerArray saCompose = makeDrawParameters(prog_compose, &samplersCompose, 0, &outCompose);
    SamplerArray saSkybox = makeDrawParameters(prog_skybox, &samplersSkybox, 0, &outSkybox);

    std::vector<DrawCmd> draw_commands;/*
    draw_commands.push_back(DrawCmd{
        .type = DRAW_CMD_LINEAR,
        .mode = GL_TRIANGLES,
        .vao = vao,
        .progid = progid,
        .offset = 0,
        .count = 36,
        .instance_count = 0,
        .ub_model = ub_model,
    });*/
    
    const float pipe_radius = .15f;
    const int torus_segments = 200;
    const int pipe_segments = 16;
    GLuint vao_lines = 0;
    {
        auto torusKnot = [](float& x, float& y, float& z, float t) {
            const float p = 5;// -3.f;
            const float q = 4;// 2.f;

            float r = .5f * (2.f + sinf(q * t));
            x = r * cosf(p * t);
            y = .5f * r * cosf(q * t);
            z = r * sinf(p * t);
        };

        std::vector<gfxm::vec3> vertices;
        std::vector<gfxm::vec3> normals;
        std::vector<gfxm::vec3> tangents;
        std::vector<gfxm::vec3> bitangents;
        std::vector<gfxm::vec2> uvs;
        std::vector<uint32_t> colors;
        for (int i = 0; i < torus_segments; ++i) {
            float t0 = i / (float)torus_segments * gfxm::pi2;
            float t1 = (i + 1) / (float)torus_segments * gfxm::pi2;
            float t2 = (i + 2) / (float)torus_segments * gfxm::pi2;
            gfxm::vec3 v0, v1, v2;
            torusKnot(v0.x, v0.y, v0.z, t0);
            torusKnot(v1.x, v1.y, v1.z, t1);
            torusKnot(v2.x, v2.y, v2.z, t2);
            
            gfxm::vec3 trZ0 = gfxm::normalize(v1 - v0);
            gfxm::vec3 trX0 = gfxm::normalize(v0);
            gfxm::vec3 trY0 = gfxm::normalize(gfxm::cross(trZ0, trX0));
            trX0 = gfxm::normalize(gfxm::cross(trY0, trZ0));
            gfxm::mat4 tr0 = gfxm::mat4(trX0, trY0, trZ0);
            gfxm::vec3 trZ1 = gfxm::normalize(v2 - v1);
            gfxm::vec3 trX1 = gfxm::normalize(v1);
            gfxm::vec3 trY1 = gfxm::normalize(gfxm::cross(trZ1, trX1));
            trX1 = gfxm::normalize(gfxm::cross(trY1, trZ1));
            gfxm::mat4 tr1 = gfxm::mat4(trX1, trY1, trZ1);
            
            for (int j = 0; j < pipe_segments; ++j) {
                float th0 = j / (float)pipe_segments * -gfxm::pi2;
                float th1 = (j + 1) / (float)pipe_segments * -gfxm::pi2;
                gfxm::vec3 vv0 = gfxm::vec3(cosf(th0) * pipe_radius, sinf(th0) * pipe_radius, .0f);
                gfxm::vec3 vv1 = gfxm::vec3(cosf(th1) * pipe_radius, sinf(th1) * pipe_radius, .0f);

                gfxm::vec3 vv2 = v1 + gfxm::vec3(tr1 * gfxm::vec4(vv0, 1.f));
                gfxm::vec3 vv3 = v1 + gfxm::vec3(tr1 * gfxm::vec4(vv1, 1.f));
                vv0 = v0 + gfxm::vec3(tr0 * gfxm::vec4(vv0, 1.f));
                vv1 = v0 + gfxm::vec3(tr0 * gfxm::vec4(vv1, 1.f));
                vertices.push_back(vv0);
                vertices.push_back(vv2);
                vertices.push_back(vv1);
                vertices.push_back(vv3);
                normals.push_back(gfxm::normalize(vv0 - v0));
                normals.push_back(gfxm::normalize(vv2 - v1));
                normals.push_back(gfxm::normalize(vv1 - v0));
                normals.push_back(gfxm::normalize(vv3 - v1));
                tangents.push_back(gfxm::normalize(gfxm::cross(trZ0, vv0 - v0)));
                tangents.push_back(gfxm::normalize(gfxm::cross(trZ0, vv0 - v0)));
                tangents.push_back(gfxm::normalize(gfxm::cross(trZ0, vv0 - v0)));
                tangents.push_back(gfxm::normalize(gfxm::cross(trZ0, vv0 - v0)));
                bitangents.push_back(trZ0);
                bitangents.push_back(trZ0);
                bitangents.push_back(trZ0);
                bitangents.push_back(trZ0);
                uvs.push_back(gfxm::vec2(th1 / -gfxm::pi2, t0 / gfxm::pi2 * 12.f * 2.f));
                uvs.push_back(gfxm::vec2(th1 / -gfxm::pi2, t1 / gfxm::pi2 * 12.f * 2.f));
                uvs.push_back(gfxm::vec2(th0 / -gfxm::pi2, t0 / gfxm::pi2 * 12.f * 2.f));
                uvs.push_back(gfxm::vec2(th0 / -gfxm::pi2, t1 / gfxm::pi2 * 12.f * 2.f));
                colors.push_back(hsv2rgb(sinf(t0 * .5f) * .1f + .1f, .9f, .80f));
                colors.push_back(hsv2rgb(sinf(t1 * .5f) * .1f + .1f, .9f, .80f));
                colors.push_back(hsv2rgb(sinf(t0 * .5f) * .1f + .1f, .9f, .80f));
                colors.push_back(hsv2rgb(sinf(t1 * .5f) * .1f + .1f, .9f, .80f));
            }
        }

        GLuint vbo_vertices = glxCreateArrayBuffer(sizeof(vertices[0]) * vertices.size(), vertices.data(), GL_STATIC_DRAW);
        GLuint vbo_normals = glxCreateArrayBuffer(sizeof(normals[0]) * normals.size(), normals.data(), GL_STATIC_DRAW);
        GLuint vbo_tangents = glxCreateArrayBuffer(sizeof(tangents[0]) * tangents.size(), tangents.data(), GL_STATIC_DRAW);
        GLuint vbo_bitangents = glxCreateArrayBuffer(sizeof(bitangents[0]) * bitangents.size(), bitangents.data(), GL_STATIC_DRAW);
        GLuint vbo_uvs = glxCreateArrayBuffer(sizeof(vertices[0]) * vertices.size(), uvs.data(), GL_STATIC_DRAW);
        GLuint vbo_colors = glxCreateArrayBuffer(sizeof(colors[0]) * colors.size(), colors.data(), GL_STATIC_DRAW);

        glGenVertexArrays(1, &vao_lines);
        glBindVertexArray(vao_lines);
        glxEnableVertexAttrib(0, vbo_vertices, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glxEnableVertexAttrib(1, vbo_normals, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glxEnableVertexAttrib(2, vbo_tangents, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glxEnableVertexAttrib(3, vbo_bitangents, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glxEnableVertexAttrib(4, vbo_uvs, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glxEnableVertexAttrib(5, vbo_colors, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
        glBindVertexArray(0);
    }
    draw_commands.push_back(DrawCmd{
        .type = DRAW_CMD_LINEAR,
        .mode = GL_TRIANGLE_STRIP,
        .vao = vao_lines,
        .progid = prog_geom->id(),
        .offset = 0,
        .count = torus_segments * pipe_segments * 4,
        .instance_count = 0,
        .ub_model = ub_model,
    });
    /*
    GLuint fbo;
    {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0, GL_TEXTURE_2D, fbtex_albedo, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 1, GL_TEXTURE_2D, fbtex_normal, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 2, GL_TEXTURE_2D, fbtex_worldpos, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 3, GL_TEXTURE_2D, fbtex_roughness, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 4, GL_TEXTURE_2D, fbtex_metallic, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 5, GL_TEXTURE_2D, fbtex_emission, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fbtex_depth, 0);
        GLenum draw_buffers[] = {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT0 + 1,
            GL_COLOR_ATTACHMENT0 + 2,
            GL_COLOR_ATTACHMENT0 + 3,
            GL_COLOR_ATTACHMENT0 + 4,
            GL_COLOR_ATTACHMENT0 + 5,
            GL_NONE,
            GL_NONE,
        };
        glDrawBuffers(8, draw_buffers);
        if (!glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERR("gl/framebuffer", "Framebuffer is incomplete");
            return -1;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    GLuint fbo_lighting;
    {
        glGenFramebuffers(1, &fbo_lighting);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_lighting);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 6, GL_TEXTURE_2D, fbtex_lightness, 0);
        GLenum draw_buffers[] = {
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_COLOR_ATTACHMENT0 + 6,
            GL_NONE,
        };
        glDrawBuffers(8, draw_buffers);
        if (!glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERR("gl/framebuffer", "Framebuffer is incomplete");
            return -1;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    GLuint fbo_compose;
    {
        glGenFramebuffers(1, &fbo_compose);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_compose);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 7, GL_TEXTURE_2D, fbtex_final, 0);
        GLenum draw_buffers[] = {
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_COLOR_ATTACHMENT0 + 7,
        };
        glDrawBuffers(8, draw_buffers);
        if (!glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERR("gl/framebuffer", "Framebuffer is incomplete");
            return -1;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    GLuint fbo_skybox;
    {
        glGenFramebuffers(1, &fbo_skybox);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_skybox);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 7, GL_TEXTURE_2D, fbtex_final, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fbtex_depth, 0);
        GLenum draw_buffers[] = {
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_NONE,
            GL_COLOR_ATTACHMENT0 + 7,
        };
        glDrawBuffers(8, draw_buffers);
        if (!glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERR("gl/framebuffer", "Framebuffer is incomplete");
            return -1;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }*/

    if (GL_NO_ERROR != glGetError()) {
        assert(false);
    }

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    
    float time = .0f;
    while (pollMessages()) {
        PROF_SCOPE("GameLoop");

        PROF_BEGIN("PrepareStateAndClear");
        // #6b489f
        //glClearColor(0x2b / 255.f, 0x18 / 255.f, 0x3f / 255.f, 1.f);
        glClearColor(0, 0, 0, 0);
        glFrontFace(GL_CCW);
        glEnable(GL_CULL_FACE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_SCISSOR_TEST);
        glDisable(GL_LINE_SMOOTH);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);

        gfxm::vec3 camera_pivot = gfxm::vec3(0, 0, 0);
        float camera_distance = 5.0f;
        gfxm::quat q 
            = gfxm::angle_axis(camera_rotation_y, gfxm::vec3(0, -1, 0))
            * gfxm::angle_axis(camera_rotation_x, gfxm::vec3(-1, 0, 0));
        gfxm::mat4 matCamera = gfxm::to_mat4(q);
        gfxm::vec3 cameraPosition = camera_pivot + gfxm::vec3(matCamera[2] * camera_distance);
        matCamera = gfxm::translate(matCamera, camera_pivot + gfxm::vec3(0, 0, 1) * camera_distance);

        float znear = 0.01f;
        float zfar = 1000.0f;
        //ub_common_data.matView = (gfxm::lookAt(cameraPosition, gfxm::vec3(0, 0, 0), gfxm::vec3(0, 1, 0)));
        ub_common_data.matView = gfxm::inverse(matCamera);
        ub_common_data.matProjection = gfxm::perspective(gfxm::radian(60.f), s_window_width / (float)s_window_height, znear, zfar);
        ub_common_data.cameraPosition = cameraPosition;
        ub_common_data.time = time;
        ub_common_data.viewportSize = gfxm::vec2(s_window_width, s_window_height);
        ub_common_data.zNear = znear;
        ub_common_data.zFar = zfar;
        glBindBuffer(GL_UNIFORM_BUFFER, ub_common);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(ub_common_data), &ub_common_data, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        
        //ub_model_data.matModel = gfxm::mat4(1.0f);
        ub_model_data.matModel = gfxm::to_mat4(
            gfxm::angle_axis(time, gfxm::vec3(.0f, 1.f, .0f))
            * gfxm::angle_axis(time, gfxm::vec3(1.f, .0f, .0f))
        );
        glBindBuffer(GL_UNIFORM_BUFFER, ub_model);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(ub_model_data), &ub_model_data, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glViewport(0, 0, gbuffer_width, gbuffer_height);
        glScissor(0, 0, gbuffer_width, gbuffer_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glBindBufferBase(GL_UNIFORM_BUFFER, 0, ub_common);
        PROF_END();

        PROF_BEGIN("DrawCommands");
        for (int i = 0; i < draw_commands.size(); ++i) {
            const auto& cmd = draw_commands[i];

            PROF_BEGIN("PrepareState");
            glBindBufferBase(GL_UNIFORM_BUFFER, 1, cmd.ub_model);
            glBindVertexArray(cmd.vao);
            bindSamplers(&saGeom);/*
            glxBindTexture2dSlot(0, pbr_textures.albedo);
            glxBindTexture2dSlot(1, pbr_textures.normal);
            glxBindTexture2dSlot(3, pbr_textures.roughness);
            glxBindTexture2dSlot(4, pbr_textures.metallic);
            glxBindTexture2dSlot(5, pbr_textures.emission);*/
            glUseProgram(cmd.progid);
            PROF_END();

            switch (cmd.type) {
            case DRAW_CMD_LINEAR:
                glDrawArrays(cmd.mode, cmd.offset, cmd.count);
                break;
            case DRAW_CMD_INDEXED:
                glDrawElements(cmd.mode, cmd.count, GL_UNSIGNED_INT, (const GLvoid*)cmd.offset);
                break;
            case DRAW_CMD_LINEAR_INSTANCED:
                glDrawArraysInstanced(cmd.mode, cmd.offset, cmd.count, cmd.instance_count);
                break;
            case DRAW_CMD_INDEXED_INSTANCED:
                glDrawElementsInstanced(cmd.mode, cmd.count, GL_UNSIGNED_INT, (const GLvoid*)cmd.offset, cmd.instance_count);
                break;
            default:
                assert(false);
            }
        }
        PROF_END();

        // Clear the lighting buffer
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_lighting);
        glViewport(0, 0, gbuffer_width, gbuffer_height);
        glScissor(0, 0, gbuffer_width, gbuffer_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        /*
        PROF_BEGIN("Lighting");
        glBlendFunc(GL_ONE, GL_ONE);
        glEnable(GL_BLEND);
        glBindVertexArray(vao_screen_triangle);
        glUseProgram(prog_light_direct);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_lighting);
        glViewport(0, 0, gbuffer_width, gbuffer_height);
        glScissor(0, 0, gbuffer_width, gbuffer_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbtex_albedo);
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, fbtex_worldpos);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, fbtex_normal);
        glActiveTexture(GL_TEXTURE0 + 3);
        glBindTexture(GL_TEXTURE_2D, fbtex_roughness);
        glActiveTexture(GL_TEXTURE0 + 4);
        glBindTexture(GL_TEXTURE_2D, fbtex_metallic);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        PROF_END();*/

        // IBL lighting
        glBlendFunc(GL_ONE, GL_ONE);
        glEnable(GL_BLEND);
        glBindVertexArray(vao_screen_triangle);
        glUseProgram(prog_environment->id());
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_lighting);
        glViewport(0, 0, gbuffer_width, gbuffer_height);
        glScissor(0, 0, gbuffer_width, gbuffer_height);
        bindSamplers(&saIBL);/*
        glxBindTexture2dSlot(0, fbtex_albedo);
        glxBindTexture2dSlot(2, fbtex_worldpos);
        glxBindTexture2dSlot(1, fbtex_normal);
        glxBindTexture2dSlot(3, fbtex_roughness);
        glxBindTexture2dSlot(4, fbtex_metallic);
        glxBindTexture2dSlot(9, tex_brdf);
        glxBindCubemapSlot(10, cubemap_irradiance);
        glxBindCubemapSlot(11, cubemap_specular);*/
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        PROF_BEGIN("Compose");
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glBindVertexArray(vao_screen_triangle);
        glUseProgram(prog_compose->id());
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_compose);
        glViewport(0, 0, gbuffer_width, gbuffer_height);
        glScissor(0, 0, gbuffer_width, gbuffer_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        bindSamplers(&saCompose);
        /*
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbtex_albedo);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, fbtex_normal);
        glActiveTexture(GL_TEXTURE0 + 6);
        glBindTexture(GL_TEXTURE_2D, fbtex_lightness);
        glActiveTexture(GL_TEXTURE0 + 5);
        glBindTexture(GL_TEXTURE_2D, fbtex_emission);
        glActiveTexture(GL_TEXTURE0 + 7);
        glBindTexture(GL_TEXTURE_2D, fbtex_depth);*/
        glDrawArrays(GL_TRIANGLES, 0, 3);
        PROF_END();

        // Skybox
        glFrontFace(GL_CW);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(vao);
        glUseProgram(prog_skybox->id());
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_skybox);
        glViewport(0, 0, gbuffer_width, gbuffer_height);
        glScissor(0, 0, gbuffer_width, gbuffer_height);
        bindSamplers(&saSkybox);
        /*
        glActiveTexture(GL_TEXTURE0 + 8);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap2);*/
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // Present
        glFrontFace(GL_CCW);
        glBindVertexArray(vao_screen_triangle);
        glUseProgram(prog_present->id());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, s_window_width, s_window_height);
        glScissor(0, 0, s_window_width, s_window_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        
        if (!dbgShowGBuffer) {
            glViewport(0, 0, s_window_width, s_window_height);
            glScissor(0, 0, s_window_width, s_window_height);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_final);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        } else {
            glViewport(0, 0, s_window_width / 3, s_window_height / 3);
            glScissor(0, 0, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_final);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glViewport(s_window_width / 3, 0, s_window_width / 3, s_window_height / 3);
            glScissor(s_window_width / 3, 0, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_albedo);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glViewport(s_window_width / 3 * 2, 0, s_window_width / 3, s_window_height / 3);
            glScissor(s_window_width / 3 * 2, 0, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_normal);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glViewport(0, s_window_height / 3, s_window_width / 3, s_window_height / 3);
            glScissor(0, s_window_height / 3, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_worldpos);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glViewport(s_window_width / 3, s_window_height / 3, s_window_width / 3, s_window_height / 3);
            glScissor(s_window_width / 3, s_window_height / 3, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_roughness);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glViewport(s_window_width / 3 * 2, s_window_height / 3, s_window_width / 3, s_window_height / 3);
            glScissor(s_window_width / 3 * 2, s_window_height / 3, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_metallic);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glViewport(0, s_window_height / 3 * 2, s_window_width / 3, s_window_height / 3);
            glScissor(0, s_window_height / 3 * 2, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_emission);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glViewport(s_window_width / 3, s_window_height / 3 * 2, s_window_width / 3, s_window_height / 3);
            glScissor(s_window_width / 3, s_window_height / 3 * 2, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_lightness);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glUseProgram(prog_present_depth->id());
            glViewport(s_window_width / 3 * 2, s_window_height / 3 * 2, s_window_width / 3, s_window_height / 3);
            glScissor(s_window_width / 3 * 2, s_window_height / 3 * 2, s_window_width / 3, s_window_height / 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fbtex_depth);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        PROF_BEGIN("Present");
        SwapBuffers(s_hdc);
        PROF_END();

        // TODO:
        time += 0.01f;
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo_colors);
    glDeleteBuffers(1, &vbo_vertices);

    profilerDump("profile.csv");

	return 0;
}
