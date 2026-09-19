#include "Blur.h"
#include <GL/glew.h>
#include <GL/GL.h>
#include "gui.h"
#include "opengl_hook.h"

static const char* vertex_blur_330 = R"glsl(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)glsl";

static const char* fragment_blur_330 = R"glsl(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D image;
uniform vec2 resolution;
uniform vec2 direction;
void main() {
  vec4 color = vec4(0.0);
  vec2 off1 = vec2(1.411764705882353) * direction;
  vec2 off2 = vec2(3.2941176470588234) * direction;
  vec2 off3 = vec2(5.176470588235294) * direction;
  color += texture(image, TexCoord) * 0.1964825501511404;
  color += texture(image, TexCoord + (off1 / resolution)) * 0.2969069646728344;
  color += texture(image, TexCoord - (off1 / resolution)) * 0.2969069646728344;
  color += texture(image, TexCoord + (off2 / resolution)) * 0.09447039785044732;
  color += texture(image, TexCoord - (off2 / resolution)) * 0.09447039785044732;
  color += texture(image, TexCoord + (off3 / resolution)) * 0.010381362401148057;
  color += texture(image, TexCoord - (off3 / resolution)) * 0.010381362401148057;
  FragColor = color;
}
)glsl";

static const char* vertex_blur_120 = R"glsl(
#version 120
attribute vec2 aPos;
attribute vec2 aTexCoord;
varying vec2 TexCoord;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)glsl";

static const char* fragment_blur_120 = R"glsl(
#version 120
varying vec2 TexCoord;
uniform sampler2D image;
uniform vec2 resolution;
uniform vec2 direction;
void main() {
  vec4 color = vec4(0.0);
  vec2 off1 = vec2(1.411764705882353) * direction;
  vec2 off2 = vec2(3.2941176470588234) * direction;
  vec2 off3 = vec2(5.176470588235294) * direction;
  color += texture2D(image, TexCoord) * 0.1964825501511404;
  color += texture2D(image, TexCoord + (off1 / resolution)) * 0.2969069646728344;
  color += texture2D(image, TexCoord - (off1 / resolution)) * 0.2969069646728344;
  color += texture2D(image, TexCoord + (off2 / resolution)) * 0.09447039785044732;
  color += texture2D(image, TexCoord - (off2 / resolution)) * 0.09447039785044732;
  color += texture2D(image, TexCoord + (off3 / resolution)) * 0.010381362401148057;
  color += texture2D(image, TexCoord - (off3 / resolution)) * 0.010381362401148057;
  gl_FragColor = color;
}
)glsl";

static const char*& getVertBlur() { return g_gl30Available ? vertex_blur_330 : vertex_blur_120; }
static const char*& getFragBlur() { return g_gl30Available ? fragment_blur_330 : fragment_blur_120; }

void Blur::RenderBlur(float cur_blurriness)
{
    if (!g_gl30Available) return;

    const int width = opengl_hook::screen_size.x;
    const int height = opengl_hook::screen_size.y;
    if (width <= 0 || height <= 0) return;

    glViewport(0, 0, width, height);

    if (!initialized_) {
        initialize_pingpong(width, height);
        initialize_texture(width, height);
        initialize_quad();
        initialize_shader();
        initialized_ = true;
        texture_width_ = width;
        texture_height_ = height;
    }
    if (texture_width_ != width || texture_height_ != height)
        resize_texture(width, height);

    copy_to_current();
    apply_blur((int)cur_blurriness);
    if (cur_blurriness >= 1.0f) draw_final_texture();
}

void Blur::apply_blur(int iterations) const
{
    if (iterations <= 0 || !shader_program_) return;
    glUseProgram(shader_program_);
    glUniform2f(glGetUniformLocation(shader_program_, "resolution"), (float)texture_width_, (float)texture_height_);

    bool horizontal = true, firstPass = true;
    for (int i = 0; i < iterations * 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[horizontal]);
        glUniform2f(glGetUniformLocation(shader_program_, "direction"),
            horizontal ? 1.0f : 0.0f, horizontal ? 0.0f : 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, firstPass ? current_texture_ : pingpongColorBuffers[!horizontal]);
        glUniform1i(glGetUniformLocation(shader_program_, "image"), 0);

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
        horizontal = !horizontal;
        if (firstPass) firstPass = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
}

void Blur::draw_final_texture() const
{
    GLboolean depth, blend;
    glGetBooleanv(GL_DEPTH_TEST, &depth);
    glGetBooleanv(GL_BLEND, &blend);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(shader_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, pingpongColorBuffers[0]);
    glUniform1i(glGetUniformLocation(shader_program_, "image"), 0);

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

void Blur::Destory()
{
    if (!initialized_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(2, pingpongFBO);
    glDeleteTextures(2, pingpongColorBuffers);
    glDeleteTextures(1, &current_texture_);
    if (g_gl30Available) glDeleteVertexArrays(1, &quad_vao_);
    glDeleteBuffers(1, &quad_vbo_);
    if (shader_program_) glDeleteProgram(shader_program_);
    pingpongFBO[0] = pingpongFBO[1] = 0;
    pingpongColorBuffers[0] = pingpongColorBuffers[1] = 0;
    current_texture_ = 0;
    quad_vao_ = 0; quad_vbo_ = 0; shader_program_ = 0;
    initialized_ = false;
}

void Blur::initialize_pingpong(const int width, const int height)
{
    glGenFramebuffers(2, pingpongFBO);
    glGenTextures(2, pingpongColorBuffers);
    GLint internalFmt = g_gl30Available ? GL_RGBA8 : GL_RGBA;
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, pingpongColorBuffers[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongColorBuffers[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Blur::initialize_texture(const int width, const int height)
{
    GLint internalFmt = g_gl30Available ? GL_RGBA8 : GL_RGBA;
    glGenTextures(1, &current_texture_);
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Blur::initialize_quad()
{
    constexpr GLfloat vertices[] = {
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
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    } else {
        glGenBuffers(1, &quad_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void Blur::initialize_shader()
{
    if (!shader_program_) shader_program_ = glCreateProgram();
    const char* vsSrc = getVertBlur();
    const char* fsSrc = getFragBlur();
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSrc, nullptr); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSrc, nullptr); glCompileShader(fs);
    glAttachShader(shader_program_, vs);
    glAttachShader(shader_program_, fs);
    glLinkProgram(shader_program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

void Blur::resize_texture(int width, int height)
{
    GLint internalFmt = g_gl30Available ? GL_RGBA8 : GL_RGBA;
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, pingpongColorBuffers[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    texture_width_ = width; texture_height_ = height;
}

void Blur::copy_to_current() const
{
    if (g_gl30Available) glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    else glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, texture_width_, texture_height_);
}
