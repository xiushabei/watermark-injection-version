#pragma once
#include "menuRule.h"
#include "ImGuiStd.h"
#include "imgui/imgui.h"

class Menu;

class MoresPanel
{
public:
    static void Draw()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::BeginChild("EditPanel", ImVec2(-padding + ImGui::GetStyle().WindowPadding.x, -padding + ImGui::GetStyle().WindowPadding.y), true, flags);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));

        ImGuiStyle& style = ImGui::GetStyle();
        float basePadding = style.WindowPadding.x;
        float bigPadding = basePadding * 3.0f;

        float contentWidth = ImGui::GetContentRegionAvail().x;

        ImGui::PushFont(NULL, ImGui::GetFontSize() * 0.9f);
        ImGuiStd::TextShadow(u8"编辑模式");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::NewLine();

        ImGui::PushTextWrapPos(contentWidth - bigPadding);
        ImGuiStd::TextShadow(u8"在此模式中，你可以调整所有叠加层的位置：");
        ImGui::NewLine();
        ImGuiStd::TextShadow(u8"- 右键元素打开菜单选项");
        ImGuiStd::TextShadow(u8"- 双击水印图片切换显示/隐藏");
        ImGuiStd::TextShadow(u8"- 拖拽图片文件到窗口添加水印");
        ImGuiStd::TextShadow(u8"- 拖拽模块GUI重新定位");
        ImGuiStd::TextShadow(u8"- 滚轮调整水印大小");
        ImGui::NewLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGuiStd::TextShadow(u8"提示：按 Esc 退出编辑模式");
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        ImGui::NewLine();
        ImGui::SetCursorPosX((contentWidth - 200.0f) * 0.5f);
        if (ImGui::Button(u8"进入编辑模式", ImVec2(200.0f, 40.0f)))
        {
            EnterEditMode();
        }

        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    static void EnterEditMode();
};
