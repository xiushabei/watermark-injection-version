#include "MovementController.h"
#include "ImageOverlayItem.h"
#include "opengl_hook.h"

void MovementController::TickAll()
{
    int screenW = opengl_hook::screen_size.x;
    int screenH = opengl_hook::screen_size.y;
    if (screenW <= 0 || screenH <= 0) return;

    auto& overlays = ImageOverlayItem::Instance().GetOverlays();
    for (auto& overlay : overlays) {
        overlay->TickMovement(screenW, screenH);
        overlay->UpdateCache(screenW, screenH);
    }
}
