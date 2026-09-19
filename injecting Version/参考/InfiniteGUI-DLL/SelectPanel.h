#pragma once
#include <vector>

#include "PanelButton.hpp"

class SelectPanel {
public:
    SelectPanel()
    {
        buttons.emplace_back("R", u8"模组");
        buttons.emplace_back("~", u8"设置");
        buttons.emplace_back("}", u8"编辑");
        buttons.emplace_back(u8"\uE009", u8"关于");
        buttons.emplace_back("=", u8"退出", true);
    }

    bool Draw()
    {
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        bool clicked = false;
        for (int i = 0; i < buttons.size(); i++)
        {
            PanelButton& btn = buttons[i];

            // 设置按钮的“是否选中”
            btn.SetSelected(i == selectedButtonIndex);
            // 绘制按钮
            if (btn.Draw())
            {
                selectedButtonIndex = i;
                clicked = true;
            }
        }
        return clicked;   // -1 表示没有点击
    }

    int GetSelectedIndex() const { return selectedButtonIndex; }
    void SetSelectedIndex(int index)
    {
        selectedButtonIndex = index;
    }

private:
    std::vector<PanelButton> buttons;
    int selectedButtonIndex = 0;
};