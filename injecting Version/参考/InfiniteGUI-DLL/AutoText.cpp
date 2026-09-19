#include "AutoText.h"

#include "AudioManager.h"
#include "GameStateDetector.h"
#include "NotificationItem.h"

void AutoText::Toggle()
{
}

void AutoText::Update()
{
    if (!GameStateDetector::Instance().IsInGameWindow() || !GameStateDetector::Instance().IsInGame()) return;
    for (auto& text : texts)
    {
        if(!text.isEnabled) continue;
        if (text.Update(keyStateHelper, customGameKeybinds ? chatKeybind : GameKeyBind::Instance().GetVK(GameAction::Chat), mode))
        {
            AudioManager::Instance().playSound("menu\\pop.wav", soundVolume);
            std::string message = u8"自动消息：已发送。\n\"" + text.GetText() + u8"\"";
            NotificationItem::Instance().AddNotification(NotificationType_Info, message);
        }
    }
}

void AutoText::DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth)
{
    for (int i = 0; i < texts.size(); i++)
    {
        ImGui::PushID(i);
        AutoSingleText& text = texts[i];
        if (ImGui::CollapsingHeader(text.GetText().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            text.DrawSettings(bigPadding, centerX, itemWidth, i);
            //居中显示
            ImGui::SetCursorPosX(bigPadding);
            std::string deleteStr = u8"删除";
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.1f, 0.1f, 1.0f)); //红色警示
            if (ImGui::Button(deleteStr.c_str(), ImVec2(centerX * 2 - bigPadding, 0)))
            {
                texts.erase(texts.begin() + i);
            }
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        ImGui::PopID();
    }
    ImGui::SetCursorPosX(bigPadding);
    if (ImGui::Button("+", ImVec2(centerX * 2 - bigPadding, 0)))
    {
        texts.emplace_back();
    }
    ImGui::PushFont(NULL, ImGui::GetFontSize() * 0.8f);
    ImGui::BeginDisabled();
    ImGuiStd::TextShadow(u8"其它设置");
    ImGui::EndDisabled();
    ImGui::PopFont();

    ImGui::SetCursorPosX(bigPadding);
    ImGui::SetNextItemWidth(itemWidth);
    const char* inputModeNames[] = {
    u8"新(1.19+)",
    u8"旧(无法输入中文)",
    };

    ImGui::Combo(u8"输入模式", &mode, inputModeNames, IM_ARRAYSIZE(inputModeNames));

    if(mode == Modern)
    {
        ImGui::SetCursorPosX(bigPadding);
        ImGui::SetNextItemWidth(itemWidth);
        ImGui::Checkbox(u8"自定义聊天栏键", &customGameKeybinds);
        if(customGameKeybinds)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(bigPadding + centerX);
            ImGui::SetNextItemWidth(itemWidth);
            ImGuiStd::Keybind(u8"聊天栏键：", chatKeybind);
        }
    }
    DrawSoundSettings(bigPadding, centerX, itemWidth);
}

void AutoText::Load(const nlohmann::json& j)
{
    LoadItem(j);
    LoadSound(j);
    texts.clear();
    if(j.contains("customGameKeybinds")) customGameKeybinds = j["customGameKeybinds"].get<bool>();
    if(j.contains("chatKeybind")) chatKeybind = j["chatKeybind"].get<int>();
    if(j.contains("mode")) mode = j["mode"].get<int>();
    if (!j.contains("texts") || !j["texts"].is_array())
        return;

    for (const auto& tj : j["texts"])
    {
        AutoSingleText text;
        text.Load(tj);
        texts.push_back(std::move(text));
    }
}

void AutoText::Save(nlohmann::json& j) const
{
    SaveItem(j);
    SaveSound(j);
    j["customGameKeybinds"] = customGameKeybinds;
    j["chatKeybind"] = chatKeybind;
    j["mode"] = mode;
    j["texts"] = nlohmann::json::array();
    for (const auto& text : texts)
    {
        nlohmann::json tj;
        text.Save(tj);
        j["texts"].push_back(tj);
    }
}
