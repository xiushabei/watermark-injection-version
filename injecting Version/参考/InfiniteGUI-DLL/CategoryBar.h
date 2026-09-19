#pragma once
#include <vector>

#include "ImGuiStd.h"
#include "MyButton.hpp"
class CategoryBar {
public:
    CategoryBar()
    {
        buttons.emplace_back(u8"全部", buttonSize);
        buttons.emplace_back(u8"窗口", buttonSize);
        buttons.emplace_back(u8"视觉", buttonSize);
        buttons.emplace_back(u8"工具", buttonSize);
        buttons.emplace_back(u8"服务器", buttonSize);
    }

    bool Draw()
    {
        ImVec2 curPos = ImGui::GetCursorPos();
        int clickedOne = false;
        for (int i = 0; i < buttons.size(); i++)
        {
            MyButton& btn = buttons[i];

            // 设置按钮的“是否选中”
            btn.SetSelected(i == selectedButtonIndex);

            // 绘制按钮

            ImGui::SetCursorPos(curPos);
            bool clicked = btn.Draw();
            if (clicked)
            {
                selectedButtonIndex = i;
                clickedOne = true;
            }
            curPos.x += buttonSize.x + padding * 0.625f;
        }
        curPos.y += 1.0f;
        ImGui::SetCursorPos(curPos);

        float windowWith = ImGui::GetWindowWidth();

        if (filter.Draw(u8"##filter", windowWith - curPos.x - padding, u8" 搜索"))
        {
            return true;
        }

        if (!filter.IsActive())
        {
            curPos.y += 5.0f;
            ImGui::SetCursorPos(ImVec2(windowWith - padding - 25.0f, curPos.y));
            ImGui::BeginDisabled();
            ImGui::PushFont(opengl_hook::gui.iconFont, 18.0f);

            ImGuiStd::TextColoredShadow(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), u8"\uE041");

            ImGui::PopFont();

            ImGui::EndDisabled();
        }

        return clickedOne;   // -1 表示没有点击
    }

    int GetSelectedIndex() const { return selectedButtonIndex; }
    void SetSelectedIndex(int index) { selectedButtonIndex = index; }
    float GetButtonHeight() const { return buttonSize.y; }

    ImGuiTextFilter *GetFilter() { return &filter; }

private:
    std::vector<MyButton> buttons;
    ImVec2 buttonSize = ImVec2(80, 30);
    int selectedButtonIndex = 0;
    ImGuiTextFilter filter;
};