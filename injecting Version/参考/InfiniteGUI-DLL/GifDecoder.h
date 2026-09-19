#pragma once
#include <string>
#include <vector>
#include <GL/glew.h>
#include <GL/GL.h>

struct DecodedFrame {
    int width = 0;
    int height = 0;
    int delayMs = 100;
    std::vector<unsigned char> rgba;
};

struct DecodeResult {
    std::vector<DecodedFrame> frames;
    int canvasWidth = 0;
    int canvasHeight = 0;
};

class GifDecoder {
public:
    static DecodeResult Load(const std::string& filePath);
    static bool IsGifFile(const std::string& filePath);
};
