#define NOMINMAX
#include <windows.h>
#include <objbase.h>  // For CoInitializeEx
#define GL_GLEXT_LEGACY
#include <GL/GL.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <random>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cwchar>
#include <map>
#include <shlobj.h>
#include "detours/include/detours.h"
#include "JavaDetector.h"

// OpenGL extension constants
// Newer Windows SDK gl.h no longer defines APIENTRYP (it moved to glext.h,
// which GL_GLEXT_LEGACY excludes) — provide the classic fallback here.
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

// wglSwapBuffers function pointer type
typedef BOOL (WINAPI *wglSwapBuffers_t)(HDC hdc);

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "detours.lib")

// Forward declarations for stb_image
extern "C" {
    typedef unsigned char stbi_uc;
    
    struct stbi_gif_frame {
        int delay;
        stbi_uc *data;
    };
    
    struct stbi_gif {
        int w, h, count;
        stbi_gif_frame *frames;
    };
    
    stbi_uc *stbi_load(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void *retval_from_stbi_load);
    stbi_gif *stbi_load_gif(char const *filename, int **delays, int *x, int *y, int *z, int *comp, int req_comp);
    void stbi_gif_free(stbi_gif *gif);
}

// OpenGL constants
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// Forward declarations
struct Waypoint;
class ImageOverlay;
class OverlayManager;

// Movement mode enum
enum class MovementMode {
    NONE,
    WAYPOINT,
    RANDOM_MOVE,
    RANDOM_POSITION
};

// MovementMode <-> string conversion (matches mod's SavedOverlay format)
static const char* movementModeToString(MovementMode m) {
    switch (m) {
        case MovementMode::WAYPOINT: return "WAYPOINT";
        case MovementMode::RANDOM_MOVE: return "RANDOM_MOVE";
        case MovementMode::RANDOM_POSITION: return "RANDOM_POSITION";
        default: return "NONE";
    }
}

static MovementMode movementModeFromString(const std::string& s) {
    if (s == "WAYPOINT") return MovementMode::WAYPOINT;
    if (s == "RANDOM_MOVE") return MovementMode::RANDOM_MOVE;
    if (s == "RANDOM_POSITION") return MovementMode::RANDOM_POSITION;
    return MovementMode::NONE;
}

// Minimal JSON parser (objects, arrays, strings, numbers, bools, null)
// Convert a wide string to UTF-8. The naive wstring->string cast truncates
// every non-ASCII character (e.g. Chinese folder names), which then breaks
// WIC path resolution - dragged images from such folders silently fail.
static std::string WideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &out[0], len, nullptr, nullptr);
    return out;
}

namespace Json {    struct Value {
        enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
        bool b = false;
        double num = 0;
        std::string str;
        std::vector<Value> arr;
        std::vector<std::pair<std::string, Value>> obj;

        const Value* find(const char* key) const {
            if (type != Obj) return nullptr;
            for (auto& kv : obj) if (kv.first == key) return &kv.second;
            return nullptr;
        }
        double getNum(const char* key, double def) const {
            const Value* v = find(key);
            return (v && v->type == Num) ? v->num : def;
        }
        std::string getStr(const char* key, const std::string& def) const {
            const Value* v = find(key);
            return (v && v->type == Str) ? v->str : def;
        }
        bool getBool(const char* key, bool def) const {
            const Value* v = find(key);
            return (v && v->type == Bool) ? v->b : def;
        }
    };

    class Parser {
        const char* p;
    public:
        Parser(const char* s) : p(s) {}
        Value parse() { skipWs(); return parseValue(); }
    private:
        void skipWs() { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; }
        Value parseValue() {
            skipWs();
            if (*p == '{') return parseObj();
            if (*p == '[') return parseArr();
            if (*p == '"') { Value v; v.type = Value::Str; v.str = parseStr(); return v; }
            if (strncmp(p, "true", 4) == 0) { p += 4; Value v; v.type = Value::Bool; v.b = true; return v; }
            if (strncmp(p, "false", 5) == 0) { p += 5; Value v; v.type = Value::Bool; v.b = false; return v; }
            if (strncmp(p, "null", 4) == 0) { p += 4; return Value{}; }
            Value v; v.type = Value::Num;
            v.num = strtod(p, const_cast<char**>(&p));
            return v;
        }
        Value parseObj() {
            Value v; v.type = Value::Obj; p++; skipWs();
            if (*p == '}') { p++; return v; }
            while (*p) {
                skipWs();
                std::string key = parseStr();
                skipWs();
                if (*p == ':') p++;
                Value val = parseValue();
                v.obj.emplace_back(std::move(key), std::move(val));
                skipWs();
                if (*p == ',') { p++; continue; }
                if (*p == '}') { p++; break; }
                break;
            }
            return v;
        }
        Value parseArr() {
            Value v; v.type = Value::Arr; p++; skipWs();
            if (*p == ']') { p++; return v; }
            while (*p) {
                v.arr.push_back(parseValue());
                skipWs();
                if (*p == ',') { p++; continue; }
                if (*p == ']') { p++; break; }
                break;
            }
            return v;
        }
        std::string parseStr() {
            std::string s;
            if (*p == '"') p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                        case 'n': s += '\n'; break;
                        case 't': s += '\t'; break;
                        case 'r': s += '\r'; break;
                        case 'u': {
                            unsigned code = 0;
                            for (int i = 0; i < 4 && isxdigit((unsigned char)p[1]); i++) {
                                p++;
                                code = code * 16 + (isdigit((unsigned char)*p) ? *p - '0' : tolower((unsigned char)*p) - 'a' + 10);
                            }
                            if (code < 0x80) s += (char)code;
                            else if (code < 0x800) {
                                s += (char)(0xC0 | (code >> 6));
                                s += (char)(0x80 | (code & 0x3F));
                            } else {
                                s += (char)(0xE0 | (code >> 12));
                                s += (char)(0x80 | ((code >> 6) & 0x3F));
                                s += (char)(0x80 | (code & 0x3F));
                            }
                            break;
                        }
                        default: s += *p; break;
                    }
                    p++;
                } else {
                    s += *p++;
                }
            }
            if (*p == '"') p++;
            return s;
        }
    };
} // namespace Json



// Waypoint structure
struct Waypoint {
    double xFraction;
    double yFraction;
    Waypoint(double x, double y) : xFraction(x), yFraction(y) {}
};

// Global variables
HMODULE g_hModule = nullptr;
bool g_isDetaching = false;
bool g_isRendering = false;
HWND g_hwnd = nullptr;
HDC g_hdc = nullptr;
HGLRC g_glCtx = nullptr;
int g_screenW = 0;
int g_screenH = 0;
int g_guiScale = 1;            // Minecraft GUI scale factor (version-independent)
// Window-resize proportional scaling: display size = original * scale * g_winScaleRatio,
// where the ratio is current window width / reference width (captured once at startup).
// The saved `scale` never includes the ratio, so restoring the window size restores
// the exact original display size.
double g_winScaleRatio = 1.0;
int g_detectMode = 0;          // 0=unknown 1=JNI 2=cursor-compat
WNDPROC g_origWndProc = nullptr;
bool g_menuEnabled = false;
bool g_chatScreenOpen = false;
bool g_manualEditMode = false; // Manual edit mode toggle (Insert key)

// Context menu state
bool g_showMenu = false;
int g_menuX = 0;
int g_menuY = 0;
std::shared_ptr<ImageOverlay> g_menuTarget = nullptr;

// Context menu geometry + item labels (5 items, mirrors mod's context menu)
static const int MENU_WIDTH = 120;
static const int MENU_ITEM_H = 18;
static const int MENU_ITEM_COUNT = 5;
static const wchar_t* const MENU_ITEMS[MENU_ITEM_COUNT] = {
    L"定点移动",
    L"随机运动",
    L"随机展示",
    L"重置运动",
    L"删除",
};

// Last known mouse position in window coordinates (updated by WndProc hook)
int g_mouseX = 0;
int g_mouseY = 0;

// ImageOverlay class
class ImageOverlay {
public:
    std::string backupFileName;
    std::string originalFileName;
    bool animated = false;
    std::vector<GLuint> frameTextures;
    std::vector<int> frameDelays;
    int totalFrames = 0;
    int originalWidth = 0;
    int originalHeight = 0;

    double xFraction = 0.1;
    double yFraction = 0.1;
    float scale = 1.0f;

    MovementMode movementMode = MovementMode::NONE;
    std::vector<Waypoint> waypoints;
    int currentWaypointIndex = 0;
    double randMoveDX = 0.0;
    double randMoveDY = 0.0;
    long long waypointStaticUntil = 0;

    bool dragging = false;
    bool resizing = false;
    int dragOffsetX = 0;
    int dragOffsetY = 0;

    // Random-move fractional accumulators (see tickMovement RANDOM_MOVE):
    // the mod advances 2px along the direction vector per 50ms client tick;
    // we accumulate sub-pixel movement so an axis with a small |cos/sin|
    // component never freezes.
    double moveAccX = 0.0;
    double moveAccY = 0.0;
    long long lastMoveTickMs = 0;

    int cachedScreenW = 0;
    int cachedScreenH = 0;
    int cachedX = 0;
    int cachedY = 0;
    int cachedDisplayW = 0;
    int cachedDisplayH = 0;

    bool textureNeedsRegistration = true;
    int currentFrameIndex = 0;
    long long frameStartMs = 0;
    int animationLoops = 0;

    static constexpr double MOVE_SPEED = 2.0;

    ImageOverlay(const std::string& backup, const std::string& original,
                 const std::vector<GLuint>& textures, const std::vector<int>& delays,
                 int width, int height)
        : backupFileName(backup), originalFileName(original),
          frameTextures(textures), frameDelays(delays),
          originalWidth(width), originalHeight(height) {
        totalFrames = static_cast<int>(textures.size());
        animated = totalFrames > 1;
        textureNeedsRegistration = true;
        frameStartMs = GetTickCount64();
        generateRandomDirection();
    }

    ~ImageOverlay() {
        releaseTextures();
    }

    void releaseTextures() {
        for (GLuint tex : frameTextures) {
            if (tex != 0) glDeleteTextures(1, &tex);
        }
        frameTextures.clear();
    }

    MovementMode getMovementMode() const { return movementMode; }
    void setMovementMode(MovementMode m) { movementMode = m; }
    bool needsTextureRegistration() const { return textureNeedsRegistration; }
    void markTexturesRegistered() { textureNeedsRegistration = false; }

    GLuint getFrameTexture(int index) const {
        if (frameTextures.empty()) return 0;
        return frameTextures[index % totalFrames];
    }

    int getCurrentFrameIndex() {
        if (!animated) return 0;
        long long now = GetTickCount64();
        int delay = getFrameDelayMs(currentFrameIndex);
        if (delay > 0 && now - frameStartMs >= delay) {
            int prev = currentFrameIndex;
            currentFrameIndex = (currentFrameIndex + 1) % totalFrames;
            frameStartMs = now;
            if (currentFrameIndex < prev) animationLoops++;
        }
        return currentFrameIndex;
    }

    int consumeAnimationLoopCount() {
        int c = animationLoops;
        animationLoops = 0;
        return c;
    }

    int getFrameDelayMs(int index) const {
        if (index < 0 || index >= static_cast<int>(frameDelays.size())) return 100;
        int d = frameDelays[index];
        return d > 10 ? d : 100;
    }

    void updateFromScreenSize(int screenW, int screenH) {
        if (screenW == cachedScreenW && screenH == cachedScreenH) return;
        cachedScreenW = screenW;
        cachedScreenH = screenH;
        cachedX = static_cast<int>(xFraction * screenW);
        cachedY = static_cast<int>(yFraction * screenH);
        cachedDisplayW = static_cast<int>(originalWidth * scale * g_winScaleRatio);
        cachedDisplayH = static_cast<int>(originalHeight * scale * g_winScaleRatio);
    }

    void syncFractionsFromPixels(int screenW, int screenH) {
        xFraction = static_cast<double>(cachedX) / screenW;
        yFraction = static_cast<double>(cachedY) / screenH;
        xFraction = std::clamp(xFraction, 0.0, 1.0);
        yFraction = std::clamp(yFraction, 0.0, 1.0);
    }

    void setPosition(int x, int y) {
        cachedX = x;
        cachedY = y;
    }

    void setScale(float s) {
        scale = std::clamp(s, 0.1f, 10.0f);
        cachedDisplayW = static_cast<int>(originalWidth * scale * g_winScaleRatio);
        cachedDisplayH = static_cast<int>(originalHeight * scale * g_winScaleRatio);
    }

    float getScale() const { return scale; }

    // Restore full saved state (mirrors mod's ImageOverlay.setSavedState)
    void setSavedState(double xf, double yf, float s, MovementMode mode,
                       const std::vector<Waypoint>& wps, int wpIndex,
                       double dmx, double dmy) {
        xFraction = std::clamp(xf, 0.0, 1.0);
        yFraction = std::clamp(yf, 0.0, 1.0);
        scale = std::clamp(s, 0.1f, 10.0f);
        movementMode = mode;
        waypoints = wps;
        currentWaypointIndex = wpIndex;
        randMoveDX = dmx;
        randMoveDY = dmy;
        if (randMoveDX == 0 && randMoveDY == 0) generateRandomDirection();
        cachedScreenW = 0;
        cachedScreenH = 0;
        if (mode == MovementMode::WAYPOINT) {
            waypointStaticUntil = GetTickCount64() + 2000;
        }
    }
    bool isDragging() const { return dragging; }
    bool isResizing() const { return resizing; }

    void startDrag(int mouseX, int mouseY) {
        dragging = true;
        dragOffsetX = mouseX - cachedX;
        dragOffsetY = mouseY - cachedY;
        resizing = false;
    }

    void stopDrag() {
        dragging = false;
        resizing = false;
    }

    void dragTo(int mouseX, int mouseY) {
        cachedX = mouseX - dragOffsetX;
        cachedY = mouseY - dragOffsetY;
    }

    void startResize() {
        resizing = true;
        dragging = false;
    }

    void resizeTo(int mouseX) {
        int newW = mouseX - cachedX;
        if (newW < 20) newW = 20;
        cachedDisplayW = newW;
        cachedDisplayH = static_cast<int>(newW / (static_cast<double>(originalWidth) / originalHeight));
        // Store the ratio-free scale so a later window resize doesn't compound
        scale = static_cast<float>(cachedDisplayW) / static_cast<float>(originalWidth * g_winScaleRatio);
    }

    bool containsPoint(int mouseX, int mouseY) const {
        return mouseX >= cachedX && mouseX <= cachedX + cachedDisplayW &&
               mouseY >= cachedY && mouseY <= cachedY + cachedDisplayH;
    }

    bool isOverCorner(int mouseX, int mouseY, int cornerSize) const {
        int half = cornerSize / 2;
        return std::abs(mouseX - (cachedX + cachedDisplayW)) <= half &&
               std::abs(mouseY - (cachedY + cachedDisplayH)) <= half;
    }

    void generateRandomDirection() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dist(0.0, 1.0);
        double angle = dist(gen) * 3.14159265358979323846 * 2.0;
        randMoveDX = std::cos(angle);
        randMoveDY = std::sin(angle);
    }

    void tickMovement(int screenW, int screenH) {
        if (movementMode == MovementMode::NONE) return;
        int imgW = cachedDisplayW;
        int imgH = cachedDisplayH;

        switch (movementMode) {
            case MovementMode::WAYPOINT: {
                if (waypoints.empty()) break;
                const Waypoint& wp = waypoints[currentWaypointIndex];
                cachedX = static_cast<int>(wp.xFraction * screenW);
                cachedY = static_cast<int>(wp.yFraction * screenH);
                syncFractionsFromPixels(screenW, screenH);

                bool advance;
                if (animated) {
                    advance = consumeAnimationLoopCount() > 0;
                } else {
                    advance = GetTickCount64() >= waypointStaticUntil;
                }
                if (advance) {
                    currentWaypointIndex = (currentWaypointIndex + 1) % static_cast<int>(waypoints.size());
                    waypointStaticUntil = GetTickCount64() + 2000;
                }
                break;
            }
            case MovementMode::RANDOM_MOVE: {
                long long nowMs = GetTickCount64();
                if (lastMoveTickMs == 0) lastMoveTickMs = nowMs;
                double elapsedMs = (double)(nowMs - lastMoveTickMs);
                lastMoveTickMs = nowMs;
                // Advance MOVE_SPEED px along the direction vector per 50ms
                // (matches the mod's per-client-tick rate). Accumulate the
                // fractional part: the old (int)(d*SPEED) truncation froze any
                // axis whose |cos/sin| < 0.5 for the whole lifetime of that
                // direction, making many overlays slide only up/down.
                double units = elapsedMs / 50.0;
                moveAccX += randMoveDX * MOVE_SPEED * units;
                moveAccY += randMoveDY * MOVE_SPEED * units;
                int stepX = static_cast<int>(moveAccX); moveAccX -= stepX;
                int stepY = static_cast<int>(moveAccY); moveAccY -= stepY;
                cachedX += stepX;
                cachedY += stepY;
                if (cachedX < 0) { cachedX = 0; randMoveDX = std::abs(randMoveDX); moveAccX = 0; }
                if (cachedX + imgW > screenW) { cachedX = screenW - imgW; randMoveDX = -std::abs(randMoveDX); moveAccX = 0; }
                if (cachedY < 0) { cachedY = 0; randMoveDY = std::abs(randMoveDY); moveAccY = 0; }
                if (cachedY + imgH > screenH) { cachedY = screenH - imgH; randMoveDY = -std::abs(randMoveDY); moveAccY = 0; }
                syncFractionsFromPixels(screenW, screenH);
                break;
            }
            case MovementMode::RANDOM_POSITION: {
                bool advance;
                if (animated) {
                    advance = consumeAnimationLoopCount() > 0;
                } else {
                    advance = GetTickCount64() >= waypointStaticUntil;
                }
                if (advance) {
                    static std::random_device rd;
                    static std::mt19937 gen(rd());
                    std::uniform_int_distribution<> distX(0, std::max(1, screenW - imgW));
                    std::uniform_int_distribution<> distY(0, std::max(1, screenH - imgH));
                    cachedX = distX(gen);
                    cachedY = distY(gen);
                    waypointStaticUntil = GetTickCount64() + 2000;
                    syncFractionsFromPixels(screenW, screenH);
                }
                break;
            }
            default:
                break;
        }
    }
};

// ---------- Modern GL entry points ----------
// opengl32.dll only exports GL 1.1 symbols; shader-era entry points must be
// resolved at runtime (wglGetProcAddress with GetProcAddress fallback).
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#define GL_ACTIVE_TEXTURE 0x84E0
#endif

// NOTE: typedef names use a WMF prefix because the platform GL headers may
// already define/expand the canonical PFNGL...PROC names (as macros), which
// breaks plain re-declarations.
typedef char WMFGLchar;
#ifndef GL_VERSION_1_5
typedef ptrdiff_t WMFGLsizeiptr;
#else
typedef GLsizeiptr WMFGLsizeiptr;
#endif

typedef GLuint (APIENTRYP WMFPFNGLCREATESHADERPROC)(GLenum);
typedef void (APIENTRYP WMFPFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const WMFGLchar* const*, const GLint*);
typedef void (APIENTRYP WMFPFNGLCOMPILESHADERPROC)(GLuint);
typedef GLuint (APIENTRYP WMFPFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRYP WMFPFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (APIENTRYP WMFPFNGLLINKPROGRAMPROC)(GLuint);
typedef void (APIENTRYP WMFPFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void (APIENTRYP WMFPFNGLDELETESHADERPROC)(GLuint);
typedef void (APIENTRYP WMFPFNGLDELETEPROGRAMPROC)(GLuint);
typedef GLint (APIENTRYP WMFPFNGLGETUNIFORMLOCATIONPROC)(GLuint, const WMFGLchar*);
typedef GLint (APIENTRYP WMFPFNGLGETATTRIBLOCATIONPROC)(GLuint, const WMFGLchar*);
typedef void (APIENTRYP WMFPFNGLUSEPROGRAMPROC)(GLuint);
typedef void (APIENTRYP WMFPFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void (APIENTRYP WMFPFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRYP WMFPFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void (APIENTRYP WMFPFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void (APIENTRYP WMFPFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void (APIENTRYP WMFPFNGLBUFFERDATAPROC)(GLenum, WMFGLsizeiptr, const void*, GLenum);
typedef void (APIENTRYP WMFPFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void (APIENTRYP WMFPFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (APIENTRYP WMFPFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (APIENTRYP WMFPFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (APIENTRYP WMFPFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void (APIENTRYP WMFPFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void (APIENTRYP WMFPFNGLACTIVETEXTUREPROC)(GLenum);

static WMFPFNGLCREATESHADERPROC p_glCreateShader = nullptr;
static WMFPFNGLSHADERSOURCEPROC p_glShaderSource = nullptr;
static WMFPFNGLCOMPILESHADERPROC p_glCompileShader = nullptr;
static WMFPFNGLCREATEPROGRAMPROC p_glCreateProgram = nullptr;
static WMFPFNGLATTACHSHADERPROC p_glAttachShader = nullptr;
static WMFPFNGLLINKPROGRAMPROC p_glLinkProgram = nullptr;
static WMFPFNGLGETPROGRAMIVPROC p_glGetProgramiv = nullptr;
static WMFPFNGLDELETESHADERPROC p_glDeleteShader = nullptr;
static WMFPFNGLDELETEPROGRAMPROC p_glDeleteProgram = nullptr;
static WMFPFNGLGETUNIFORMLOCATIONPROC p_glGetUniformLocation = nullptr;
static WMFPFNGLGETATTRIBLOCATIONPROC p_glGetAttribLocation = nullptr;
static WMFPFNGLUSEPROGRAMPROC p_glUseProgram = nullptr;
static WMFPFNGLUNIFORMMATRIX4FVPROC p_glUniformMatrix4fv = nullptr;
static WMFPFNGLUNIFORM4FPROC p_glUniform4f = nullptr;
static WMFPFNGLUNIFORM1IPROC p_glUniform1i = nullptr;
static WMFPFNGLGENBUFFERSPROC p_glGenBuffers = nullptr;
static WMFPFNGLBINDBUFFERPROC p_glBindBuffer = nullptr;
static WMFPFNGLBUFFERDATAPROC p_glBufferData = nullptr;
static WMFPFNGLDELETEBUFFERSPROC p_glDeleteBuffers = nullptr;
static WMFPFNGLENABLEVERTEXATTRIBARRAYPROC p_glEnableVertexAttribArray = nullptr;
static WMFPFNGLDISABLEVERTEXATTRIBARRAYPROC p_glDisableVertexAttribArray = nullptr;
static WMFPFNGLVERTEXATTRIBPOINTERPROC p_glVertexAttribPointer = nullptr;
static WMFPFNGLGENVERTEXARRAYSPROC p_glGenVertexArrays = nullptr;
static WMFPFNGLBINDVERTEXARRAYPROC p_glBindVertexArray = nullptr;
static WMFPFNGLACTIVETEXTUREPROC p_glActiveTexture = nullptr;

// Resolve all modern entry points. Requires a current GL context. Returns false if unavailable.
static bool LoadModernGL() {
    static bool tried = false, ok = false;
    if (tried) return ok;
    tried = true;

    HMODULE ogl = GetModuleHandleW(L"opengl32.dll");
    auto get = [&](const char* name) -> void* {
        void* p = (void*)wglGetProcAddress(name);
        if (!p || p == (void*)-1 || p == (void*)1 || p == (void*)2 || p == (void*)3) {
            p = ogl ? (void*)GetProcAddress(ogl, name) : nullptr;
        }
        return p;
    };

    p_glCreateShader = (WMFPFNGLCREATESHADERPROC)get("glCreateShader");
    p_glShaderSource = (WMFPFNGLSHADERSOURCEPROC)get("glShaderSource");
    p_glCompileShader = (WMFPFNGLCOMPILESHADERPROC)get("glCompileShader");
    p_glCreateProgram = (WMFPFNGLCREATEPROGRAMPROC)get("glCreateProgram");
    p_glAttachShader = (WMFPFNGLATTACHSHADERPROC)get("glAttachShader");
    p_glLinkProgram = (WMFPFNGLLINKPROGRAMPROC)get("glLinkProgram");
    p_glGetProgramiv = (WMFPFNGLGETPROGRAMIVPROC)get("glGetProgramiv");
    p_glDeleteShader = (WMFPFNGLDELETESHADERPROC)get("glDeleteShader");
    p_glDeleteProgram = (WMFPFNGLDELETEPROGRAMPROC)get("glDeleteProgram");
    p_glGetUniformLocation = (WMFPFNGLGETUNIFORMLOCATIONPROC)get("glGetUniformLocation");
    p_glGetAttribLocation = (WMFPFNGLGETATTRIBLOCATIONPROC)get("glGetAttribLocation");
    p_glUseProgram = (WMFPFNGLUSEPROGRAMPROC)get("glUseProgram");
    p_glUniformMatrix4fv = (WMFPFNGLUNIFORMMATRIX4FVPROC)get("glUniformMatrix4fv");
    p_glUniform4f = (WMFPFNGLUNIFORM4FPROC)get("glUniform4f");
    p_glUniform1i = (WMFPFNGLUNIFORM1IPROC)get("glUniform1i");
    p_glGenBuffers = (WMFPFNGLGENBUFFERSPROC)get("glGenBuffers");
    p_glBindBuffer = (WMFPFNGLBINDBUFFERPROC)get("glBindBuffer");
    p_glBufferData = (WMFPFNGLBUFFERDATAPROC)get("glBufferData");
    p_glDeleteBuffers = (WMFPFNGLDELETEBUFFERSPROC)get("glDeleteBuffers");
    p_glEnableVertexAttribArray = (WMFPFNGLENABLEVERTEXATTRIBARRAYPROC)get("glEnableVertexAttribArray");
    p_glDisableVertexAttribArray = (WMFPFNGLDISABLEVERTEXATTRIBARRAYPROC)get("glDisableVertexAttribArray");
    p_glVertexAttribPointer = (WMFPFNGLVERTEXATTRIBPOINTERPROC)get("glVertexAttribPointer");
    p_glGenVertexArrays = (WMFPFNGLGENVERTEXARRAYSPROC)get("glGenVertexArrays");
    p_glBindVertexArray = (WMFPFNGLBINDVERTEXARRAYPROC)get("glBindVertexArray");
    p_glActiveTexture = (WMFPFNGLACTIVETEXTUREPROC)get("glActiveTexture");

    ok = p_glCreateShader && p_glShaderSource && p_glCompileShader &&
         p_glCreateProgram && p_glAttachShader && p_glLinkProgram && p_glGetProgramiv &&
         p_glGetUniformLocation && p_glGetAttribLocation && p_glUseProgram &&
         p_glUniformMatrix4fv && p_glUniform4f && p_glUniform1i &&
         p_glGenBuffers && p_glBindBuffer && p_glBufferData &&
         p_glEnableVertexAttribArray && p_glDisableVertexAttribArray && p_glVertexAttribPointer &&
         p_glActiveTexture;
    OutputDebugStringA(ok ? "[WatermarkDLL] Modern GL entry points loaded\n"
                          : "[WatermarkDLL] Modern GL entry points UNAVAILABLE\n");
    return ok;
}

// Forward declaration for ImageLoader (defined below)
namespace ImageLoader {
    std::shared_ptr<ImageOverlay> loadImageNoCopy(const std::wstring& filePath);
}

// OverlayManager class
class OverlayManager {
public:
    std::vector<std::shared_ptr<ImageOverlay>> overlays;
    std::shared_ptr<ImageOverlay> selectedOverlay;
    bool interacting = false;
    bool paused = false;

    std::wstring configDir;
    std::wstring imagesDir;
    std::wstring configFile;

    bool pickingWaypoints = false;
    std::shared_ptr<ImageOverlay> waypointTarget;
    std::vector<Waypoint> pendingWaypoints;

    // Save throttling (mirrors mod's SAVE_INTERVAL_MS / dirty flush)
    long long lastSaveTime = 0;
    bool dirty = false;
    static constexpr long long SAVE_INTERVAL_MS = 1000;

    std::mutex mutex;

    OverlayManager() {
        wchar_t* appDataPath = nullptr;
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath);
        if (appDataPath) {
            configDir = std::wstring(appDataPath) + L"\\WatermarkDLL";
            imagesDir = configDir + L"\\images";
            configFile = configDir + L"\\overlays.json";
            CoTaskMemFree(appDataPath);
        }
        createConfigDirs();
    }

    void createConfigDirs() {
        try {
            std::filesystem::create_directories(configDir);
            std::filesystem::create_directories(imagesDir);
        } catch (...) {}
    }

    void addOverlay(std::shared_ptr<ImageOverlay> overlay) {
        std::lock_guard<std::mutex> lock(mutex);
        overlays.push_back(overlay);
        saveOverlays();
    }

    void removeOverlay(std::shared_ptr<ImageOverlay> overlay) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = std::find(overlays.begin(), overlays.end(), overlay);
        if (it != overlays.end()) {
            overlay->releaseTextures();
            overlays.erase(it);
            if (selectedOverlay == overlay) selectedOverlay = nullptr;
            saveOverlays();
        }
    }

    void clearAll() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& o : overlays) o->releaseTextures();
        overlays.clear();
        selectedOverlay = nullptr;
    }

    std::shared_ptr<ImageOverlay> getOverlayAt(int mx, int my) {
        for (int i = static_cast<int>(overlays.size()) - 1; i >= 0; i--) {
            auto& ov = overlays[i];
            if (ov->containsPoint(mx, my)) return ov;
        }
        return nullptr;
    }

    std::shared_ptr<ImageOverlay> getOverlayAtCorner(int mx, int my, int cs) {
        for (int i = static_cast<int>(overlays.size()) - 1; i >= 0; i--) {
            auto& ov = overlays[i];
            if (ov->isOverCorner(mx, my, cs)) return ov;
        }
        return nullptr;
    }

    void syncFractions(int screenW, int screenH) {
        for (auto& o : overlays) o->syncFractionsFromPixels(screenW, screenH);
        saveOverlays();
    }

    void tickMovement(int screenW, int screenH) {
        for (auto& o : overlays) o->tickMovement(screenW, screenH);
        flushPendingSave();
    }

    // Throttled save: defer rapid successive saves, flush via tickMovement
    void saveOverlays() {
        long long now = GetTickCount64();
        if (now - lastSaveTime < SAVE_INTERVAL_MS) { dirty = true; return; }
        actuallySave();
        lastSaveTime = now;
        dirty = false;
    }

    void flushPendingSave() {
        if (dirty) {
            actuallySave();
            lastSaveTime = GetTickCount64();
            dirty = false;
        }
    }

    void actuallySave() {
        try {
            std::ofstream file(configFile);
            if (!file.is_open()) return;
            auto fmtDouble = [](double v) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.10g", v);
                return std::string(buf);
            };
            file << "[\n";
            for (size_t i = 0; i < overlays.size(); i++) {
                auto& ov = overlays[i];
                file << "  {\n";
                file << "    \"fileName\": \"" << ov->backupFileName << "\",\n";
                file << "    \"xFraction\": " << fmtDouble(ov->xFraction) << ",\n";
                file << "    \"yFraction\": " << fmtDouble(ov->yFraction) << ",\n";
                file << "    \"scale\": " << fmtDouble(ov->scale) << ",\n";
                file << "    \"movementMode\": \"" << movementModeToString(ov->movementMode) << "\",\n";
                file << "    \"waypoints\": [";
                for (size_t j = 0; j < ov->waypoints.size(); j++) {
                    if (j > 0) file << ", ";
                    file << "{\"xf\": " << fmtDouble(ov->waypoints[j].xFraction)
                         << ", \"yf\": " << fmtDouble(ov->waypoints[j].yFraction) << "}";
                }
                file << "],\n";
                file << "    \"currentWaypointIndex\": " << ov->currentWaypointIndex << ",\n";
                file << "    \"randDX\": " << fmtDouble(ov->randMoveDX) << ",\n";
                file << "    \"randDY\": " << fmtDouble(ov->randMoveDY) << "\n";
                file << "  }";
                if (i < overlays.size() - 1) file << ",";
                file << "\n";
            }
            file << "]\n";
        } catch (...) {}
    }

    // Load overlays from overlays.json (mirrors mod's OverlayManager.loadSavedOverlays).
    // Must be called with a current GL context (render thread).
    void loadSavedOverlays() {
        try {
            std::ifstream file(configFile);
            if (!file.is_open()) {
                OutputDebugStringA("[WatermarkDLL] No saved overlays config found\n");
                return;
            }
            std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            Json::Parser parser(json.c_str());
            Json::Value root = parser.parse();
            if (root.type != Json::Value::Arr) {
                OutputDebugStringA("[WatermarkDLL] overlays.json is not an array\n");
                return;
            }
            int loaded = 0;
            for (auto& item : root.arr) {
                if (item.type != Json::Value::Obj) continue;
                std::string fileName = item.getStr("fileName", "");
                if (fileName.empty()) continue;
                std::wstring wName(fileName.begin(), fileName.end());
                std::filesystem::path imgPath = std::filesystem::path(imagesDir) / wName;
                if (!std::filesystem::exists(imgPath)) {
                    OutputDebugStringA(("[WatermarkDLL] Saved image not found: " + fileName + "\n").c_str());
                    continue;
                }
                auto overlay = ImageLoader::loadImageNoCopy(imgPath.wstring());
                if (!overlay) {
                    OutputDebugStringA(("[WatermarkDLL] Failed to load saved image: " + fileName + "\n").c_str());
                    continue;
                }
                std::vector<Waypoint> wps;
                const Json::Value* wpArr = item.find("waypoints");
                if (wpArr && wpArr->type == Json::Value::Arr) {
                    for (auto& w : wpArr->arr) {
                        if (w.type == Json::Value::Obj) {
                            wps.emplace_back(w.getNum("xf", 0.0), w.getNum("yf", 0.0));
                        }
                    }
                }
                overlay->setSavedState(
                    item.getNum("xFraction", 0.1), item.getNum("yFraction", 0.1),
                    (float)item.getNum("scale", 1.0),
                    movementModeFromString(item.getStr("movementMode", "NONE")),
                    wps, (int)item.getNum("currentWaypointIndex", 0),
                    item.getNum("randDX", 0.0), item.getNum("randDY", 0.0));
                overlays.push_back(overlay);
                loaded++;
            }
            OutputDebugStringA(("[WatermarkDLL] Loaded " + std::to_string(loaded) + " saved overlay(s)\n").c_str());
        } catch (...) {
            OutputDebugStringA("[WatermarkDLL] loadSavedOverlays exception\n");
        }
    }

    std::string copyImageToBackup(const std::wstring& sourcePath) {
        try {
            std::filesystem::path source(sourcePath);
            std::string name = source.filename().string();
            size_t dot = name.find_last_of('.');
            std::string ext = (dot != std::string::npos) ? name.substr(dot) : "";

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dist(0, 15);
            const char* hex = "0123456789abcdef";
            std::string uuid;
            for (int i = 0; i < 8; i++) uuid += hex[dist(gen)];

            std::string backupName = uuid + ext;
            std::filesystem::path destPath = std::filesystem::path(imagesDir) / std::wstring(backupName.begin(), backupName.end());
            std::filesystem::copy_file(source, destPath, std::filesystem::copy_options::overwrite_existing);
            return backupName;
        } catch (...) { return ""; }
    }

    void startWaypointPicking(std::shared_ptr<ImageOverlay> target) {
        pickingWaypoints = true;
        waypointTarget = target;
        pendingWaypoints.clear();
    }

    void addWaypoint(double xf, double yf) {
        if (pendingWaypoints.size() < 7) pendingWaypoints.emplace_back(xf, yf);
    }

    void removeLastWaypoint() {
        if (!pendingWaypoints.empty()) pendingWaypoints.pop_back();
    }

    void confirmWaypoints(int screenW, int screenH) {
        if (!waypointTarget || pendingWaypoints.empty()) { cancelWaypoints(); return; }
        waypointTarget->waypoints = pendingWaypoints;
        waypointTarget->setMovementMode(MovementMode::WAYPOINT);
        waypointTarget->currentWaypointIndex = 0;
        waypointTarget->waypointStaticUntil = GetTickCount64() + 2000;
        auto& first = pendingWaypoints[0];
        waypointTarget->xFraction = first.xFraction;
        waypointTarget->yFraction = first.yFraction;
        waypointTarget->updateFromScreenSize(screenW, screenH);
        pendingWaypoints.clear();
        pickingWaypoints = false;
        waypointTarget = nullptr;
        saveOverlays();
    }

    void cancelWaypoints() {
        pendingWaypoints.clear();
        pickingWaypoints = false;
        waypointTarget = nullptr;
    }
};

// Image loader using stb_image
namespace ImageLoader {
    GLuint createTextureFromRGBA(const unsigned char* data, int width, int height) {
        GLuint texture;
        glGenTextures(1, &texture);
        GLint prevTex = 0;
        p_glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Use GL_BGRA because WIC loads images in BGRA format
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
        return texture;
    }

    std::shared_ptr<ImageOverlay> loadImage(const std::wstring& filePath, OverlayManager& mgr) {
        // Check if we have a valid GL context
        HGLRC currentCtx = wglGetCurrentContext();
        if (!currentCtx) {
            OutputDebugStringA("[WatermarkDLL] No GL context when loading image\n");
            return nullptr;
        }

        // Convert wstring to UTF-8 for stb_image/WIC (handles non-ASCII paths)
        std::string path = WideToUtf8(filePath);
        
        // Get file extension
        std::string ext = "";
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) {
            ext = path.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        // Handle GIF files
        if (ext == ".gif") {
            OutputDebugStringA("[WatermarkDLL] Loading GIF file\n");
            
            int width, height, frames;
            int* delays = nullptr;
            stbi_gif* gif = stbi_load_gif(path.c_str(), &delays, &width, &height, &frames, nullptr, 4);
            
            if (!gif || gif->count <= 0) {
                OutputDebugStringA("[WatermarkDLL] Failed to load GIF\n");
                if (gif) stbi_gif_free(gif);
                return nullptr;
            }

            std::vector<GLuint> textures;
            std::vector<int> frameDelays;

            for (int i = 0; i < gif->count; i++) {
                if (!gif->frames[i].data) continue;
                GLuint tex = createTextureFromRGBA(gif->frames[i].data, width, height);
                if (tex != 0) {
                    textures.push_back(tex);
                    frameDelays.push_back(gif->frames[i].delay);
                }
            }

            stbi_gif_free(gif);

            if (textures.empty()) {
                OutputDebugStringA("[WatermarkDLL] No textures created from GIF\n");
                return nullptr;
            }

            std::string backupName = mgr.copyImageToBackup(filePath);
            if (backupName.empty()) {
                for (auto tex : textures) glDeleteTextures(1, &tex);
                return nullptr;
            }

            OutputDebugStringA(("[WatermarkDLL] Loaded GIF with " + std::to_string(textures.size()) + " frames\n").c_str());
            return std::make_shared<ImageOverlay>(backupName, path, textures, frameDelays, width, height);
        }

        // Handle static images (PNG, JPG, etc.)
        OutputDebugStringA("[WatermarkDLL] Loading static image\n");
        
        int width, height, channels;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!data) {
            OutputDebugStringA("[WatermarkDLL] Failed to load image\n");
            return nullptr;
        }

        GLuint texture = createTextureFromRGBA(data, width, height);
        stbi_image_free(data);

        if (texture == 0) {
            OutputDebugStringA("[WatermarkDLL] Failed to create texture\n");
            return nullptr;
        }

        std::vector<GLuint> textures = { texture };
        std::vector<int> delays = { 0 };
        std::string backupName = mgr.copyImageToBackup(filePath);
        if (backupName.empty()) {
            glDeleteTextures(1, &texture);
            return nullptr;
        }

        OutputDebugStringA(("[WatermarkDLL] Loaded static image: " + std::to_string(width) + "x" + std::to_string(height) + "\n").c_str());
        return std::make_shared<ImageOverlay>(backupName, path, textures, delays, width, height);
    }

    // Load an image that is already in the backup dir (no copy). Used by loadSavedOverlays.
    std::shared_ptr<ImageOverlay> loadImageNoCopy(const std::wstring& filePath) {
        HGLRC currentCtx = wglGetCurrentContext();
        if (!currentCtx) {
            OutputDebugStringA("[WatermarkDLL] No GL context when loading saved image\n");
            return nullptr;
        }

        std::string path = WideToUtf8(filePath);
        std::string name = WideToUtf8(std::filesystem::path(filePath).filename().wstring());

        std::string ext = "";
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) {
            ext = path.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        if (ext == ".gif") {
            int width, height, frames;
            int* delays = nullptr;
            stbi_gif* gif = stbi_load_gif(path.c_str(), &delays, &width, &height, &frames, nullptr, 4);
            if (!gif || gif->count <= 0) {
                if (gif) stbi_gif_free(gif);
                return nullptr;
            }
            std::vector<GLuint> textures;
            std::vector<int> frameDelays;
            for (int i = 0; i < gif->count; i++) {
                if (gif->frames[i].data) {
                    GLuint tex = createTextureFromRGBA(gif->frames[i].data, width, height);
                    if (tex != 0) {
                        textures.push_back(tex);
                        frameDelays.push_back(gif->frames[i].delay);
                    }
                }
            }
            stbi_gif_free(gif);
            if (textures.empty()) return nullptr;
            return std::make_shared<ImageOverlay>(name, path, textures, frameDelays, width, height);
        }

        int width, height, channels;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!data) return nullptr;
        GLuint texture = createTextureFromRGBA(data, width, height);
        stbi_image_free(data);
        if (texture == 0) return nullptr;
        std::vector<GLuint> textures = { texture };
        std::vector<int> delays = { 0 };
        return std::make_shared<ImageOverlay>(name, path, textures, delays, width, height);
    }
}

// Global OverlayManager
OverlayManager g_overlayManager;

// ---------- Version-independent game detection ----------
// Reference approach (InfiniteGUI-DLL): no obfuscated class names needed,
// works on every Minecraft version (1.8.9 -> latest) via pure Win32 state.
namespace Compat {
    // True when the OS cursor is visible. In-game Minecraft hides the cursor;
    // any GUI screen (chat, inventory, pause...) shows a standard system cursor.
    bool IsMouseCursorVisible() {
        CURSORINFO ci; ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci)) return false;
        if (!(ci.flags & CURSOR_SHOWING)) return false;

        static const LPCWSTR sysIds[] = {
            IDC_ARROW, IDC_IBEAM, IDC_WAIT, IDC_CROSS, IDC_UPARROW,
            IDC_SIZEALL, IDC_SIZENWSE, IDC_SIZENESW, IDC_SIZENS, IDC_SIZEWE,
            IDC_HAND, IDC_NO, IDC_APPSTARTING
        };
        static const size_t kCount = sizeof(sysIds) / sizeof(sysIds[0]);
        static HCURSOR sysCursors[kCount] = {};
        static bool loaded = false;
        if (!loaded) {
            for (size_t i = 0; i < kCount; i++) sysCursors[i] = LoadCursorW(nullptr, sysIds[i]);
            loaded = true;
        }
        for (size_t i = 0; i < kCount; i++) {
            if (sysCursors[i] && sysCursors[i] == ci.hCursor) return true;
        }
        return false;
    }

    // Only trust cursor state while the game window is focused
    bool IsGameWindowFocused() {
        return g_hwnd && GetForegroundWindow() == g_hwnd;
    }

    // Read guiScale from .minecraft/options.txt (-1 = not found)
    int ReadGuiScaleSetting() {
        static int cached = -2; // -2 = not read yet
        static DWORD lastRead = 0;
        DWORD now = GetTickCount();
        if (cached != -2 && now - lastRead < 5000) return cached;
        lastRead = now;

        wchar_t appdata[MAX_PATH];
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) return cached = -1;
        std::ifstream f(std::wstring(appdata) + L"\\.minecraft\\options.txt");
        if (!f.is_open()) return cached = -1;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("guiScale:", 0) == 0) {
                try { return cached = std::stoi(line.substr(9)); }
                catch (...) { return cached = -1; }
            }
        }
        return cached = -1;
    }

    // Official Minecraft scale-factor algorithm (identical across all versions):
    // scale starts at 1 and grows while the window still fits 320x240 per step.
    int ComputeScaleFactor(int winW, int winH, int guiScale) {
        int scale = 1;
        int limit = (guiScale <= 0) ? 1000 : guiScale;
        while (scale < limit && winW / (scale + 1) >= 320 && winH / (scale + 1) >= 240) scale++;
        return scale;
    }

    int GetScaleFactor(int winW, int winH) {
        if (winW <= 0 || winH <= 0) return 1;
        return ComputeScaleFactor(winW, winH, ReadGuiScaleSetting());
    }
} // namespace Compat

// ---------- Modern OpenGL renderer ----------
// Minecraft 1.17+ uses OpenGL 3.2 Core Profile where all fixed-function APIs
// (glBegin/glOrtho/glPushAttrib/wglUseFontBitmaps) are ILLEGAL and break the
// game's GL state. This renderer uses a shader + VBO pipeline that works on
// both Core (3.2+) and Compatibility (2.1, e.g. MC 1.8.9) contexts.
namespace OverlayGL {
    static GLuint g_prog = 0;
    static GLuint g_vbo = 0;
    static GLuint g_vao = 0;
    static GLuint g_whiteTex = 0;
    static GLint g_uMVP = -1, g_uColor = -1, g_uTex = -1;
    static GLuint g_aPos = 0, g_aUV = 0;
    static bool g_inited = false;
    static bool g_useVAO = false;   // VAOs require GL 3.0+
    static bool g_use150 = false;   // GLSL 150 core vs 120 fallback

    static GLuint compileShader(GLenum type, const char* src) {
        GLuint s = p_glCreateShader(type);
        p_glShaderSource(s, 1, &src, nullptr);
        p_glCompileShader(s);
        return s;
    }

    static bool buildProgram() {
        static const char* vs150 =
            "#version 150 core\n"
            "in vec2 aPos; in vec2 aUV; uniform mat4 uMVP; out vec2 vUV;\n"
            "void main(){ vUV = aUV; gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }";
        static const char* fs150 =
            "#version 150 core\n"
            "in vec2 vUV; uniform sampler2D uTex; uniform vec4 uColor; out vec4 oColor;\n"
            "void main(){ oColor = texture(uTex, vUV) * uColor; }";
        static const char* vs120 =
            "#version 120\n"
            "attribute vec2 aPos; attribute vec2 aUV; uniform mat4 uMVP; varying vec2 vUV;\n"
            "void main(){ vUV = aUV; gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }";
        static const char* fs120 =
            "#version 120\n"
            "varying vec2 vUV; uniform sampler2D uTex; uniform vec4 uColor;\n"
            "void main(){ gl_FragColor = texture2D(uTex, vUV) * uColor; }";

        const char* vs = g_use150 ? vs150 : vs120;
        const char* fs = g_use150 ? fs150 : fs120;
        GLuint v = compileShader(GL_VERTEX_SHADER, vs);
        GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
        GLuint p = p_glCreateProgram();
        p_glAttachShader(p, v);
        p_glAttachShader(p, f);
        p_glLinkProgram(p);
        GLint ok = 0;
        p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
        p_glDeleteShader(v);
        p_glDeleteShader(f);
        if (!ok) {
            p_glDeleteProgram(p);
            return false;
        }
        g_prog = p;
        g_uMVP = p_glGetUniformLocation(p, "uMVP");
        g_uColor = p_glGetUniformLocation(p, "uColor");
        g_uTex = p_glGetUniformLocation(p, "uTex");
        g_aPos = (GLuint)p_glGetAttribLocation(p, "aPos");
        g_aUV = (GLuint)p_glGetAttribLocation(p, "aUV");
        return true;
    }

    // Must be called with the game GL context current (render thread).
    static bool EnsureInit() {
        if (g_inited) return g_prog != 0;
        if (!LoadModernGL()) { g_inited = true; return false; }

        // Detect context version: GL_MAJOR_VERSION (3.0+); fall back to string parse
        GLint major = 2, minor = 1;
        const char* verStr = (const char*)glGetString(GL_VERSION);
        GLint mj = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &mj);
        if (mj > 0) {
            glGetIntegerv(GL_MAJOR_VERSION, &major);
            glGetIntegerv(GL_MINOR_VERSION, &minor);
        } else if (verStr) {
            sscanf_s(verStr, "%d.%d", &major, &minor);
        }
        g_use150 = (major > 3) || (major == 3 && minor >= 2);
        g_useVAO = (major >= 3);

        if (!buildProgram()) {
            // Retry with legacy GLSL if the modern shader failed
            if (g_use150) {
                g_use150 = false;
                if (!buildProgram()) { g_inited = true; return false; }
            } else {
                g_inited = true;
                return false;
            }
        }

        // 1x1 white texture for solid-color quads
        unsigned char white[4] = {255, 255, 255, 255};
        glGenTextures(1, &g_whiteTex);
        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glBindTexture(GL_TEXTURE_2D, g_whiteTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, white);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);

        p_glGenBuffers(1, &g_vbo);
        if (g_useVAO) p_glGenVertexArrays(1, &g_vao);

        g_inited = true;
        OutputDebugStringA(g_use150
            ? "[WatermarkDLL] OverlayGL: GLSL 150 core pipeline ready\n"
            : "[WatermarkDLL] OverlayGL: GLSL 120 pipeline ready\n");
        return true;
    }

    static void ortho2D(float l, float r, float b, float t, float* m) {
        // Column-major orthographic projection, top-left origin
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = 2.0f / (r - l);
        m[5] = 2.0f / (t - b);
        m[10] = -1.0f;
        m[12] = -(r + l) / (r - l);
        m[13] = -(t + b) / (t - b);
        m[15] = 1.0f;
    }

    // Draw a textured (or white) quad in raw window pixel coordinates
    static void DrawQuad(GLuint tex, float x, float y, float w, float h,
                         float r, float g, float b, float a) {
        if (!g_prog) return;
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        float m[16];
        ortho2D(0.0f, (float)viewport[2], (float)viewport[3], 0.0f, m);

        float verts[4][4] = {
            {x,     y,     0.0f, 0.0f},
            {x,     y + h, 0.0f, 1.0f},
            {x + w, y + h, 1.0f, 1.0f},
            {x + w, y,     1.0f, 0.0f},
        };

        p_glUseProgram(g_prog);
        p_glUniformMatrix4fv(g_uMVP, 1, GL_FALSE, m);
        p_glUniform4f(g_uColor, r, g, b, a);
        p_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex ? tex : g_whiteTex);
        p_glUniform1i(g_uTex, 0);

        GLint prevVbo = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVbo);
        if (g_useVAO) p_glBindVertexArray(g_vao);
        p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        p_glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
        p_glEnableVertexAttribArray(g_aPos);
        p_glVertexAttribPointer(g_aPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        p_glEnableVertexAttribArray(g_aUV);
        p_glVertexAttribPointer(g_aUV, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        p_glDisableVertexAttribArray(g_aPos);
        p_glDisableVertexAttribArray(g_aUV);
        p_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevVbo);
        if (g_useVAO) p_glBindVertexArray(0);
    }
} // namespace OverlayGL

// Solid-color rect helper (window pixel coordinates)
static void fillRect(int x, int y, int w, int h, float r, float g, float b, float a) {
    OverlayGL::DrawQuad(0, (float)x, (float)y, (float)w, (float)h, r, g, b, a);
}

// ---------- Text rendering via GDI-rasterized label textures ----------
// (wglUseFontBitmaps is illegal in Core Profile; we rasterize whole labels
//  into DIBs once and draw them as textured quads instead.)
struct LabelTex { GLuint tex = 0; int w = 0, h = 0; };
static std::map<std::wstring, LabelTex> g_labelCache;

static const LabelTex& GetLabel(const wchar_t* text) {
    auto it = g_labelCache.find(text);
    if (it != g_labelCache.end()) return it->second;

    LabelTex lt;
    g_labelCache[text] = lt; // placeholder in case of failure
    LabelTex& cached = g_labelCache[text];

    HDC memDC = CreateCompatibleDC(nullptr);
    if (!memDC) return cached;
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (!font) { DeleteDC(memDC); return cached; }

    HGDIOBJ oldFont = SelectObject(memDC, font);
    SIZE sz = {0, 0};
    int len = (int)wcslen(text);
    if (!GetTextExtentPoint32W(memDC, text, len, &sz) || sz.cx <= 0 || sz.cy <= 0) {
        SelectObject(memDC, oldFont); DeleteObject(font); DeleteDC(memDC);
        return cached;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sz.cx;
    bmi.bmiHeader.biHeight = -sz.cy;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) {
        SelectObject(memDC, oldFont); DeleteObject(font); DeleteDC(memDC);
        return cached;
    }
    HGDIOBJ oldBmp = SelectObject(memDC, bmp);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    ExtTextOutW(memDC, 0, 0, 0, nullptr, text, len, nullptr);

    // BGRX DIB -> RGBA texture with luminance alpha (white text)
    int pxCount = sz.cx * sz.cy;
    std::vector<unsigned char> rgba(pxCount * 4);
    unsigned char* src = (unsigned char*)bits;
    for (int i = 0; i < pxCount; i++) {
        unsigned char bV = src[i * 4 + 0], gV = src[i * 4 + 1], rV = src[i * 4 + 2];
        unsigned char aV = (unsigned char)(((int)rV + (int)gV + (int)bV) / 3);
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = aV;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex) {
        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz.cx, sz.cy, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    }

    SelectObject(memDC, oldBmp);
    SelectObject(memDC, oldFont);
    DeleteObject(bmp);
    DeleteObject(font);
    DeleteDC(memDC);

    cached.tex = tex;
    cached.w = sz.cx;
    cached.h = sz.cy;
    return cached;
}

// Draw text label (window pixel coordinates, white/colored via tint)
static void DrawLabel(const wchar_t* text, int x, int y, float r, float g, float b) {
    const LabelTex& lt = GetLabel(text);
    if (!lt.tex) return;
    OverlayGL::DrawQuad(lt.tex, (float)x, (float)y, (float)lt.w, (float)lt.h, r, g, b, 1.0f);
}

// Renderer
void renderOverlay(ImageOverlay& overlay) {
    int fi = overlay.getCurrentFrameIndex();
    GLuint texture = overlay.getFrameTexture(fi);
    if (texture == 0) return;

    float x = (float)(overlay.cachedX * g_guiScale);
    float y = (float)(overlay.cachedY * g_guiScale);
    float w = (float)(overlay.cachedDisplayW * g_guiScale);
    float h = (float)(overlay.cachedDisplayH * g_guiScale);

    OverlayGL::DrawQuad(texture, x, y, w, h, 1.0f, 1.0f, 1.0f, 1.0f);
}

void renderSelectionBorder(const ImageOverlay& overlay) {
    float x1 = (float)(overlay.cachedX * g_guiScale - 1);
    float y1 = (float)(overlay.cachedY * g_guiScale - 1);
    float x2 = (float)((overlay.cachedX + overlay.cachedDisplayW) * g_guiScale + 1);
    float y2 = (float)((overlay.cachedY + overlay.cachedDisplayH) * g_guiScale + 1);
    float t = 2.0f;

    fillRect((int)x1, (int)y1, (int)(x2 - x1), (int)t, 1.0f, 1.0f, 0.0f, 1.0f);          // top
    fillRect((int)x1, (int)(y2 - t), (int)(x2 - x1), (int)t, 1.0f, 1.0f, 0.0f, 1.0f);    // bottom
    fillRect((int)x1, (int)y1, (int)t, (int)(y2 - y1), 1.0f, 1.0f, 0.0f, 1.0f);          // left
    fillRect((int)(x2 - t), (int)y1, (int)t, (int)(y2 - y1), 1.0f, 1.0f, 0.0f, 1.0f);    // right

    float hs = 8.0f;
    float cx = (float)((overlay.cachedX + overlay.cachedDisplayW) * g_guiScale);
    float cy = (float)((overlay.cachedY + overlay.cachedDisplayH) * g_guiScale);
    fillRect((int)(cx - hs / 2), (int)(cy - hs / 2), (int)hs, (int)hs, 0.0f, 1.0f, 0.0f, 1.0f);
}

void renderOverlays() {
    if (!OverlayGL::EnsureInit() || !OverlayGL::g_prog) return;

    // Save/restore game GL state explicitly (glPushAttrib is illegal in core profile)
    GLboolean savedDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean savedBlend = glIsEnabled(GL_BLEND);
    GLboolean savedCull = glIsEnabled(GL_CULL_FACE);
    GLboolean savedDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &savedDepthMask);
    GLint savedProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &savedProg);
    GLint savedTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTex);
    GLint savedActiveTex = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActiveTex);
    GLint savedVao = 0;
    if (OverlayGL::g_useVAO) glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &savedVao);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Waypoint picking mode: dim screen, show markers + hint (mirrors mod's renderWaypointPicker)
    if (g_overlayManager.pickingWaypoints) {
        fillRect(0, 0, g_screenW, g_screenH, 0.0f, 0.0f, 0.0f, 0.53f);
        DrawLabel(L"左键添加标点(最多7个) | 右键撤销 | Enter确认 | Esc取消", 6, 6, 1.0f, 1.0f, 0.0f);

        auto& wps = g_overlayManager.pendingWaypoints;
        for (size_t i = 0; i < wps.size(); i++) {
            int px = static_cast<int>(wps[i].xFraction * g_screenW);
            int py = static_cast<int>(wps[i].yFraction * g_screenH);
            int s = 12;
            fillRect(px - s, py - s, s * 2, 1, 1.0f, 0.2f, 0.2f, 1.0f);  // top bar
            fillRect(px - s, py + s, s * 2, 1, 1.0f, 0.2f, 0.2f, 1.0f);  // bottom bar
            fillRect(px - s, py - s, 1, s * 2, 1.0f, 0.2f, 0.2f, 1.0f);  // left bar
            fillRect(px + s, py - s, 1, s * 2, 1.0f, 0.2f, 0.2f, 1.0f);  // right bar
            wchar_t num[8];
            swprintf_s(num, L"%d", (int)i + 1);
            DrawLabel(num, px - 7, py - 8, 1.0f, 0.2f, 0.2f);
        }

        goto restore;
    }

    // Render all overlays (cachedX/Y live in Minecraft scaled coordinates)
    for (auto& overlay : g_overlayManager.overlays) {
        overlay->updateFromScreenSize(g_screenW / g_guiScale, g_screenH / g_guiScale);
        renderOverlay(*overlay);

        if (g_overlayManager.interacting && overlay == g_overlayManager.selectedOverlay) {
            renderSelectionBorder(*overlay);
        }
    }

    // Render context menu if showing (5 items, Chinese, hover highlight)
    if (g_showMenu && g_menuTarget) {
        int menuW = MENU_WIDTH;
        int itemH = MENU_ITEM_H;
        int menuH = itemH * MENU_ITEM_COUNT;

        int hoverIndex = -1;
        if (g_mouseX >= g_menuX && g_mouseX <= g_menuX + menuW &&
            g_mouseY >= g_menuY && g_mouseY <= g_menuY + menuH) {
            hoverIndex = (g_mouseY - g_menuY) / itemH;
        }

        // Background
        fillRect(g_menuX, g_menuY, menuW, menuH, 0.1f, 0.1f, 0.1f, 0.8f);

        // Hover highlight
        if (hoverIndex >= 0 && hoverIndex < MENU_ITEM_COUNT) {
            fillRect(g_menuX + 1, g_menuY + hoverIndex * itemH + 1, menuW - 2, itemH - 1,
                     0.27f, 0.53f, 0.8f, 0.27f);
        }

        // Border (4 thin rects) + separators
        fillRect(g_menuX, g_menuY, menuW, 1, 0.67f, 0.67f, 0.67f, 1.0f);
        fillRect(g_menuX, g_menuY + menuH - 1, menuW, 1, 0.67f, 0.67f, 0.67f, 1.0f);
        fillRect(g_menuX, g_menuY, 1, menuH, 0.67f, 0.67f, 0.67f, 1.0f);
        fillRect(g_menuX + menuW - 1, g_menuY, 1, menuH, 0.67f, 0.67f, 0.67f, 1.0f);
        for (int i = 1; i < MENU_ITEM_COUNT; i++) {
            fillRect(g_menuX + 2, g_menuY + i * itemH, menuW - 4, 1, 0.5f, 0.5f, 0.5f, 1.0f);
        }

        // Menu item text
        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            DrawLabel(MENU_ITEMS[i], g_menuX + 6, g_menuY + i * itemH + 3, 1.0f, 1.0f, 1.0f);
        }
    }

restore:
    // Restore game GL state
    p_glUseProgram((GLuint)savedProg);
    p_glActiveTexture((GLenum)savedActiveTex);
    glBindTexture(GL_TEXTURE_2D, (GLuint)savedTex);
    if (OverlayGL::g_useVAO) p_glBindVertexArray((GLuint)savedVao);
    if (savedDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(savedDepthMask);
    if (savedBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (savedCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

// WndProc hook
LRESULT CALLBACK WndProcHook(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    bool isRepeat = (lParam & (1 << 30)) != 0;

    switch (message) {
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            OutputDebugStringA(("[WatermarkDLL] WM_DROPFILES received, " + std::to_string(fileCount) + " file(s)\n").c_str());
            for (UINT i = 0; i < fileCount; i++) {
                wchar_t filePath[MAX_PATH];
                DragQueryFileW(hDrop, i, filePath, MAX_PATH);
                OutputDebugStringW((L"[WatermarkDLL] Dropped file: " + std::wstring(filePath) + L"\n").c_str());
                std::wstring path(filePath);
                std::wstring ext;
                size_t dot = path.find_last_of(L'.');
                if (dot != std::wstring::npos) {
                    ext = path.substr(dot);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                }
                if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" || ext == L".bmp") {
                    // Load image in the context of the main thread
                    auto overlay = ImageLoader::loadImage(path, g_overlayManager);
                    if (overlay) {
                        g_overlayManager.addOverlay(overlay);
                        // Select the new overlay immediately so it shows the
                        // selection border and is ready to drag without any
                        // chat toggle.
                        overlay->updateFromScreenSize(g_screenW / g_guiScale, g_screenH / g_guiScale);
                        g_overlayManager.selectedOverlay = overlay;
                        OutputDebugStringA(("[WatermarkDLL] Image added, interacting=" +
                            std::string(g_overlayManager.interacting ? "1" : "0") + "\n").c_str());
                    }
                }
            }
            DragFinish(hDrop);
            break;
        }
        case WM_KEYDOWN: {
            if (!isRepeat) {
                // Waypoint picking: Enter confirms, Esc cancels (mirrors mod)
                if (g_overlayManager.pickingWaypoints) {
                    if (wParam == VK_RETURN) {
                        g_overlayManager.confirmWaypoints(g_screenW, g_screenH);
                        OutputDebugStringA("[WatermarkDLL] Waypoints confirmed\n");
                    } else if (wParam == VK_ESCAPE) {
                        g_overlayManager.cancelWaypoints();
                        OutputDebugStringA("[WatermarkDLL] Waypoint picking cancelled\n");
                    }
                    return 0;
                }

                // Insert key to toggle edit mode manually
                if (wParam == VK_INSERT) {
                    g_manualEditMode = !g_manualEditMode;
                    g_overlayManager.interacting = g_manualEditMode;
                    g_overlayManager.paused = g_manualEditMode;
                    
                    if (g_manualEditMode) {
                        OutputDebugStringA("[WatermarkDLL] Manual edit mode ON\n");
                        // Release mouse cursor - call ShowCursor multiple times to ensure it's visible
                        ClipCursor(NULL);
                        for (int i = 0; i < 10; i++) {
                            if (ShowCursor(TRUE) >= 0) break;
                        }
                        // Also release the mouse capture
                        ReleaseCapture();
                        // Send a message to release cursor
                        SendMessage(g_hwnd, WM_SETCURSOR, 0, 0);
                    } else {
                        OutputDebugStringA("[WatermarkDLL] Manual edit mode OFF\n");
                        g_overlayManager.selectedOverlay = nullptr;
                        g_showMenu = false;
                        // Hide cursor
                        for (int i = 0; i < 10; i++) {
                            if (ShowCursor(FALSE) < 0) break;
                        }
                    }
                }
            }
            break;
        }
        case WM_SIZE: {
            g_screenW = LOWORD(lParam);
            g_screenH = HIWORD(lParam);
            break;
        }
        case WM_LBUTTONDOWN: {
            if (g_overlayManager.interacting) {
                int mx = LOWORD(lParam);
                int my = HIWORD(lParam);

                // Waypoint picking: left-click adds a waypoint (mirrors mod)
                if (g_overlayManager.pickingWaypoints) {
                    if (g_screenW > 0 && g_screenH > 0) {
                        g_overlayManager.addWaypoint(static_cast<double>(mx) / g_screenW,
                                                     static_cast<double>(my) / g_screenH);
                    }
                    return 0;
                }

                char debugMsg[256];
                sprintf_s(debugMsg, "[WatermarkDLL] LBUTTONDOWN at (%d, %d), showMenu=%d\n", mx, my, g_showMenu);
                OutputDebugStringA(debugMsg);

                // Check if clicking on menu
                if (g_showMenu) {
                    int menuW = MENU_WIDTH;
                    int itemH = MENU_ITEM_H;
                    int menuH = itemH * MENU_ITEM_COUNT;
                    
                    sprintf_s(debugMsg, "[WatermarkDLL] Menu bounds: (%d,%d)-(%d,%d)\n", g_menuX, g_menuY, g_menuX + menuW, g_menuY + menuH);
                    OutputDebugStringA(debugMsg);
                    
                    if (mx >= g_menuX && mx <= g_menuX + menuW && my >= g_menuY && my <= g_menuY + menuH) {
                        // Calculate which menu item was clicked
                        int itemIndex = (my - g_menuY) / itemH;
                        
                        sprintf_s(debugMsg, "[WatermarkDLL] Clicked menu item %d\n", itemIndex);
                        OutputDebugStringA(debugMsg);
                        
                        if (g_menuTarget) {
                            switch (itemIndex) {
                                case 0: // 定点移动 - start waypoint picking
                                    g_overlayManager.startWaypointPicking(g_menuTarget);
                                    OutputDebugStringA("[WatermarkDLL] Waypoint picking started\n");
                                    break;
                                case 1: // 随机运动
                                    g_menuTarget->setMovementMode(MovementMode::RANDOM_MOVE);
                                    g_menuTarget->generateRandomDirection();
                                    g_overlayManager.saveOverlays();
                                    OutputDebugStringA("[WatermarkDLL] Set random movement\n");
                                    break;
                                case 2: // 随机展示
                                    g_menuTarget->setMovementMode(MovementMode::RANDOM_POSITION);
                                    g_overlayManager.saveOverlays();
                                    OutputDebugStringA("[WatermarkDLL] Set random position\n");
                                    break;
                                case 3: // 重置运动
                                    g_menuTarget->setMovementMode(MovementMode::NONE);
                                    g_overlayManager.saveOverlays();
                                    OutputDebugStringA("[WatermarkDLL] Reset movement\n");
                                    break;
                                case 4: // 删除
                                    g_overlayManager.removeOverlay(g_menuTarget);
                                    OutputDebugStringA("[WatermarkDLL] Overlay removed\n");
                                    break;
                            }
                        }
                        
                        g_showMenu = false;
                        return 0;
                    } else {
                        // Clicked outside menu, close it
                        g_showMenu = false;
                    }
                }
                
                // Check if clicking on corner (resize) - scaled coords like the mod
                int sx = mx / g_guiScale;
                int sy = my / g_guiScale;
                auto ov = g_overlayManager.getOverlayAtCorner(sx, sy, 16);
                if (ov) {
                    ov->startResize();
                    g_overlayManager.selectedOverlay = ov;
                    OutputDebugStringA("[WatermarkDLL] Started resizing overlay\n");
                    return 0;
                }
                // Check if clicking on overlay (drag)
                ov = g_overlayManager.getOverlayAt(sx, sy);
                if (ov) {
                    ov->startDrag(sx, sy);
                    g_overlayManager.selectedOverlay = ov;
                    OutputDebugStringA("[WatermarkDLL] Started dragging overlay\n");
                    return 0;
                }
                // Missed everything: clear selection and pass the click to the game
                // (fixes main-menu / inventory clicks dying when edit mode is on)
                g_overlayManager.selectedOverlay = nullptr;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_overlayManager.interacting) {
                bool consumed = false;
                // Stop all dragging/resizing
                for (auto& ov : g_overlayManager.overlays) {
                    if (ov->isDragging() || ov->isResizing()) {
                        ov->stopDrag();
                        consumed = true;
                        OutputDebugStringA("[WatermarkDLL] Stopped dragging/resizing\n");
                    }
                }
                if (consumed) {
                    g_overlayManager.syncFractions(g_screenW, g_screenH);
                    return 0;
                }
                // Nothing was being dragged: pass release to the game
            }
            break;
        }
        case WM_RBUTTONDOWN: {
            if (g_overlayManager.interacting) {
                int mx = LOWORD(lParam);
                int my = HIWORD(lParam);

                // Waypoint picking: right-click removes last waypoint (mirrors mod)
                if (g_overlayManager.pickingWaypoints) {
                    g_overlayManager.removeLastWaypoint();
                    return 0;
                }

                char debugMsg[256];
                sprintf_s(debugMsg, "[WatermarkDLL] RBUTTONDOWN at (%d, %d)\n", mx, my);
                OutputDebugStringA(debugMsg);
                
                // Check if right-clicking on overlay (scaled coords; menu itself is raw px)
                auto ov = g_overlayManager.getOverlayAt(mx / g_guiScale, my / g_guiScale);
                if (ov) {
                    g_overlayManager.selectedOverlay = ov;
                    g_showMenu = true;
                    g_menuX = mx;
                    g_menuY = my;
                    g_menuTarget = ov;
                    sprintf_s(debugMsg, "[WatermarkDLL] Showing context menu at (%d, %d), target=%p\n", mx, my, ov.get());
                    OutputDebugStringA(debugMsg);
                    return 0;
                }
                // Missed: close menu and pass the click to the game
                g_showMenu = false;
                OutputDebugStringA("[WatermarkDLL] No overlay at click position\n");
            }
            break;
        }
        case WM_MOUSEMOVE: {
            // Always track mouse position (menu hover highlight)
            g_mouseX = LOWORD(lParam);
            g_mouseY = HIWORD(lParam);

            if (g_overlayManager.interacting) {
                int mx = LOWORD(lParam);
                int my = HIWORD(lParam);
                int sx = mx / g_guiScale;
                int sy = my / g_guiScale;

                // Handle dragging; only consume the message while actually dragging
                bool active = false;
                for (auto& ov : g_overlayManager.overlays) {
                    if (ov->isDragging()) {
                        ov->dragTo(sx, sy);
                        active = true;
                    } else if (ov->isResizing()) {
                        ov->resizeTo(sx);
                        active = true;
                    }
                }
                if (active) return 0;
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            if (g_overlayManager.interacting) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                POINT pt;
                pt.x = LOWORD(lParam);
                pt.y = HIWORD(lParam);
                ScreenToClient(hWnd, &pt);
                
                // Find overlay under cursor (scaled coords like rendering)
                auto ov = g_overlayManager.getOverlayAt(pt.x / g_guiScale, pt.y / g_guiScale);
                if (ov) {
                    float scale = ov->getScale() + (delta > 0 ? 0.1f : -0.1f);
                    if (scale < 0.1f) scale = 0.1f;
                    if (scale > 10.0f) scale = 10.0f;
                    ov->setScale(scale);
                    g_overlayManager.syncFractions(g_screenW, g_screenH);
                    OutputDebugStringA("[WatermarkDLL] Scaled overlay\n");
                    return 0;
                }
                // No overlay under cursor: let the game process the scroll
            }
            break;
        }
    }

    if (g_origWndProc) return CallWindowProcW(g_origWndProc, hWnd, message, wParam, lParam);
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// wglSwapBuffers hook
wglSwapBuffers_t original_wglSwapBuffers = nullptr;
BOOL WINAPI wglSwapBuffersDetour(HDC hdc) {
    if (g_isDetaching) return original_wglSwapBuffers(hdc);
    g_isRendering = true;

    // Save current context
    HGLRC origCtx = wglGetCurrentContext();

    // First-time initialization
    static bool initialized = false;
    if (!initialized) {
        g_hwnd = WindowFromDC(hdc);
        g_hdc = hdc;
        
        // Don't create a new context - use the existing one
        // This fixes the white square issue
        g_glCtx = origCtx;
        
        RECT area;
        GetClientRect(g_hwnd, &area);
        g_screenW = area.right - area.left;
        g_screenH = area.bottom - area.top;
        g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
        DragAcceptFiles(g_hwnd, TRUE);
        // If the game runs elevated (admin), UIPI silently discards WM_DROPFILES
        // from Explorer (lower integrity). Allow it explicitly so drag-drop works.
#ifndef WM_COPYGLOBALDATA
#define WM_COPYGLOBALDATA 0x0049
#endif
        ChangeWindowMessageFilterEx(g_hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(g_hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(g_hwnd, WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr);

        // Initialize Java detector for chat screen detection
        JavaDetector::initialize();

        // Initialize modern GL pipeline (core-profile safe shader + VBO)
        OverlayGL::EnsureInit();

        // Restore previously saved overlays (GL context is current here)
        g_overlayManager.loadSavedOverlays();

        initialized = true;
        OutputDebugStringA("[WatermarkDLL] Initialized (using existing GL context)\n");
    }

    // Check if this is our window
    if (WindowFromDC(hdc) != g_hwnd) {
        g_isRendering = false;
        return original_wglSwapBuffers(hdc);
    }

    // Update screen size
    RECT area;
    GetClientRect(g_hwnd, &area);
    g_screenW = area.right - area.left;
    g_screenH = area.bottom - area.top;

    // Minecraft GUI scale factor (all-version algorithm from options.txt)
    g_guiScale = Compat::GetScaleFactor(g_screenW, g_screenH);

    // Proportional scaling on window resize: ratio = current width / reference
    // width captured once at startup. Restoring the window restores ratio 1.
    {
        static int refScaledW = 0;
        int scaledW = std::max(1, g_screenW / g_guiScale);
        if (refScaledW == 0) refScaledW = scaledW;
        g_winScaleRatio = scaledW / static_cast<double>(refScaledW);
    }

    // Screen detection: JNI chat detection (version-specific) with automatic
    // fallback to cursor-visibility detection (works on ALL versions, ref: InfiniteGUI-DLL)
    if (!g_manualEditMode) {
        bool screenOpen;
        if (JavaDetector::isDetectionActive()) {
            screenOpen = JavaDetector::isChatScreenOpen();
            if (g_detectMode != 1) {
                g_detectMode = 1;
                OutputDebugStringA("[WatermarkDLL] Screen detection: JNI mode\n");
            }
        } else {
            // Compat mode: enter edit mode only when the cursor became visible
            // after having been hidden (i.e. player was in-game and opened a GUI).
            // The main menu shows the cursor from the start - never auto-edit there.
            static bool cursorWasHidden = false;
            static int unfocusedFrames = 0;
            bool focused = Compat::IsGameWindowFocused();
            // While dragging a file from Explorer the game window is unfocused
            // for the WHOLE drag (often several seconds) - far longer than any
            // debounce window, so the latch must never be reset on unfocus.
            // Focus only gates the current screenOpen state: alt-tab keeps edit
            // mode off while away and restores it on return (latch survives).
            bool focusStable = focused;
            if (!focused) {
                ++unfocusedFrames;
                if (unfocusedFrames <= 60) focusStable = true;  // transient flicker
            } else {
                unfocusedFrames = 0;
            }
            screenOpen = focusStable && cursorWasHidden && Compat::IsMouseCursorVisible();
            if (g_detectMode != 2) {
                g_detectMode = 2;
                OutputDebugStringA("[WatermarkDLL] Screen detection: cursor-compat mode (all versions)\n");
            }
        }

        bool chatOpen = screenOpen;
        if (chatOpen != g_chatScreenOpen) {
            g_chatScreenOpen = chatOpen;
            g_overlayManager.interacting = chatOpen;
            g_overlayManager.paused = chatOpen;

            if (chatOpen) {
                // Release cursor lock once on entering edit mode (only if hidden,
                // to avoid unbalancing the cursor display counter)
                ClipCursor(NULL);
                ReleaseCapture();
                CURSORINFO ci; ci.cbSize = sizeof(ci);
                if (GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING)) {
                    ShowCursor(TRUE);
                }
                OutputDebugStringA("[WatermarkDLL] Screen opened - entering edit mode\n");
            } else {
                OutputDebugStringA("[WatermarkDLL] Screen closed - exiting edit mode\n");
                // Deselect overlay when closing screen
                g_overlayManager.selectedOverlay = nullptr;
                g_showMenu = false;
            }
        }
    }

    // Keep cursor unlocked while in edit mode (game re-locks every frame otherwise)
    if (g_overlayManager.interacting) {
        ClipCursor(NULL);
    }

    // Update overlay movement (in Minecraft scaled coordinates, like the mod)
    if (!g_overlayManager.paused) {
        g_overlayManager.tickMovement(g_screenW / g_guiScale, g_screenH / g_guiScale);
    }

    // Render overlays
    renderOverlays();

    g_isRendering = false;

    return original_wglSwapBuffers(hdc);
}

// Main thread
DWORD WINAPI MainThread(LPVOID lpParam) {
    Sleep(3000); // Wait for Minecraft to initialize

    // Initialize COM for WIC (Windows Imaging Component)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        OutputDebugStringA("[WatermarkDLL] Failed to initialize COM\n");
        // Continue anyway, might work without it
    } else {
        OutputDebugStringA("[WatermarkDLL] COM initialized\n");
    }

    HMODULE hOGL32 = GetModuleHandleW(L"opengl32.dll");
    if (!hOGL32) {
        OutputDebugStringA("[WatermarkDLL] opengl32.dll not found\n");
        return 0;
    }

    LPVOID pFunc = (LPVOID)GetProcAddress(hOGL32, "wglSwapBuffers");
    if (!pFunc) {
        OutputDebugStringA("[WatermarkDLL] wglSwapBuffers not found\n");
        return 0;
    }

    original_wglSwapBuffers = (wglSwapBuffers_t)(pFunc);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)original_wglSwapBuffers, wglSwapBuffersDetour);
    DetourTransactionCommit();

    OutputDebugStringA("[WatermarkDLL] Hook installed\n");

    // Main loop
    while (!g_isDetaching) {
        Sleep(100);
        
        // Periodically check Java detector (in case it wasn't initialized at startup)
        static bool javaInitialized = false;
        if (!javaInitialized) {
            javaInitialized = JavaDetector::initialize();
            if (javaInitialized) {
                OutputDebugStringA("[WatermarkDLL] Java detector initialized\n");
            }
        }
    }

    // Cleanup
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&)original_wglSwapBuffers, wglSwapBuffersDetour);
    DetourTransactionCommit();

    if (g_origWndProc && g_hwnd) {
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
    }

    g_overlayManager.clearAll();
    JavaDetector::shutdown();
    
    // Uninitialize COM
    CoUninitialize();
    OutputDebugStringA("[WatermarkDLL] COM uninitialized\n");

    return 0;
}

// Per-process injection guard. The launcher checks this mutex before injecting;
// the DLL itself also refuses a second load, so ANY injector is blocked from
// double-injecting the same Minecraft process.
static HANDLE g_injectGuardMutex = nullptr;
static void BuildInjectGuardName(wchar_t* buf, size_t cap, DWORD pid) {
    swprintf_s(buf, cap, L"Local\\WatermarkInjection_Active_%lu", pid);
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    g_hModule = hModule;
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);
            wchar_t name[96];
            BuildInjectGuardName(name, 96, GetCurrentProcessId());
            g_injectGuardMutex = CreateMutexW(nullptr, TRUE, name);
            if (!g_injectGuardMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
                // Already injected into this process - refuse the load.
                OutputDebugStringA("[WatermarkDLL] Already injected, refusing duplicate load\n");
                return FALSE;
            }
            CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
            break;
        }
        case DLL_PROCESS_DETACH:
            g_isDetaching = true;
            if (g_injectGuardMutex) { ReleaseMutex(g_injectGuardMutex); CloseHandle(g_injectGuardMutex); }
            break;
    }
    return TRUE;
}
