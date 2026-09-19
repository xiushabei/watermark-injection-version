#define NOMINMAX
#undef min
#undef max
#include "OverlayEditor.h"
#include "imgui/imgui_internal.h"
#include <GL/glew.h>
#include <GL/GL.h>
#include <algorithm>


ImageOverlay* OverlayEditor::selectedOverlay = nullptr;
bool OverlayEditor::isDragging = false;
bool OverlayEditor::isResizing = false;
float OverlayEditor::dragOffsetX = 0;
float OverlayEditor::dragOffsetY = 0;
float OverlayEditor::clickStartX = 0;
float OverlayEditor::clickStartY = 0;
DWORD OverlayEditor::lastClickTime = 0;
int OverlayEditor::clickCount = 0;
bool OverlayEditor::waypointMode = false;
ImageOverlay* OverlayEditor::waypointOverlay = nullptr;
bool OverlayEditor::keybindMode = false;
ImageOverlay* OverlayEditor::keybindOverlay = nullptr;
bool OverlayEditor::showContextMenu = false;
ImageOverlay* OverlayEditor::contextOverlay = nullptr;
float OverlayEditor::contextX = 0;
float OverlayEditor::contextY = 0;

void OverlayEditor::EnterEditMode()
{
    selectedOverlay = nullptr;
    isDragging = false;
    isResizing = false;
    waypointMode = false;
    keybindMode = false;
    showContextMenu = false;
    lastClickTime = 0;
}

void OverlayEditor::ExitEditMode()
{
    selectedOverlay = nullptr;
    isDragging = false;
    isResizing = false;
    waypointMode = false;
    keybindMode = false;
    showContextMenu = false;
}

void OverlayEditor::RenderEditModeOverlay()
{
    if (!Menu::Instance().IsEditMode()) return;
    if (!opengl_hook::gui.isInit) return;

    int screenW = opengl_hook::screen_size.x;
    int screenH = opengl_hook::screen_size.y;
    if (screenW <= 0 || screenH <= 0) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Update all overlay caches
    auto& overlays = ImageOverlayItem::Instance().GetOverlays();

    // Render all overlays
    for (auto& overlay : overlays)
    {
        overlay->UpdateCache(screenW, screenH);
        bool sel = (overlay.get() == selectedOverlay);
        RenderOverlay(overlay.get(), sel);
    }

    // Render selection border on top
    if (selectedOverlay && !isDragging)
    {
        RenderSelectionBorder(selectedOverlay);
    }

    // Render waypoint UI
    if (waypointMode && waypointOverlay)
    {
        RenderWaypointUI();
    }

    // Render keybind capture UI
    if (keybindMode)
    {
        RenderKeybindCaptureUI();
    }

    // Render context menu
    if (showContextMenu && contextOverlay)
    {
        RenderContextMenu();
    }

    // Edit mode hint text
    if (!waypointMode && !keybindMode)
    {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)screenW, 30), IM_COL32(0, 0, 0, 160));
        dl->AddText(ImVec2(10, 6), IM_COL32(255, 255, 255, 200), u8"编辑模式 - Esc: 退出 | 左键: 选择 | 拖拽: 移动 | 右键: 菜单 | 滚轮: 缩放");
    }

    // Handle mouse interaction
    if (!waypointMode && !keybindMode && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
    {
        bool mouseDown = ImGui::IsMouseDown(0);
        bool mouseClicked = ImGui::IsMouseClicked(0);
        bool mouseReleased = ImGui::IsMouseReleased(0);
        bool mouseRightClicked = ImGui::IsMouseClicked(1);
        float mx = io.MousePos.x;
        float my = io.MousePos.y;

        if (mouseReleased && !isDragging && !isResizing)
        {
            OnMouseRelease(mx, my, 0);
        }

        if (mouseRightClicked)
        {
            OnMouseClick(mx, my, 1);
        }

        if (mouseClicked)
        {
            DWORD now = GetTickCount();
            if (now - lastClickTime < 350 && clickCount > 0) clickCount++;
            else clickCount = 1;
            lastClickTime = now;
            OnMouseClick(mx, my, 0);
        }

        if (mouseDown && isDragging)
        {
            HandleDrag(mx, my);
        }
        if (mouseDown && isResizing)
        {
            HandleResize(mx, my);
        }
    }
}

void OverlayEditor::RenderOverlay(ImageOverlay* overlay, bool isSelected)
{
    if (!overlay) return;

    if (overlay->frameTextures.empty() && !overlay->loadPath.empty())
    {
        ImageOverlayItem::Instance().TryLoadTextures(overlay);
    }

    if (overlay->frameTextures.empty()) return;

    int screenW = opengl_hook::screen_size.x;
    int screenH = opengl_hook::screen_size.y;

    float x = overlay->cachedX;
    float y = overlay->cachedY;
    float w = overlay->cachedW;
    float h = overlay->cachedH;

    GLuint tex = overlay->GetCurrentTexture();
    if (!tex) return;

    float alpha = overlay->visible ? 1.0f : 0.3f;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddImageQuad((ImTextureID)(uintptr_t)tex,
        ImVec2(x, y), ImVec2(x + w, y),
        ImVec2(x + w, y + h), ImVec2(x, y + h),
        ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1),
        IM_COL32(255, 255, 255, (int)(alpha * 255)));
}

void OverlayEditor::RenderSelectionBorder(ImageOverlay* overlay)
{
    float x = overlay->cachedX;
    float y = overlay->cachedY;
    float w = overlay->cachedW;
    float h = overlay->cachedH;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + w + 1, y + h + 1),
        IM_COL32(255, 255, 0, 220), 0, 0, 2.0f);

    // Resize handle (bottom-right corner)
    dl->AddRectFilled(ImVec2(x + w - 8, y + h - 8), ImVec2(x + w + 8, y + h + 8),
        IM_COL32(0, 255, 0, 200));
    dl->AddRect(ImVec2(x + w - 8, y + h - 8), ImVec2(x + w + 8, y + h + 8),
        IM_COL32(255, 255, 255, 200), 0, 0, 1.0f);
}

void OverlayEditor::RenderContextMenu()
{
    if (!contextOverlay) return;

    ImGui::SetNextWindowPos(ImVec2(contextX, contextY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    if (ImGui::Begin("##OverlayContext", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::MenuItem(u8"置顶显示")) {
            int maxZ = 0;
            for (auto& o : ImageOverlayItem::Instance().GetOverlays())
                if (o->zOrder > maxZ) maxZ = o->zOrder;
            ImageOverlayItem::Instance().SetOverlayZOrder(contextOverlay, maxZ + 1);
        }
        if (ImGui::MenuItem(contextOverlay->locked ? u8"取消锁定" : u8"锁定位置")) {
            contextOverlay->locked = !contextOverlay->locked;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(u8"定点移动")) {
            waypointMode = true;
            waypointOverlay = contextOverlay;
            waypointOverlay->movementMode = OverlayMovementMode::WAYPOINT;
        }
        if (ImGui::MenuItem(u8"随机运动")) {
            contextOverlay->movementMode = OverlayMovementMode::RANDOM_MOVE;
        }
        if (ImGui::MenuItem(u8"随机展示")) {
            contextOverlay->movementMode = OverlayMovementMode::RANDOM_POSITION;
        }
        if (ImGui::MenuItem(u8"重置运动")) {
            contextOverlay->movementMode = OverlayMovementMode::NONE;
            contextOverlay->waypoints.clear();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(u8"按键绑定...")) {
            keybindMode = true;
            keybindOverlay = contextOverlay;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(u8"删除", NULL, false, true)) {
            ImageOverlayItem::Instance().RemoveOverlay(contextOverlay);
            if (selectedOverlay == contextOverlay) selectedOverlay = nullptr;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
    {
        showContextMenu = false;
        contextOverlay = nullptr;
    }
}

void OverlayEditor::RenderWaypointUI()
{
    if (!waypointOverlay) return;

    int screenW = opengl_hook::screen_size.x;
    int screenH = opengl_hook::screen_size.y;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Dim overlay
    dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)screenW, (float)screenH),
        IM_COL32(0, 0, 0, 135));

    // Draw existing waypoints
    for (size_t i = 0; i < waypointOverlay->waypoints.size(); i++)
    {
        float wx = (float)(waypointOverlay->waypoints[i].xFraction * screenW);
        float wy = (float)(waypointOverlay->waypoints[i].yFraction * screenH);
        dl->AddLine(ImVec2(wx - 8, wy - 8), ImVec2(wx + 8, wy + 8), IM_COL32(255, 0, 0, 220), 2);
        dl->AddLine(ImVec2(wx + 8, wy - 8), ImVec2(wx - 8, wy + 8), IM_COL32(255, 0, 0, 220), 2);

        char idx[8];
        sprintf_s(idx, "%zu", i + 1);
        dl->AddText(ImVec2(wx + 10, wy - 10), IM_COL32(255, 255, 0, 255), idx);
    }

    // Hint text
    dl->AddText(ImVec2(10, (float)screenH - 40), IM_COL32(255, 255, 255, 220),
        u8"航点模式 - 左键: 添加航点 | 右键: 删除上一个 | Enter: 确认 | Esc: 取消");

    // Handle clicks
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(0))
    {
        if (waypointOverlay->waypoints.size() < 7)
        {
            Waypoint wp;
            wp.xFraction = io.MousePos.x / screenW;
            wp.yFraction = io.MousePos.y / screenH;
            waypointOverlay->waypoints.push_back(wp);
        }
    }
    if (ImGui::IsMouseClicked(1) && !waypointOverlay->waypoints.empty())
    {
        waypointOverlay->waypoints.pop_back();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
        waypointMode = false;
        waypointOverlay = nullptr;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        waypointOverlay->waypoints.clear();
        waypointOverlay->movementMode = OverlayMovementMode::NONE;
        waypointMode = false;
        waypointOverlay = nullptr;
    }
}

void OverlayEditor::RenderKeybindCaptureUI()
{
    if (!keybindOverlay) return;

    int screenW = opengl_hook::screen_size.x;
    int screenH = opengl_hook::screen_size.y;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)screenW, (float)screenH),
        IM_COL32(0, 0, 0, 170));

    const char* msg = u8"请按下按键绑定...（Esc: 取消, 退格: 清除绑定）";
    ImVec2 textSize = ImGui::CalcTextSize(msg);
    dl->AddText(ImVec2((screenW - textSize.x) / 2, (screenH - textSize.y) / 2),
        IM_COL32(255, 255, 255, 255), msg);

    ImGuiIO& io = ImGui::GetIO();
    for (int k = 0; k < 256; k++)
    {
        if (ImGui::IsKeyDown((ImGuiKey)k) && !ImGui::IsKeyDown((ImGuiKey)(k-1)))
        {
            int vk = k;
            if (k >= (int)ImGuiKey_A && k <= (int)ImGuiKey_Z)
                vk = 'A' + (k - (int)ImGuiKey_A);
            else if (k >= (int)ImGuiKey_0 && k <= (int)ImGuiKey_9)
                vk = '0' + (k - (int)ImGuiKey_0);
            else if (k >= (int)ImGuiKey_F1 && k <= (int)ImGuiKey_F12)
                vk = VK_F1 + (k - (int)ImGuiKey_F1);
            else if (k == (int)ImGuiKey_Space) vk = VK_SPACE;
            else if (k == (int)ImGuiKey_Tab) vk = VK_TAB;
            else if (k == (int)ImGuiKey_Escape) vk = VK_ESCAPE;
            else if (k == (int)ImGuiKey_Backspace) vk = VK_BACK;
            else continue;

            if (vk == VK_ESCAPE) {
                keybindMode = false; keybindOverlay = nullptr;
            } else if (vk == VK_BACK) {
                keybindOverlay->keybindKey = -1;
                keybindMode = false; keybindOverlay = nullptr;
            } else {
                keybindOverlay->keybindKey = vk;
                keybindMode = false; keybindOverlay = nullptr;
            }
            break;
        }
    }
}

void OverlayEditor::OnMouseClick(float x, float y, int button)
{
    if (button == 0)
    {
        // Check resize first (corner hit test)
        if (selectedOverlay && selectedOverlay->IsOverCorner(x, y))
        {
            isResizing = true;
            return;
        }

        ImageOverlay* hitOverlay = ImageOverlayItem::Instance().GetOverlayAt(x, y);
        if (hitOverlay && !hitOverlay->locked)
        {
            if (clickCount >= 2)
            {
                hitOverlay->visible = !hitOverlay->visible;
                clickCount = 0;
            }

            if (selectedOverlay != hitOverlay)
            {
                clickCount = 0;
            }

            selectedOverlay = hitOverlay;
            isDragging = true;
            dragOffsetX = x - hitOverlay->cachedX;
            dragOffsetY = y - hitOverlay->cachedY;
        }
        else
        {
            if (!isResizing) {
                selectedOverlay = nullptr;
            }
        }
    }
    else if (button == 1)
    {
        ImageOverlay* hitOverlay = ImageOverlayItem::Instance().GetOverlayAt(x, y);
        if (hitOverlay)
        {
            showContextMenu = true;
            contextOverlay = hitOverlay;
            contextX = x;
            contextY = y;
        }
    }
}

void OverlayEditor::OnMouseRelease(float x, float y, int button)
{
    if (button == 0)
    {
        isDragging = false;
        isResizing = false;

        // Update fractions
        if (selectedOverlay)
        {
            int screenW = opengl_hook::screen_size.x;
            int screenH = opengl_hook::screen_size.y;
            if (screenW > 0 && screenH > 0)
            {
                selectedOverlay->xFraction = (selectedOverlay->cachedX + selectedOverlay->cachedW / 2) / screenW;
                selectedOverlay->yFraction = (selectedOverlay->cachedY + selectedOverlay->cachedH / 2) / screenH;
            }
        }
    }
}

void OverlayEditor::OnMouseMove(float x, float y)
{
    HandleDrag(x, y);
    HandleResize(x, y);
}

void OverlayEditor::OnMouseWheel(float delta)
{
    if (selectedOverlay && !selectedOverlay->locked)
    {
        selectedOverlay->scale *= (1.0f + delta * 0.05f);
        if (selectedOverlay->scale < 0.1f) selectedOverlay->scale = 0.1f;
        if (selectedOverlay->scale > 10.0f) selectedOverlay->scale = 10.0f;
    }
}

void OverlayEditor::OnKeyPress(int vkKey)
{
    if (keybindMode && keybindOverlay)
    {
        if (vkKey == VK_ESCAPE) {
            keybindMode = false;
            keybindOverlay = nullptr;
        }
        else if (vkKey == VK_BACK) {
            keybindOverlay->keybindKey = -1;
            keybindMode = false;
            keybindOverlay = nullptr;
        }
        else {
            keybindOverlay->keybindKey = vkKey;
            keybindMode = false;
            keybindOverlay = nullptr;
        }
    }
}

void OverlayEditor::HandleDrag(float mx, float my)
{
    if (!isDragging || !selectedOverlay) return;

    int screenW = opengl_hook::screen_size.x;
    int screenH = opengl_hook::screen_size.y;
    if (screenW <= 0 || screenH <= 0) return;

    float newX = mx - dragOffsetX;
    float newY = my - dragOffsetY;

    float halfW = selectedOverlay->cachedW / 2;
    float halfH = selectedOverlay->cachedH / 2;

    if (newX < -halfW + 1.0f) newX = -halfW + 1.0f;
    if (newX > (float)screenW - selectedOverlay->cachedW + halfW - 1.0f) newX = (float)screenW - selectedOverlay->cachedW + halfW - 1.0f;
    if (newY < -halfH + 1.0f) newY = -halfH + 1.0f;
    if (newY > (float)screenH - selectedOverlay->cachedH + halfH - 1.0f) newY = (float)screenH - selectedOverlay->cachedH + halfH - 1.0f;

    selectedOverlay->cachedX = newX;
    selectedOverlay->cachedY = newY;
}

void OverlayEditor::HandleResize(float mx, float my)
{
    if (!isResizing || !selectedOverlay) return;

    float dx = mx - (selectedOverlay->cachedX + selectedOverlay->cachedW - dragOffsetX);
    float dy = my - (selectedOverlay->cachedY + selectedOverlay->cachedH - dragOffsetY);
    float maxDim = (selectedOverlay->cachedW > selectedOverlay->cachedH) ? selectedOverlay->cachedW : selectedOverlay->cachedH;
    float dScale = (dx + dy) / maxDim * 0.5f;

    selectedOverlay->scale *= (1.0f + dScale);
    if (selectedOverlay->scale < 0.1f) selectedOverlay->scale = 0.1f;
    if (selectedOverlay->scale > 10.0f) selectedOverlay->scale = 10.0f;
}
