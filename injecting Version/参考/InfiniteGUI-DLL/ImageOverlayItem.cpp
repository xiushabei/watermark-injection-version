#include "ImageOverlayItem.h"
#include "OverlayPersistence.h"
#include "GifDecoder.h"
#include "opengl_hook.h"
#include <algorithm>
#include <GL/glew.h>
#include <GL/GL.h>
#include <stb_image.h>

ImageOverlayItem::ImageOverlayItem()
{
    type = ItemType::Util;
    name = "ImageOverlayItem";
    description = u8"水印图片叠加层";
    icon = "I";
    isEnabled = true;
}

ImageOverlayItem& ImageOverlayItem::Instance()
{
    static ImageOverlayItem instance;
    return instance;
}

static GLuint UploadTexture(const unsigned char* rgba, int width, int height)
{
    if (!wglGetCurrentContext()) return 0;
    if (!rgba || width <= 0 || height <= 0) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return tex;
}

static std::shared_ptr<ImageOverlay> CreateOverlayFromFile(const std::string& filePath)
{
    auto overlay = std::make_shared<ImageOverlay>();

    size_t pos = filePath.find_last_of("/\\");
    overlay->originalName = (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;
    overlay->fileName = overlay->originalName;

    if (GifDecoder::IsGifFile(filePath))
    {
        overlay->isGif = true;
        DecodeResult result = GifDecoder::Load(filePath);
        if (!result.frames.empty())
        {
            overlay->originalWidth = result.canvasWidth;
            overlay->originalHeight = result.canvasHeight;
            for (auto& frame : result.frames)
            {
                if (frame.width <= 0) frame.width = result.canvasWidth;
                if (frame.height <= 0) frame.height = result.canvasHeight;
                if (!frame.rgba.empty())
                {
                    GLuint tex = UploadTexture(frame.rgba.data(), frame.width, frame.height);
                    if (tex) {
                        overlay->frameTextures.push_back(tex);
                        overlay->frameDelays.push_back(frame.delayMs);
                    }
                }
            }
        }
    }

    if (overlay->frameTextures.empty())
    {
        overlay->isGif = false;
        int w, h, channels;
        unsigned char* data = stbi_load(filePath.c_str(), &w, &h, &channels, 4);
        if (data)
        {
            overlay->originalWidth = w;
            overlay->originalHeight = h;
            GLuint tex = UploadTexture(data, w, h);
            if (tex) {
                overlay->frameTextures.push_back(tex);
                overlay->frameDelays.push_back(0);
            }
            stbi_image_free(data);
        }
    }

    return overlay;
}

std::shared_ptr<ImageOverlay> ImageOverlayItem::AddOverlay(const std::string& fileName)
{
    std::string fullPath = OverlayPersistence::GetImagePath(fileName);
    auto overlay = CreateOverlayFromFile(fullPath);
    if (!overlay->frameTextures.empty())
    {
        overlay->zOrder = nextZOrder++;
        overlay->xFraction = 0.5;
        overlay->yFraction = 0.5;
        overlays.push_back(overlay);
    }
    else if (!overlay->loadPath.empty())
    {
        overlay->zOrder = nextZOrder++;
        overlay->xFraction = 0.5;
        overlay->yFraction = 0.5;
        overlay->loadPath = fullPath;
        overlays.push_back(overlay);
    }
    return overlay;
}

bool ImageOverlayItem::RemoveOverlay(ImageOverlay* overlay)
{
    auto it = std::find_if(overlays.begin(), overlays.end(),
        [overlay](const std::shared_ptr<ImageOverlay>& o) { return o.get() == overlay; });
    if (it != overlays.end()) { overlays.erase(it); return true; }
    return false;
}

ImageOverlay* ImageOverlayItem::GetOverlayAt(float x, float y)
{
    for (int i = (int)overlays.size() - 1; i >= 0; i--)
        if (overlays[i]->visible && overlays[i]->ContainsPoint(x, y))
            return overlays[i].get();
    return nullptr;
}

ImageOverlay* ImageOverlayItem::GetOverlayAtCorner(float x, float y)
{
    for (int i = (int)overlays.size() - 1; i >= 0; i--)
        if (overlays[i]->visible && overlays[i]->IsOverCorner(x, y))
            return overlays[i].get();
    return nullptr;
}

void ImageOverlayItem::SetOverlayZOrder(ImageOverlay* overlay, int z)
{
    overlay->zOrder = z;
    std::sort(overlays.begin(), overlays.end(),
        [](const std::shared_ptr<ImageOverlay>& a, const std::shared_ptr<ImageOverlay>& b) {
            return a->zOrder < b->zOrder;
        });
}

void ImageOverlayItem::Toggle() { isEnabled = !isEnabled; }
void ImageOverlayItem::Reset() { overlays.clear(); }

void ImageOverlayItem::Load(const nlohmann::json& j)
{
    if (!j.contains("overlays")) return;
    for (auto& node : j["overlays"])
    {
        auto overlay = std::make_shared<ImageOverlay>();
        overlay->fileName = node.value("fileName", "");
        overlay->originalName = node.value("originalName", "");
        overlay->xFraction = node.value("xFraction", 0.5);
        overlay->yFraction = node.value("yFraction", 0.5);
        overlay->scale = node.value("scale", 1.0f);
        overlay->visible = node.value("visible", true);
        overlay->locked = node.value("locked", false);
        overlay->zOrder = node.value("zOrder", 0);
        overlay->keybindKey = node.value("keybindKey", -1);

        std::string modeStr = node.value("movementMode", "NONE");
        if (modeStr == "WAYPOINT") overlay->movementMode = OverlayMovementMode::WAYPOINT;
        else if (modeStr == "RANDOM_MOVE") overlay->movementMode = OverlayMovementMode::RANDOM_MOVE;
        else if (modeStr == "RANDOM_POSITION") overlay->movementMode = OverlayMovementMode::RANDOM_POSITION;
        else overlay->movementMode = OverlayMovementMode::NONE;

        if (node.contains("waypoints")) {
            for (auto& wp : node["waypoints"])
                overlay->waypoints.push_back({ wp["x"], wp["y"] });
        }

        std::string fullPath = OverlayPersistence::GetImagePath(overlay->fileName);
        auto tempOverlay = CreateOverlayFromFile(fullPath);
        if (tempOverlay && !tempOverlay->frameTextures.empty())
        {
            overlay->frameTextures = std::move(tempOverlay->frameTextures);
            overlay->frameDelays = std::move(tempOverlay->frameDelays);
            overlay->originalWidth = tempOverlay->originalWidth;
            overlay->originalHeight = tempOverlay->originalHeight;
            overlay->isGif = tempOverlay->isGif;
        } else {
            overlay->loadPath = fullPath;
            overlay->originalWidth = tempOverlay ? tempOverlay->originalWidth : 0;
            overlay->originalHeight = tempOverlay ? tempOverlay->originalHeight : 0;
        }
        overlays.push_back(overlay);
    }
    if (!overlays.empty()) nextZOrder = overlays.back()->zOrder + 1;
}

void ImageOverlayItem::Save(nlohmann::json& j) const
{
    j["type"] = name;
    j["overlays"] = nlohmann::json::array();
    for (auto& overlay : overlays)
    {
        nlohmann::json node;
        node["fileName"] = overlay->fileName;
        node["originalName"] = overlay->originalName;
        node["xFraction"] = overlay->xFraction;
        node["yFraction"] = overlay->yFraction;
        node["scale"] = overlay->scale;
        node["visible"] = overlay->visible;
        node["locked"] = overlay->locked;
        node["zOrder"] = overlay->zOrder;
        node["keybindKey"] = overlay->keybindKey;

        switch (overlay->movementMode) {
        case OverlayMovementMode::WAYPOINT: node["movementMode"] = "WAYPOINT"; break;
        case OverlayMovementMode::RANDOM_MOVE: node["movementMode"] = "RANDOM_MOVE"; break;
        case OverlayMovementMode::RANDOM_POSITION: node["movementMode"] = "RANDOM_POSITION"; break;
        default: node["movementMode"] = "NONE"; break;
        }

        if (!overlay->waypoints.empty()) {
            node["waypoints"] = nlohmann::json::array();
            for (auto& wp : overlay->waypoints) {
                nlohmann::json wpNode;
                wpNode["x"] = wp.xFraction;
                wpNode["y"] = wp.yFraction;
                node["waypoints"].push_back(wpNode);
            }
        }
        j["overlays"].push_back(node);
    }
}

void ImageOverlayItem::TryLoadTextures(ImageOverlay* overlay)
{
    if (!overlay || !overlay->loadPath.empty() || !overlay->frameTextures.empty()) return;
    if (!wglGetCurrentContext()) return;

    auto loaded = std::make_shared<ImageOverlay>();
    loaded->originalName = overlay->originalName;
    loaded->fileName = overlay->fileName;

    if (GifDecoder::IsGifFile(overlay->loadPath)) {
        DecodeResult result = GifDecoder::Load(overlay->loadPath);
        if (!result.frames.empty()) {
            loaded->originalWidth = result.canvasWidth;
            loaded->originalHeight = result.canvasHeight;
            for (auto& frame : result.frames) {
                if (!frame.rgba.empty()) {
                    GLuint tex = UploadTexture(frame.rgba.data(), frame.width, frame.height);
                    if (tex) { loaded->frameTextures.push_back(tex); loaded->frameDelays.push_back(frame.delayMs); }
                }
            }
        }
    }
    if (loaded->frameTextures.empty()) {
        int w, h, ch;
        unsigned char* data = stbi_load(overlay->loadPath.c_str(), &w, &h, &ch, 4);
        if (data) {
            loaded->originalWidth = w; loaded->originalHeight = h;
            GLuint tex = UploadTexture(data, w, h);
            if (tex) { loaded->frameTextures.push_back(tex); loaded->frameDelays.push_back(0); }
            stbi_image_free(data);
        }
    }

    if (!loaded->frameTextures.empty()) {
        overlay->frameTextures = std::move(loaded->frameTextures);
        overlay->frameDelays = std::move(loaded->frameDelays);
        overlay->originalWidth = loaded->originalWidth;
        overlay->originalHeight = loaded->originalHeight;
        overlay->isGif = loaded->isGif;
        overlay->loadPath.clear();
    }
}

void ImageOverlayItem::DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth)
{
    ImGui::Text("Image Overlays: %zu", overlays.size());
}
