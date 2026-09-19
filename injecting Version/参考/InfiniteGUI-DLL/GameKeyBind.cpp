#include "GameKeyBind.h"

#include "FileUtils.h"
#include "ImGuiStd.h"

void GameKeyBind::ApplyDefaultKeybinds()
{
    for (const auto& [action, vk] : DEFAULT_KEYBINDS)
    {
        // 如果不存在 或 vk == 0，才补默认
        if (keys.find(action) == keys.end() || keys[action].vk == 0)
        {
            keys[action].vk = vk;
        }
    }
}

void GameKeyBind::Load(const std::filesystem::path& optionsPath)
{
    keys.clear();
    loadSuccess = false;

    // ---------- options.txt 不存在 ----------
    if (!std::filesystem::exists(optionsPath))
    {
        ApplyDefaultKeybinds();
        return;
    }

    std::ifstream file(optionsPath);
    if (!file.is_open())
    {
        ApplyDefaultKeybinds();
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        auto pos = line.find(':');
        if (pos == std::string::npos)
            continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        auto itAction = ACTION_MAP.find(key);
        if (itAction == ACTION_MAP.end())
            continue;

        int vk = ParseValueToVK(value);
        keys[itAction->second].vk = vk;
    }

    // ---------- 补默认 ----------
    ApplyDefaultKeybinds();

    loadSuccess = true;
}

void GameKeyBind::DrawKeyBindUI()
{
    if (!ImGui::Begin(u8"Minecraft 按键绑定"))
    {
        ImGui::End();
        return;
    }
    std::string text;
    if(loadSuccess)
        text = u8"从 " + FileUtils::optionsPath + u8" 中解析的快捷键绑定";
    else
        text = u8"从 " + FileUtils::optionsPath + u8" 中解析快捷键绑定失败";
    ImGui::Text(text.c_str());
    ImGui::Separator();

    if (ImGui::BeginTable("##KeyBindTable", 2,
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Key");
        ImGui::TableHeadersRow();

        for (int i = 0; i <= static_cast<int>(GameAction::Hotbar9); ++i)
        {
            GameAction action = static_cast<GameAction>(i);
            int vk = GetVK(action);
            int oldVk = vk;
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            //ImGui::TextUnformatted(GameActionToString(action));
            ImGuiStd::Keybind(GameActionToString(action), vk);
            if(oldVk != vk) keys[action].vk = vk;
            ImGui::TableSetColumnIndex(1);

            std::string keyName = VKToString(vk);
            if (vk == 0)
                ImGui::TextDisabled("%s", keyName.c_str());
            else
                if (GetAsyncKeyState(vk) & 0x8000)
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1.f), "%s", keyName.c_str());
                else
                    ImGui::Text("%s", keyName.c_str());

        }

        ImGui::EndTable();
    }

    ImGui::End();
}

int GameKeyBind::ParseValueToVK(const std::string& value)
{
    if (value.empty())
        return 0;

    // ---------- 新版：字符串 ----------
    if (value.rfind("key.", 0) == 0)
    {
        return ParseStringKey(value);
    }

    // ---------- 旧版：数字 ----------
    return ParseLegacyKey(value);
}

int GameKeyBind::ParseStringKey(const std::string& value)
{
    // key.keyboard.xxx / key.mouse.xxx
    if (value == "key.keyboard.unknown")
        return 0;

    // 键盘
    if (value.rfind("key.keyboard.", 0) == 0)
    {
        std::string name = value.substr(strlen("key.keyboard."));

        // 单字符（a-z / 0-9）
        if (name.size() == 1)
        {
            char c = name[0];
            if (c >= 'a' && c <= 'z')
                return toupper(c);
            if (c >= '0' && c <= '9')
                return c;
        }

        auto it = STRING_TO_VK.find(name);
        return it != STRING_TO_VK.end() ? it->second : 0;
    }

    // 鼠标
    if (value.rfind("key.mouse.", 0) == 0)
    {
        std::string btn = value.substr(strlen("key.mouse."));
        auto it = STRING_Mouse_TO_VK.find(btn);
        return it != STRING_Mouse_TO_VK.end() ? it->second : 0;
    }

    return 0;
}

int GameKeyBind::ParseLegacyKey(const std::string& value)
{
    int code = 0;
    try {
        code = std::stoi(value);
    }
    catch (...) {
        return 0;
    }

    // 鼠标
    if (code >= -100 && code < -90)
        return MouseCodeToVK(code);

    // 键盘
    auto it = LWJGL_TO_VK.find(code);
    return it != LWJGL_TO_VK.end() ? it->second : 0;
}
