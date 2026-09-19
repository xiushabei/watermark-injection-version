#include "ImageOverlay.h"
#include <algorithm>
#include <GL/glew.h>
#include <GL/GL.h>

ImageOverlay::~ImageOverlay()
{
    for (GLuint tex : frameTextures) {
        if (tex) glDeleteTextures(1, &tex);
    }
    frameTextures.clear();
}

void ImageOverlay::UpdateCache(int screenW, int screenH)
{
    float w = GetDisplayWidth();
    float h = GetDisplayHeight();
    cachedX = (float)(xFraction * screenW - w / 2.0);
    cachedY = (float)(yFraction * screenH - h / 2.0);
    cachedW = w;
    cachedH = h;
}

bool ImageOverlay::ContainsPoint(float px, float py) const
{
    return px >= cachedX && px <= cachedX + cachedW &&
           py >= cachedY && py <= cachedY + cachedH;
}

bool ImageOverlay::IsOverCorner(float px, float py) const
{
    float cx = cachedX + cachedW;
    float cy = cachedY + cachedH;
    return px >= cx - 16 && px <= cx + 16 &&
           py >= cy - 16 && py <= cy + 16;
}

void ImageOverlay::TickMovement(int screenW, int screenH)
{
    if (movementMode == OverlayMovementMode::NONE) return;

    int stayFrame = std::max(1, isGif ? (int)frameDelays.size() : 60);

    if (movementMode == OverlayMovementMode::WAYPOINT && !waypoints.empty())
    {
        const Waypoint& target = waypoints[currentWaypointIndex % waypoints.size()];
        double dx = target.xFraction - xFraction;
        double dy = target.yFraction - yFraction;
        double dist = sqrt(dx * dx + dy * dy);
        if (dist < 0.005)
        {
            waypointStayCounter++;
            if (waypointStayCounter > stayFrame)
            {
                waypointStayCounter = 0;
                currentWaypointIndex = (currentWaypointIndex + 1) % waypoints.size();
            }
        }
        else
        {
            double speed = 0.02;
            xFraction += (dx / dist) * speed;
            yFraction += (dy / dist) * speed;
        }
    }
    else if (movementMode == OverlayMovementMode::RANDOM_MOVE)
    {
        double speed = 0.01 * moveSpeed;
        xFraction += moveDirX * speed;
        yFraction += moveDirY * speed;

        float w = GetDisplayWidth();
        float h = GetDisplayHeight();
        float marginX = w / screenW / 2;
        float marginY = h / screenH / 2;

        if (xFraction < marginX || xFraction > 1.0 - marginX) moveDirX = -moveDirX;
        if (yFraction < marginY || yFraction > 1.0 - marginY) moveDirY = -moveDirY;
        xFraction = std::clamp(xFraction, (double)marginX, 1.0 - marginX);
        yFraction = std::clamp(yFraction, (double)marginY, 1.0 - marginY);

        // Random direction change
        if (rand() % 100 < 2) {
            double angle = (double)(rand() % 628) / 100.0;
            moveDirX = cos(angle);
            moveDirY = sin(angle);
            double len = sqrt(moveDirX * moveDirX + moveDirY * moveDirY);
            if (len > 0) { moveDirX /= len; moveDirY /= len; }
        }
    }
    else if (movementMode == OverlayMovementMode::RANDOM_POSITION)
    {
        randomPositionCounter++;
        if (randomPositionCounter > stayFrame)
        {
            randomPositionCounter = 0;
            xFraction = (double)(rand() % 900 + 50) / 1000.0;
            yFraction = (double)(rand() % 900 + 50) / 1000.0;
        }
    }
}

void ImageOverlay::AdvanceFrame()
{
    if (frameTextures.empty()) return;
    currentFrameIndex = (currentFrameIndex + 1) % (int)frameTextures.size();
}

GLuint ImageOverlay::GetCurrentTexture() const
{
    if (frameTextures.empty()) return 0;
    if (currentFrameIndex < 0 || currentFrameIndex >= (int)frameTextures.size())
        return frameTextures[0];
    return frameTextures[currentFrameIndex];
}
