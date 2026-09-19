#include "Menu.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "ImGuiStd.h"
#include "ConfigManager.h"
#include "FileUtils.h"
#include "opengl_hook.h"
#include <thread>

#include "AudioManager.h"
#include "ItemManager.h"
#include "MoresPanel.h"

static ImVec4 myWindowBgColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
static ImVec4 tarWindowBgColor = ImVec4(0.0f, 0.0f, 0.0f, 0.3f);

void Menu::RenderGui()
{
    if (!isEnabled)
    {
        return;
    }

    if (state == MENU_STATE_SETTINGS)
    {
        ImGui::SetNextWindowPos(ImVec2((ImGui::GetIO().DisplaySize.x - ImGui::GetIO().DisplaySize.x / 2), (ImGui::GetIO().DisplaySize.y - ImGui::GetIO().DisplaySize.y / 2)), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2((float)opengl_hook::screen_size.x + 10, (float)opengl_hook::screen_size.y + 10), ImGuiCond_Always);

        ImGuiIO& io = ImGui::GetIO();
        float speed = 5.0f * std::clamp(io.DeltaTime, 0.0f, 0.05f);
        myWindowBgColor = ImLerp(myWindowBgColor, tarWindowBgColor, speed);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, myWindowBgColor);
        ImGui::Begin("##MenuOverlay", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
        ImGui::PopStyleColor();
        ImGui::PushFont(NULL, itemStyle.fontSize);
        if (itemStyle.rainbowFont)
            processRainbowFont();
        else
            ImGui::PushStyleColor(ImGuiCol_Text, itemStyle.fontColor);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, itemStyle.bgColor);
        ImGui::PushStyleColor(ImGuiCol_Border, itemStyle.borderColor);
        PushRounding(itemStyle.windowRounding);
        panelAnim.blurriness = (float)blur->blurriness_value;
        ShowSettings(&opengl_hook::gui.done);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(7);

        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::End();
    }
}

void Menu::RenderBeforeGui()
{
    if (isEnabled && blur->menu_blur)
        blur->RenderBlur(panelAnim.blurriness);
}

void Menu::RenderAfterGui()
{
}

void Menu::OnKeyEvent(bool state, bool isRepeat, WPARAM key)
{
    if (key == NULL || isRepeat) return;
    if (state)
    {
        if (key == VK_ESCAPE)
        {
            if (editModeActive)
            {
                EndEditMode();
                return;
            }
            if (isEnabled)
            {
                isEnabled = false;
                Toggle();
            }
        }
    }
}

int Menu::GetKeyBind()
{
    return keybinds.at("Menu keybind");
}

void Menu::Toggle()
{
    static RECT gameWindowRect;
    if (!isEnabled)
    {
        if (GlobalConfig::Instance().autoSave) {
            ConfigManager::Instance().Save();
            NotificationItem::Instance().AddNotification(NotificationType_Success, u8"自动保存：配置已保存");
        }
        RECT rect;
        if (GetWindowRect(opengl_hook::handle_window, &rect)) {
            int centerX = (rect.left + rect.right) / 2;
            int centerY = (rect.top + rect.bottom) / 2;
            SetCursorPos(centerX, centerY);
        }
        ClipCursor(&gameWindowRect);
        myWindowBgColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        panelAnim.state = 0.0f;
        panelAnim.blurriness = 0.0f;
        dirtyState.animating = false;
        dirtyState.contentDirty = true;
        state = MENU_STATE_SETTINGS;
    }
    else
    {
        needRepos = true;
        GetClipCursor(&gameWindowRect);
        ClipCursor(NULL);
        dirtyState.animating = true;
        state = MENU_STATE_SETTINGS;
        editModeActive = false;
    }
}

void Menu::StartEditMode()
{
    editModeActive = true;
    state = MENU_STATE_EDIT;
    isEnabled = false;
    dirtyState.contentDirty = true;
    ClipCursor(NULL);
}

void Menu::EndEditMode()
{
    editModeActive = false;
    state = MENU_STATE_SETTINGS;
    isEnabled = true;
    needRepos = true;
    dirtyState.contentDirty = true;
        if (GlobalConfig::Instance().autoSave) {
            ConfigManager::Instance().Save();
            NotificationItem::Instance().AddNotification(NotificationType_Success, "Auto save: Config saved.");
        }
}

ImVec2 menuInnerSize = ImVec2(900, 556);
ImVec2 menuSize = ImVec2(menuInnerSize.x + 6.0f, menuInnerSize.y + 6.0f);
void Menu::ShowSettings(bool* done)
{
    if (needRepos)
    {
        ImGui::SetNextWindowPos(
            ImVec2(ImGui::GetIO().DisplaySize.x - ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y - ImGui::GetIO().DisplaySize.y / 2), 
        NULL, 
       ImVec2(0.5f, 0.5f));
        needRepos = false;
    }
    ImGui::SetNextWindowSize(menuSize, ImGuiCond_Once);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("##SettingsWindow", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(3, 3));
    if (!initialized)
    {
        settingMenu->Init();
        initialized = true;
    }
    if (settingMenu->Draw(opengl_hook::gui.done))
    {
        isEnabled = false;
        Toggle();
    }
    ImGui::End();
}

void Menu::DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth)
{
    float bigItemWidth = centerX * 2.0f - bigPadding * 4.0f;

    ImGui::SetCursorPosX(bigPadding);
    ImGui::PushItemWidth(itemWidth);
    ImGui::Checkbox("Blur Mode", &blur->menu_blur);
    ImGui::SetCursorPosX(bigPadding);
    ImGui::PushItemWidth(bigItemWidth);
    ImGui::SliderInt("Blur Strength", &blur->blurriness_value, 0, 10);
    DrawKeybindSettings(bigPadding, centerX, itemWidth);
    DrawSoundSettings(bigPadding, centerX, itemWidth);
    DrawStyleSettings(bigPadding, centerX, itemWidth);
}

void Menu::Load(const nlohmann::json& j)
{
    LoadKeybind(j);
    if(j.contains("menu_blur")) blur->menu_blur = j["menu_blur"].get<bool>();
    if(j.contains("blurriness_value")) blur->blurriness_value = j["blurriness_value"].get<int>();
    LoadSound(j);
    LoadStyle(j);
}

void Menu::Save(nlohmann::json& j) const
{
    SaveKeybind(j);
    j["menu_blur"] = blur->menu_blur;
    j["blurriness_value"] = blur->blurriness_value;
    SaveSound(j);
    SaveStyle(j);
    j["type"] = name;
}

void MoresPanel::EnterEditMode()
{
    Menu::Instance().StartEditMode();
}
