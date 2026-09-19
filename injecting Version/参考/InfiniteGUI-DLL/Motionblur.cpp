#include "Motionblur.h"
#include <GL/glew.h>
#include <GL/GL.h>

#include "FpsItem.h"
#include "opengl_hook.h"
#include "GameStateDetector.h"
#include "imgui\imgui_internal.h"
#include "ImGuiStd.h"
#include "gui.h"

static const char* vertex_shader_330 = R"glsl(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)glsl";

static const char* fragment_shader_330 = R"glsl(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D currentTexture;
uniform sampler2D historyTexture;
uniform float blurriness;
uniform float velocity_factor;
uniform float renderRGB;
uniform float smooth_blur;

vec4 blurHistory(vec2 uv)
{
    float offset = 0.0006 * velocity_factor;
    vec4 sum = vec4(0.0);
    sum += texture(historyTexture, uv + vec2(-offset, 0.0)) * 0.25;
    sum += texture(historyTexture, uv + vec2( offset, 0.0)) * 0.25;
    sum += texture(historyTexture, uv + vec2(0.0, -offset)) * 0.25;
    sum += texture(historyTexture, uv + vec2(0.0,  offset)) * 0.25;
    return sum;
}

void main()
{
    vec4 current = texture(currentTexture, TexCoord);
    vec4 history = texture(historyTexture, TexCoord);
    float cur_blurriness = blurriness;
    vec4 blurredHistory = history;
    if(velocity_factor > 0.0){
        float base_blurriness = blurriness * 0.5;
        cur_blurriness = base_blurriness + velocity_factor * base_blurriness;
        if(smooth_blur > 0.5) blurredHistory = blurHistory(TexCoord);
    }
    vec4 blurredColor = mix(current, blurredHistory, cur_blurriness);
    if (renderRGB > 0.5) FragColor = blurredColor;
    else {
        float value1 = current.r;
        FragColor = mix(vec4(value1), blurredHistory, cur_blurriness);
    }
}
)glsl";

static const char* vertex_shader_120 = R"glsl(
#version 120
attribute vec2 aPos;
attribute vec2 aTexCoord;
varying vec2 TexCoord;
void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)glsl";

static const char* fragment_shader_120 = R"glsl(
#version 120
varying vec2 TexCoord;
uniform sampler2D currentTexture;
uniform sampler2D historyTexture;
uniform float blurriness;
uniform float velocity_factor;
uniform float renderRGB;
uniform float smooth_blur;

vec4 blurHistory(vec2 uv)
{
    float offset = 0.0006 * velocity_factor;
    vec4 sum = vec4(0.0);
    sum += texture2D(historyTexture, uv + vec2(-offset, 0.0)) * 0.25;
    sum += texture2D(historyTexture, uv + vec2( offset, 0.0)) * 0.25;
    sum += texture2D(historyTexture, uv + vec2(0.0, -offset)) * 0.25;
    sum += texture2D(historyTexture, uv + vec2(0.0,  offset)) * 0.25;
    return sum;
}

void main()
{
    vec4 current = texture2D(currentTexture, TexCoord);
    vec4 history = texture2D(historyTexture, TexCoord);
    float cur_blurriness = blurriness;
    vec4 blurredHistory = history;
    if(velocity_factor > 0.0){
        float base_blurriness = blurriness * 0.5;
        cur_blurriness = base_blurriness + velocity_factor * base_blurriness;
        if(smooth_blur > 0.5) blurredHistory = blurHistory(TexCoord);
    }
    vec4 blurredColor = mix(current, blurredHistory, cur_blurriness);
    if (renderRGB > 0.5) gl_FragColor = blurredColor;
    else {
        float value1 = current.r;
        gl_FragColor = mix(vec4(value1), blurredHistory, cur_blurriness);
    }
}
)glsl";

static const char*& getVertexShader() { return g_gl30Available ? vertex_shader_330 : vertex_shader_120; }
static const char*& getFragmentShader() { return g_gl30Available ? fragment_shader_330 : fragment_shader_120; }

void Motionblur::Toggle() { if(!isEnabled) Destroy(); }
void Motionblur::RenderGui() {}
void Motionblur::RenderBeforeGui() { Render(); }
void Motionblur::RenderAfterGui() { Render(); }

void Motionblur::Render()
{
    if (!isEnabled) return;
    if (!applyOnGameMenu && GameStateDetector::Instance().GetCurrentState() == InGameMenu) return;
    static bool first = true;

    const int width = opengl_hook::screen_size.x;
    const int height = opengl_hook::screen_size.y;
    if (width <= 0 || height <= 0) return;

    GLint viewport[4];
    GLint prev_framebuffer = 0;
    if (g_gl30Available)
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glViewport(0, 0, width, height);

    if (!initialized_) {
        initialize_texture(width, height);
        initialize_quad();
        initialize_shader();
        initialized_ = true;
        texture_width_ = width;
        texture_height_ = height;
    }
    if (texture_width_ != width || texture_height_ != height) {
        resize_texture(width, height);
        first = true;
    }

    if (g_gl30Available) glBindFramebuffer(GL_FRAMEBUFFER, 0);
    copy_to_current();
    if (first) { copy_to_history(); first = false; }

    if (velocityAdaptive)
        velocity_adaptive_blur(GameStateDetector::Instance().IsCameraMoving(), GameStateDetector::Instance().GetCameraSpeed(), &velocity_factor);
    else velocity_factor = 1.0f;

    if (FpsModulate) Fps_modulate(FpsItem::Instance().GetInstantaneousFPS(), &blurriness_value, &cur_blurriness_value);
    else cur_blurriness_value = blurriness_value;

    draw_texture();
    copy_to_history();

    if (g_gl30Available) glBindFramebuffer(GL_FRAMEBUFFER, prev_framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void Motionblur::Destroy()
{
    if(!initialized_) return;
    glUseProgram(0);
    if (g_gl30Available) { glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindVertexArray(0); }
    if (shader_program_) glDeleteProgram(shader_program_);
    if (current_texture_) glDeleteTextures(1, &current_texture_);
    if (history_texture_) glDeleteTextures(1, &history_texture_);
    if (g_gl30Available && quad_vao_) glDeleteVertexArrays(1, &quad_vao_);
    if (quad_vbo_) glDeleteBuffers(1, &quad_vbo_);
    shader_program_ = 0; current_texture_ = 0; history_texture_ = 0;
    quad_vao_ = 0; quad_vbo_ = 0;
    initialized_ = false;
}

void Motionblur::initialize_texture(const int width, const int height)
{
    GLint internalFmt = g_gl30Available ? GL_RGBA8 : GL_RGBA;
    glGenTextures(1, &current_texture_);
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &history_texture_);
    glBindTexture(GL_TEXTURE_2D, history_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Motionblur::initialize_quad()
{
    constexpr GLfloat quad_vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    if (g_gl30Available) {
        glGenVertexArrays(1, &quad_vao_);
        glGenBuffers(1, &quad_vbo_);
        glBindVertexArray(quad_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    } else {
        glGenBuffers(1, &quad_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[1024]; glGetShaderInfoLog(shader, 1024, nullptr, log);
        OutputDebugStringA(log);
    }
    return shader;
}

void Motionblur::initialize_shader()
{
    if (!shader_program_) shader_program_ = glCreateProgram();
    GLuint vs = compileShader(GL_VERTEX_SHADER, getVertexShader());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, getFragmentShader());
    if (!vs || !fs) { glDeleteProgram(shader_program_); shader_program_ = 0; return; }
    glAttachShader(shader_program_, vs);
    glAttachShader(shader_program_, fs);
    glLinkProgram(shader_program_);
    GLint linked = 0;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &linked);
    if (!linked) { char log[1024]; glGetProgramInfoLog(shader_program_, 1024, nullptr, log); OutputDebugStringA(log); }
    glDeleteShader(vs);
    glDeleteShader(fs);
}

void Motionblur::resize_texture(int width, int height)
{
    GLint internalFmt = g_gl30Available ? GL_RGBA8 : GL_RGBA;
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, history_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    texture_width_ = width; texture_height_ = height;
}

void Motionblur::draw_texture() const
{
    if (current_texture_ == 0 || !shader_program_) return;
    GLboolean depth = GL_FALSE, blend = GL_FALSE;
    glGetBooleanv(GL_DEPTH_TEST, &depth);
    glGetBooleanv(GL_BLEND, &blend);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(shader_program_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glUniform1i(glGetUniformLocation(shader_program_, "currentTexture"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, history_texture_);
    glUniform1i(glGetUniformLocation(shader_program_, "historyTexture"), 1);

    float value = cur_blurriness_value / 11.0f;
    glUniform1f(glGetUniformLocation(shader_program_, "blurriness"), value);
    glUniform1f(glGetUniformLocation(shader_program_, "velocity_factor"), velocity_factor);
    glUniform1f(glGetUniformLocation(shader_program_, "renderRGB"), clear_color ? 0.0f : 1.0f);
    glUniform1f(glGetUniformLocation(shader_program_, "smooth_blur"), smooth_blur ? 1.0f : 0.0f);

    if (g_gl30Available) {
        glBindVertexArray(quad_vao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    glUseProgram(0);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND);
}

void Motionblur::copy_to_history() const
{
    if (g_gl30Available) glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    else glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, history_texture_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, texture_width_, texture_height_);
}

void Motionblur::copy_to_current() const
{
    if (g_gl30Available) glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    else glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, texture_width_, texture_height_);
}

void Motionblur::velocity_adaptive_blur(bool cameraMoving, float cameraSpeed, float* velocity_factor)
{
    float tar = std::clamp((cameraSpeed - 1.0f), 0.0f, 15.0f) / 15.0f;
    if (cameraMoving) *velocity_factor = tar;
}

void Motionblur::Fps_modulate(const float& fps, const float* blurriness_value, float* cur_blurriness_value)
{
    float nf = std::clamp(fps, 0.0f, 1000.0f) / 1000.0f;
    *cur_blurriness_value = *blurriness_value * std::pow(nf, 0.2f);
}

void Motionblur::Load(const nlohmann::json& j)
{
    LoadItem(j);
    if (j.contains("blurriness")) blurriness_value = j["blurriness"].get<float>();
    blurriness_value = std::clamp(blurriness_value, 0.0f, 10.0f);
    if (j.contains("velocityAdaptive")) velocityAdaptive = j["velocityAdaptive"].get<bool>();
    if (j.contains("smooth_blur")) smooth_blur = j["smooth_blur"].get<bool>();
    if (j.contains("applayOnMenu")) applyOnMenu = j["applayOnMenu"].get<bool>();
    if (j.contains("applyOnGameMenu")) applyOnGameMenu = j["applyOnGameMenu"].get<bool>();
    processApplyOnMenu();
    if (j.contains("clear_color")) clear_color = j["clear_color"].get<bool>();
    if (j.contains("FpsModulate")) FpsModulate = j["FpsModulate"].get<bool>();
}

void Motionblur::Save(nlohmann::json& j) const
{
    SaveItem(j);
    j["blurriness"] = blurriness_value;
    j["velocityAdaptive"] = velocityAdaptive;
    j["smooth_blur"] = smooth_blur;
    j["applyOnMenu"] = applyOnMenu;
    j["applyOnGameMenu"] = applyOnGameMenu;
    j["clear_color"] = clear_color;
    j["FpsModulate"] = FpsModulate;
}

void Motionblur::DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth)
{
    float bigItemWidth = centerX * 2.0f - bigPadding * 4.0f;
    ImGui::SetCursorPosX(bigPadding);
    ImGui::SetNextItemWidth(bigItemWidth);
    ImGui::SliderFloat(u8"模糊强度", &blurriness_value, 0.0f, 10.0f, "%.1f");
    ImGui::SetCursorPosX(bigPadding);
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::Checkbox(u8"菜单中开启", &applyOnMenu)) processApplyOnMenu();
    ImGui::SameLine();
    ImGui::SetCursorPosX(centerX + bigPadding);
    ImGui::SetNextItemWidth(itemWidth);
    ImGui::Checkbox(u8"游戏菜单中开启", &applyOnGameMenu);
    ImGui::SameLine(); ImGuiStd::HelpMarker(u8"打开背包和暂停等非游戏时GUI不会显示");
    ImGui::SetCursorPosX(bigPadding);
    ImGui::SetNextItemWidth(itemWidth);
    ImGui::Checkbox(u8"帧率调制", &FpsModulate);
    ImGui::SameLine(); ImGuiStd::HelpMarker(u8"模糊强度随帧率变化，在帧率不稳定时效果明显");
    ImGui::SameLine();
    ImGui::SetCursorPosX(centerX + bigPadding);
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::Checkbox(u8"速率自适应", &velocityAdaptive)) {
        if (!velocityAdaptive) smooth_blur = false;
    }
    ImGui::SameLine(); ImGuiStd::HelpMarker(u8"根据视角移动速度调节模糊强度，高速效果更明显。\n在MC1.12及以下版本此功能将失效。");
    ImGui::SetCursorPosX(bigPadding);
    ImGui::Checkbox(u8"灰度模式", &clear_color);
    if (velocityAdaptive) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(bigPadding + centerX);
        ImGui::SetNextItemWidth(itemWidth);
        ImGui::Checkbox(u8"平滑模糊", &smooth_blur);
        ImGui::SameLine(); ImGuiStd::HelpMarker(u8"柔化模糊影子边缘，使MCUI模糊化");
    }
}
