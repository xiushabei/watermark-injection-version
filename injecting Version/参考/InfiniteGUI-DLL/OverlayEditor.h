#pragma once
#include "imgui/imgui.h"
#include "opengl_hook.h"
#include "Menu.h"
#include "ImageOverlayItem.h"
#include "ImageOverlay.h"

class OverlayEditor {
public:
    static void RenderEditModeOverlay();
    static void OnMouseClick(float x, float y, int button);
    static void OnMouseRelease(float x, float y, int button);
    static void OnMouseMove(float x, float y);
    static void OnMouseWheel(float delta);
    static void OnKeyPress(int vkKey);
    static void EnterEditMode();
    static void ExitEditMode();
    static bool IsEditModeActive() { return Menu::Instance().IsEditMode(); }

private:
    static void RenderOverlay(ImageOverlay* overlay, bool isSelected);
    static void RenderSelectionBorder(ImageOverlay* overlay);
    static void RenderContextMenu();
    static void RenderWaypointUI();
    static void RenderKeybindCaptureUI();
    static void HandleDrag(float mx, float my);
    static void HandleResize(float mx, float my);

    static ImageOverlay* selectedOverlay;
    static bool isDragging;
    static bool isResizing;
    static float dragOffsetX, dragOffsetY;
    static float clickStartX, clickStartY;
    static DWORD lastClickTime;
    static int clickCount;

    // Waypoint mode
    static bool waypointMode;
    static ImageOverlay* waypointOverlay;

    // Keybind mode
    static bool keybindMode;
    static ImageOverlay* keybindOverlay;

    // Context menu
    static bool showContextMenu;
    static ImageOverlay* contextOverlay;
    static float contextX, contextY;
};
