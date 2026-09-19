#pragma once
#include <string>
#include <vector>
#include <GL/glew.h>
#include <GL/GL.h>

enum class OverlayMovementMode {
    NONE,
    WAYPOINT,
    RANDOM_MOVE,
    RANDOM_POSITION
};

struct Waypoint {
    double xFraction = 0.5;
    double yFraction = 0.5;
};

class ImageOverlay {
public:
    std::string fileName;
    std::string originalName;
    double xFraction = 0.5;
    double yFraction = 0.5;
    float scale = 1.0f;
    bool visible = true;
    bool locked = false;
    OverlayMovementMode movementMode = OverlayMovementMode::NONE;
    std::vector<Waypoint> waypoints;
    int currentWaypointIndex = 0;
    int keybindKey = -1;
    int zOrder = 0;

    // Texture data
    std::vector<GLuint> frameTextures;
    std::vector<int> frameDelays;
    int currentFrameIndex = 0;
    int originalWidth = 0;
    int originalHeight = 0;
    bool isGif = false;
    std::string loadPath;

    // Runtime state
    double moveDirX = 1.0;
    double moveDirY = 1.0;
    double moveSpeed = 2.0;
    int waypointStayCounter = 0;
    int randomPositionCounter = 0;

    // Pixel cache
    float cachedX = 0, cachedY = 0, cachedW = 0, cachedH = 0;

    ImageOverlay() = default;
    ~ImageOverlay();

    void UpdateCache(int screenW, int screenH);
    bool ContainsPoint(float px, float py) const;
    bool IsOverCorner(float px, float py) const;

    float GetDisplayWidth() const { return originalWidth * scale; }
    float GetDisplayHeight() const { return originalHeight * scale; }

    void TickMovement(int screenW, int screenH);
    void AdvanceFrame();
    GLuint GetCurrentTexture() const;
};
