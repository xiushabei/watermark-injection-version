#pragma once
#include <string>

#include "Item.h"
#include "KeyState.h"
#include "SoundModule.h"
#include "UpdateModule.h"
#include "StringConverter.h"
#include "KeybindModule.h"
#include <thread>

#include "GameKeyBind.h"
#include "GameStateDetector.h"
//struct ClipboardData
//{
//    UINT format;
//    HANDLE hData;
//};
enum AutoSendMode
{
    Modern,
    Old
};


struct AutoSingleText
{
    std::string text;
    bool isEnabled = true;
    int keybind = NULL;
    void Save(nlohmann::json& j) const
    {
        j["text"] = text;
        j["isEnabled"] = isEnabled;
        j["keybind"] = keybind;
    }
    void Load(const nlohmann::json& j)
    {
        if (j.contains("text")) text = j["text"].get<std::string>();
        if (j.contains("isEnabled")) isEnabled = j["isEnabled"].get<bool>();
        if (j.contains("keybind")) keybind = j["keybind"].get<int>();
    }

    std::string GetText()
    {
        return text;
    }
    void DrawSettings(float bigPadding, float centerX, float itemWidth, int id)
    {
        float bigItemWidth = centerX * 2.0f - bigPadding * 4.0f;
        ImGui::SetCursorPosX(bigPadding);
        ImGui::SetNextItemWidth(itemWidth);
        ImGui::Checkbox(u8"启用", &isEnabled);
        ImGui::SameLine();
        ImGui::SetCursorPosX(bigPadding + centerX);
        ImGui::SetNextItemWidth(itemWidth);
        std::string keybindStr = u8"快捷键(" + std::to_string(id) + ")";
        ImGuiStd::Keybind(keybindStr.c_str(), keybind);
        ImGui::SetCursorPosX(bigPadding);
        ImGui::SetNextItemWidth(bigItemWidth);
        ImGuiStd::InputTextStd(u8"文本内容", text);
        ImGui::SetCursorPosX(bigPadding);
    }
    static bool SimulatedInputText(const std::wstring& text)
    {
        // 记录修饰键状态
        bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        auto SendKey = [](WORD vk, DWORD flags)
            {
                INPUT input{};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = vk;
                input.ki.dwFlags = flags;
                SendInput(1, &input, sizeof(INPUT));
            };

        // 先抬起 Shift / Ctrl（如果按着）
        if (shiftDown)
            SendKey(VK_SHIFT, KEYEVENTF_KEYUP);

        if (ctrlDown)
            SendKey(VK_CONTROL, KEYEVENTF_KEYUP);

        if(altDown)
            SendKey(VK_MENU, KEYEVENTF_KEYUP);

        // ===== 剪贴板 =====

        if (!OpenClipboard(nullptr))
            return false;

        EmptyClipboard();

        //Unicode 剪贴板要用 wchar_t
        size_t sizeInBytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeInBytes);
        if (!hMem)
        {
            CloseClipboard();
            return false;
        }

        wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
        if (!pMem)
        {
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }

        memcpy(pMem, text.c_str(), sizeInBytes);
        GlobalUnlock(hMem);

        //CF_UNICODETEXT
        SetClipboardData(CF_UNICODETEXT, hMem);
        CloseClipboard();

        // 模拟 Ctrl + V
        INPUT inputs[4]{};

        inputs[0] = { INPUT_KEYBOARD };
        inputs[0].ki.wVk = VK_CONTROL;

        inputs[1] = { INPUT_KEYBOARD };
        inputs[1].ki.wVk = 'V';

        inputs[2] = inputs[1];
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

        inputs[3] = inputs[0];
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(4, inputs, sizeof(INPUT));

        // ===== 恢复修饰键 =====
        if (ctrlDown)
            SendKey(VK_CONTROL, 0);

        if (shiftDown)
            SendKey(VK_SHIFT, 0);

        if (altDown)
            SendKey(VK_MENU, 0);

        return true;
    }

    // 模拟输入字符串
    static void SendString(const std::string& text) {
        // 记录修饰键状态
        bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        auto SendKey = [](WORD vk, DWORD flags)
            {
                INPUT input{};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = vk;
                input.ki.dwFlags = flags;
                SendInput(1, &input, sizeof(INPUT));
            };

        // 先抬起 Shift / Ctrl（如果按着）
        if (shiftDown)
            SendKey(VK_SHIFT, KEYEVENTF_KEYUP);

        if (ctrlDown)
            SendKey(VK_CONTROL, KEYEVENTF_KEYUP);

        if (altDown)
            SendKey(VK_MENU, KEYEVENTF_KEYUP);

        for (char c : text) {
            SHORT vk = VkKeyScanA(c);  // 获取虚拟键码
            KEYBDINPUT kb = { 0 };

            // 按键
            kb.wVk = LOBYTE(vk);
            kb.wScan = MapVirtualKeyA(kb.wVk, 0);
            kb.dwFlags = 0;
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki = kb;
            SendInput(1, &input, sizeof(INPUT));

            // 松键
            kb.dwFlags = KEYEVENTF_KEYUP;
            input.ki = kb;
            SendInput(1, &input, sizeof(INPUT));
        }
        // ===== 恢复修饰键 =====
        if (ctrlDown)
            SendKey(VK_CONTROL, 0);

        if (shiftDown)
            SendKey(VK_SHIFT, 0);

        if (altDown)
            SendKey(VK_MENU, 0);

    }

    // 模拟按下并释放 Enter
    static void SendEnter() {
        SHORT vk = VK_RETURN;  // 获取虚拟键码
        KEYBDINPUT kb = { 0 };

        // 按键
        kb.wVk = LOBYTE(vk);
        kb.wScan = MapVirtualKeyA(kb.wVk, 0);
        kb.dwFlags = 0;
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki = kb;
        SendInput(1, &input, sizeof(INPUT));

        // 松键
        kb.dwFlags = KEYEVENTF_KEYUP;
        input.ki = kb;
        SendInput(1, &input, sizeof(INPUT));
    }

    bool Update(KeyState& keyStateHelper, int key, int mode) const
    {
        if (keyStateHelper.GetKeyClick(keybind))
        {
            switch (mode)
            {
            case Modern:
                std::thread([text = this->text, thisKey = key]() {
                    KeyState::SetKeyClick(thisKey, 0, 2);
                    while (GameStateDetector::Instance().IsInGame())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    SimulatedInputText(StringConverter::Utf8ToWstring(text));
                    SendEnter();
                    }).detach();
                break;
            default:
            case Old:
                std::thread([text = this->text]() {
                    SendString("T");
                    while (GameStateDetector::Instance().IsInGame())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    SendString(text);
                    SendEnter();
                    }).detach();
                break;
            }

            return true;
        }
        return false;
    }
};

class AutoText : public Item, public UpdateModule, public SoundModule
{
public:
	AutoText()
	{
        type = Util;
        name = u8"自动消息";
        description = u8"按下快捷键自动发送指定文本";
        icon = "6";
        updateIntervalMs = 50;
        lastUpdateTime = std::chrono::steady_clock::now();
        AutoText::Reset();
	}
    static AutoText& Instance() {
        static AutoText instance;
        return instance;
    }
    void Toggle() override;
    void Reset() override
    {
        ResetSound();
        isEnabled = false; // 是否启用
        chatKeybind = GameKeyBind::Instance().GetVK(GameAction::Chat);
        mode = Modern;
        texts.clear();
        //texts.emplace_back();
    }

    void Update() override;
    void DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth) override;
    void Load(const nlohmann::json& j) override;
    void Save(nlohmann::json& j) const override;

private:
    std::vector<AutoSingleText> texts;
    KeyState keyStateHelper;
    int chatKeybind;
    int mode;
    bool customGameKeybinds = false;
};