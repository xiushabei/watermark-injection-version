#pragma once
#include <cstdint>
#include <GL/glew.h>

class Blur
{
public:
    bool menu_blur = true;
    int blurriness_value = 5;
    void RenderBlur(float cur_blurriness);
    void Destory();
private:
    void apply_blur(int iterations) const;
    void draw_final_texture() const;
    void initialize_pingpong(const int width, const int height);
    void initialize_texture(const int width, const int height);
    void initialize_quad();
    void initialize_shader();

    void resize_texture(int width, int height);
    void draw_texture() const;
    void copy_to_current() const;

    uint32_t shader_program_;

    uint32_t current_texture_;

    uint32_t quad_vao_;
    uint32_t quad_vbo_;

    int32_t texture_width_;
    int32_t texture_height_;

    GLuint pingpongFBO[2];
    GLuint pingpongColorBuffers[2];
	
    bool initialized_ = false;

};
