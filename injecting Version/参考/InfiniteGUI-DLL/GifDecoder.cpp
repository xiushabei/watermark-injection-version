#include "GifDecoder.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <fstream>
#include <GL/glew.h>
#include <GL/GL.h>

#pragma pack(push, 1)
struct GIFHeader {
    char sig[3];
    char ver[3];
    unsigned short width;
    unsigned short height;
    unsigned char packed;
    unsigned char bgColor;
    unsigned char aspect;
};
#pragma pack(pop)

struct GifColor {
    unsigned char r, g, b;
};

static std::vector<unsigned char> LZWDecode(const std::vector<unsigned char>& data, int minCodeSize)
{
    std::vector<unsigned char> result;
    if (data.empty()) return result;

    int clearCode = 1 << minCodeSize;
    int eofCode = clearCode + 1;
    int codeSize = minCodeSize + 1;
    int nextCode = clearCode + 2;
    int maxCode = (1 << codeSize);

    struct Entry { std::vector<unsigned char> data; };
    std::vector<Entry> table;
    table.resize(4096);

    // Initialize table
    for (int i = 0; i < clearCode; i++) {
        table[i].data.push_back((unsigned char)i);
    }

    std::vector<unsigned char> bitBuffer;
    int bitPos = 0;

    auto readBits = [&](int bits) -> int {
        int val = 0;
        for (int i = 0; i < bits; i++) {
            int byteIdx = bitPos >> 3;
            int bitIdx = bitPos & 7;
            if (byteIdx >= (int)data.size()) return -1;
            if (data[byteIdx] & (1 << bitIdx)) val |= (1 << i);
            bitPos++;
        }
        return val;
    };

    int oldCode = -1;
    while (true) {
        int code = readBits(codeSize);
        if (code == -1) break;
        if (code == eofCode) break;

        if (code == clearCode) {
            codeSize = minCodeSize + 1;
            maxCode = 1 << codeSize;
            nextCode = clearCode + 2;
            table.resize(4096);
            for (int i = 0; i < clearCode; i++) {
                table[i].data.clear();
                table[i].data.push_back((unsigned char)i);
            }
            oldCode = -1;
            continue;
        }

        Entry entry;
        if (code < nextCode) {
            entry = table[code];
        } else if (code == nextCode && oldCode != -1) {
            entry = table[oldCode];
            entry.data.push_back(entry.data[0]);
        } else {
            break;
        }

        for (unsigned char c : entry.data) result.push_back(c);

        if (oldCode != -1 && nextCode < 4096) {
            Entry newEntry = table[oldCode];
            newEntry.data.push_back(entry.data[0]);
            table[nextCode] = newEntry;
            nextCode++;

            if (nextCode >= maxCode && codeSize < 12) {
                codeSize++;
                maxCode = 1 << codeSize;
            }
        }
        oldCode = code;
    }
    return result;
}

DecodeResult GifDecoder::Load(const std::string& filePath)
{
    DecodeResult result;

    std::ifstream file(filePath, std::ios::binary);
    if (!file) return result;

    GIFHeader header;
    file.read((char*)&header, sizeof(header));
    if (memcmp(header.sig, "GIF", 3) != 0) return result;
    if (memcmp(header.ver, "87a", 3) != 0 && memcmp(header.ver, "89a", 3) != 0) return result;

    result.canvasWidth = header.width;
    result.canvasHeight = header.height;

    bool hasGCT = (header.packed & 0x80) != 0;
    int gctSize = hasGCT ? (1 << ((header.packed & 7) + 1)) : 0;

    std::vector<GifColor> globalColorTable;
    if (hasGCT) {
        globalColorTable.resize(gctSize);
        file.read((char*)globalColorTable.data(), gctSize * 3);
    }

    // Canvas for compositing frames
    std::vector<unsigned char> canvas(result.canvasWidth * result.canvasHeight * 4, 0);

    while (file) {
        unsigned char blockType;
        file.read((char*)&blockType, 1);
        if (!file) break;

        if (blockType == 0x2C) { // Image Descriptor
            unsigned short imgLeft, imgTop, imgWidth, imgHeight;
            unsigned char packed;
            file.read((char*)&imgLeft, 2);
            file.read((char*)&imgTop, 2);
            file.read((char*)&imgWidth, 2);
            file.read((char*)&imgHeight, 2);
            file.read((char*)&packed, 1);

            bool hasLCT = (packed & 0x80) != 0;
            bool interlaced = (packed & 0x40) != 0;
            int lctSize = hasLCT ? (1 << ((packed & 7) + 1)) : 0;

            std::vector<GifColor> localColorTable;
            if (hasLCT) {
                localColorTable.resize(lctSize);
                file.read((char*)localColorTable.data(), lctSize * 3);
            }

            const auto& colorTable = hasLCT ? localColorTable : globalColorTable;
            int colorCount = hasLCT ? lctSize : gctSize;

            unsigned char minCodeSize;
            file.read((char*)&minCodeSize, 1);

            // Read LZW data
            std::vector<unsigned char> lzwData;
            while (true) {
                unsigned char blockSize;
                file.read((char*)&blockSize, 1);
                if (blockSize == 0) break;
                std::vector<unsigned char> block(blockSize);
                file.read((char*)block.data(), blockSize);
                lzwData.insert(lzwData.end(), block.begin(), block.end());
            }

            std::vector<unsigned char> decoded = LZWDecode(lzwData, minCodeSize);

            // Draw frame onto canvas
            DecodedFrame frame;
            frame.width = imgWidth;
            frame.height = imgHeight;
            frame.delayMs = 100;

            // First pass: render to canvas and extract frame pixels
            bool transparent = false;
            unsigned char transIndex = 0;
            // Check GCE before this block
            // For now, handle transparent via GCE (we'll post-process)

            std::vector<unsigned char> frameRGBA(imgWidth * imgHeight * 4);

            for (int y = 0; y < (int)imgHeight && y < (int)decoded.size() / imgWidth; y++) {
                int srcRow = y;
                if (interlaced) {
                    // Simplified: not handling interlaced perfectly
                    static const int interlaceOrder[] = {0, 4, 2, 1};
                    static const int interlaceStep[] = {8, 8, 4, 2};
                    int pass = 0;
                    int rowInPass = y;
                    while (pass < 4 && rowInPass >= interlaceStep[pass]) {
                        rowInPass -= interlaceStep[pass];
                        pass++;
                    }
                    if (pass < 4) {
                        srcRow = interlaceOrder[pass] + rowInPass * interlaceStep[pass];
                    }
                }

                if (srcRow >= (int)imgHeight) continue;

                for (int x = 0; x < (int)imgWidth; x++) {
                    int idx = srcRow * imgWidth + x;
                    if (idx < (int)decoded.size()) {
                        unsigned char pixel = decoded[idx];
                        int ci = (int)pixel;
                        int dstIdx = (y * (int)imgWidth + x) * 4;
                        int canvasIdx = ((imgTop + y) * result.canvasWidth + (imgLeft + x)) * 4;

                        if (ci < colorCount) {
                            frameRGBA[dstIdx] = colorTable[ci].r;
                            frameRGBA[dstIdx + 1] = colorTable[ci].g;
                            frameRGBA[dstIdx + 2] = colorTable[ci].b;
                            frameRGBA[dstIdx + 3] = transparent ? 0 : 255;

                            canvas[canvasIdx] = colorTable[ci].r;
                            canvas[canvasIdx + 1] = colorTable[ci].g;
                            canvas[canvasIdx + 2] = colorTable[ci].b;
                            canvas[canvasIdx + 3] = 255;
                        }
                    }
                }
            }

            frame.rgba = frameRGBA;
            result.frames.push_back(frame);
        }
        else if (blockType == 0x21) { // Extension
            unsigned char extLabel;
            file.read((char*)&extLabel, 1);
            if (extLabel == 0xF9) { // GCE (Graphics Control Extension)
                unsigned char blockSize;
                file.read((char*)&blockSize, 1);
                if (blockSize >= 4) {
                    unsigned char packed_gce;
                    unsigned short delayTime;
                    unsigned char transIndex_gce;
                    file.read((char*)&packed_gce, 1);
                    file.read((char*)&delayTime, 2);
                    file.read((char*)&transIndex_gce, 1);

                    bool transparent_gce = (packed_gce & 1) != 0;
                    int disposalMethod = (packed_gce >> 2) & 7;

                    if (!result.frames.empty()) {
                        result.frames.back().delayMs = delayTime * 10;
                        if (result.frames.back().delayMs < 20) result.frames.back().delayMs = 20;

                        if (transparent_gce && transIndex_gce < 256) {
                            int idx = transIndex_gce;
                            auto& rgba = result.frames.back().rgba;
                            for (size_t i = 3; i < rgba.size(); i += 4) {
                                rgba[i] = 0;
                            }
                        }
                    }

                    if (blockSize > 4) {
                        file.seekg(blockSize - 4, std::ios::cur);
                    }
                }
                unsigned char terminator;
                file.read((char*)&terminator, 1);
            }
            else if (extLabel == 0xFF) { // Application Extension
                unsigned char blockSize;
                file.read((char*)&blockSize, 1);
                if (blockSize > 0) file.seekg(blockSize, std::ios::cur);
                // Skip sub-blocks
                while (true) {
                    unsigned char sb;
                    file.read((char*)&sb, 1);
                    if (sb == 0) break;
                    file.seekg(sb, std::ios::cur);
                }
            }
            else if (extLabel == 0x01) { // Plain Text
                while (true) {
                    unsigned char sb;
                    file.read((char*)&sb, 1);
                    if (sb == 0) break;
                    file.seekg(sb, std::ios::cur);
                }
            }
            else { // Other extension
                while (true) {
                    unsigned char sb;
                    file.read((char*)&sb, 1);
                    if (sb == 0) break;
                    file.seekg(sb, std::ios::cur);
                }
            }
        }
        else if (blockType == 0x3B) { // Trailer
            break;
        }
        else { // Unknown block, try to skip sub-blocks
            while (true) {
                unsigned char sb;
                file.read((char*)&sb, 1);
                if (sb == 0) break;
                file.seekg(sb, std::ios::cur);
            }
        }
    }

    // If no explicit GCE, set delays for all frames
    for (auto& f : result.frames) {
        if (f.delayMs <= 0) f.delayMs = 100;
    }

    return result;
}

bool GifDecoder::IsGifFile(const std::string& filePath)
{
    FILE* f = fopen(filePath.c_str(), "rb");
    if (!f) return false;
    char sig[3] = {};
    fread(sig, 1, 3, f);
    fclose(f);
    return (memcmp(sig, "GIF", 3) == 0);
}
