#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <windows.h>
#include "imgui\imgui.h"
#include "VK_Keymap.h"
struct GameKey
{
    int vk = 0; // 0 == NULL / 未绑定
};

static std::unordered_map<int, int> LWJGL_TO_VK = {
    // 字母
    {17, 'W'},
    {30, 'A'},
    {31, 'S'},
    {32, 'D'},
    {18, 'E'},
    {16, 'Q'},
    {20, 'T'},
    {21, 'Y'},
    {22, 'U'},
    {23, 'I'},
    {24, 'O'},
    {25, 'P'},
    {44, 'Z'},
    {45, 'X'},
    {46, 'C'},
    {47, 'V'},
    {48, 'B'},
    {49, 'N'},
    {50, 'M'},
    {35, 'H'},
    {34, 'G'},
    {33, 'F'},
    {36, 'J'},
    {37, 'K'},
    {38, 'L'},
    {19, 'R'},

    // 数字
    {2, '1'}, {3, '2'}, {4, '3'}, {5, '4'}, {6, '5'},
    {7, '6'}, {8, '7'}, {9, '8'}, {10, '9'}, {11, '0'},

    // 功能
    {57, VK_SPACE},
    {28, VK_RETURN},
    {15, VK_TAB},
    {1, VK_ESCAPE},
    {14, VK_BACK},

    // 修饰键
    {42, VK_LSHIFT},
    {54, VK_RSHIFT},
    {29, VK_LCONTROL},
    {157, VK_RCONTROL},
    {56, VK_LMENU},
    {184, VK_RMENU},

    // 方向
    {200, VK_UP},
    {208, VK_DOWN},
    {203, VK_LEFT},
    {205, VK_RIGHT},

    // 功能键
    {59, VK_F1}, {60, VK_F2}, {61, VK_F3}, {62, VK_F4},
    {63, VK_F5}, {64, VK_F6}, {65, VK_F7}, {66, VK_F8},
    {67, VK_F9}, {68, VK_F10}, {87, VK_F11}, {88, VK_F12},

    // ---------- 标点 / OEM ----------
    {12, VK_OEM_MINUS},    // KEY_MINUS        - _
    {13, VK_OEM_PLUS},     // KEY_EQUALS       = +
    {26, VK_OEM_4},        // KEY_LBRACKET     [ {
    {27, VK_OEM_6},        // KEY_RBRACKET     ] }
    {39, VK_OEM_1},        // KEY_SEMICOLON    ; :
    {40, VK_OEM_7},        // KEY_APOSTROPHE   ' "
    {41, VK_OEM_3},        // KEY_GRAVE        ` ~
    {43, VK_OEM_5},        // KEY_BACKSLASH    \ |
    {51, VK_OEM_COMMA},    // KEY_COMMA        , <
    {52, VK_OEM_PERIOD},  // KEY_PERIOD       . >
    {53, VK_OEM_2},        // KEY_SLASH        / ?

    //小键盘运算符
    {181, VK_DIVIDE},    // KEY_DIVIDE    /
    {55,  VK_MULTIPLY},  // KEY_MULTIPLY  *
    {74,  VK_SUBTRACT},  // KEY_SUBTRACT  -
    {78,  VK_ADD},       // KEY_ADD       +
    {83,  VK_DECIMAL},   // KEY_DECIMAL   .
    {156, VK_RETURN},   // KEY_NUMPADENTER
    // ---------- 小键盘 ----------
    {210, VK_NUMPAD0},
    {211, VK_NUMPAD1},
    {212, VK_NUMPAD2},
    {213, VK_NUMPAD3},
    {214, VK_NUMPAD4},
    {215, VK_NUMPAD5},
    {216, VK_NUMPAD6},
    {217, VK_NUMPAD7},
    {218, VK_NUMPAD8},
    {219, VK_NUMPAD9},

}; //wormwaker

static int MouseCodeToVK(int code)
{
    // ---------- 鼠标 ----------
    if (code == -100) return VK_LBUTTON;
    if (code == -99)  return VK_RBUTTON;
    if (code == -98)  return VK_MBUTTON;
    if (code == -97)  return VK_XBUTTON1;
    if (code == -96)  return VK_XBUTTON2;
    return 0;
}

static std::unordered_map<std::string, int> STRING_TO_VK = {
    {"space", VK_SPACE},
    {"tab", VK_TAB},
    {"enter", VK_RETURN},
    {"escape", VK_ESCAPE},
    {"backspace", VK_BACK},

    {"left.shift", VK_LSHIFT},
    {"right.shift", VK_RSHIFT},
    {"left.control", VK_LCONTROL},
    {"right.control", VK_RCONTROL},
    {"left.alt", VK_LMENU},
    {"right.alt", VK_RMENU},
    {"left.win", VK_LWIN},
    {"right.win", VK_RWIN},
    {"left.bracket", VK_OEM_4},
    {"right.bracket", VK_OEM_6},
    {"bracket.left", VK_OEM_4},
    {"bracket.right", VK_OEM_6},

    {"insert", VK_INSERT},
    {"delete", VK_DELETE},
    {"home", VK_HOME},
    {"end", VK_END},
    {"page.up", VK_PRIOR},
    {"page.down", VK_NEXT},
    {"pause", VK_PAUSE},
    {"scroll.lock", VK_SCROLL},
    {"print.screen", VK_SNAPSHOT},
    //小键盘
    {"keypad.add", VK_ADD},
    {"keypad.divide", VK_DIVIDE},
    {"keypad.multiply", VK_MULTIPLY},
    {"keypad.decimal", VK_DECIMAL},
    {"keypad.subtract", VK_SUBTRACT},
    {"keypad.enter", VK_RETURN},
    {"keypad.0", VK_NUMPAD0},
    {"keypad.1", VK_NUMPAD1},
    {"keypad.2", VK_NUMPAD2},
    {"keypad.3", VK_NUMPAD3},
    {"keypad.4", VK_NUMPAD4},
    {"keypad.5", VK_NUMPAD5},
    {"keypad.6", VK_NUMPAD6},
    {"keypad.7", VK_NUMPAD7},
    {"keypad.8", VK_NUMPAD8},
    {"keypad.9", VK_NUMPAD9},

    {"grave.accent", VK_OEM_3},
    {"slash", VK_OEM_2},
    {"backslash", VK_OEM_5},

    {"up", VK_UP},
    {"down", VK_DOWN},
    {"left", VK_LEFT},
    {"right", VK_RIGHT},

    //标点
    {"comma", VK_OEM_COMMA},
    {"period", VK_OEM_PERIOD},
    {"semicolon", VK_OEM_1},
    {"apostrophe", VK_OEM_7},
    {"grave.accent", VK_OEM_3},
    {"tilde", VK_OEM_3}, // 可保留作为 alias
    {"equals", VK_OEM_PLUS},
    {"minus", VK_OEM_MINUS},
    {"backslash", VK_OEM_5},
    {"quote.left", VK_OEM_7},
    {"quote.right", VK_OEM_7},
// 功能键
    {"f1", VK_F1}, {"f2", VK_F2}, {"f3", VK_F3}, {"f4", VK_F4},
    {"f5", VK_F5}, {"f6", VK_F6}, {"f7", VK_F7}, {"f8", VK_F8},
    {"f9", VK_F9}, {"f10", VK_F10}, {"f11", VK_F11}, {"f12", VK_F12},
    {"f13", VK_F13}, {"f14", VK_F14}, {"f15", VK_F15}, {"f16", VK_F16},
    {"f17", VK_F17}, {"f18", VK_F18}, {"f19", VK_F19}, {"f20", VK_F20},
    {"f21", VK_F21}, {"f22", VK_F22}, {"f23", VK_F23}, {"f24", VK_F24},
    {"numlock", VK_NUMLOCK},
    {"capslock", VK_CAPITAL},
    {"scrolllock", VK_SCROLL},
    {"printscreen", VK_SNAPSHOT},
    {"apps", VK_APPS},
    {"sleep", VK_SLEEP},

};

static std::unordered_map<std::string, BYTE> STRING_Mouse_TO_VK = {
    {"left", VK_LBUTTON},
    {"right", VK_RBUTTON},
    {"middle", VK_MBUTTON},
    {"4", VK_XBUTTON1},
    {"5", VK_XBUTTON2},
};

enum class GameAction
{
    Attack,
    Use,
    Forward,
    Left,
    Back,
    Right,
    Jump,
    Sneak,
    Sprint,
    Drop,
    Inventory,
    Chat,
    PlayerList,
    PickItem,
    Command,
    Screenshot,
    TogglePerspective,
    Fullscreen,
    Hotbar1,
    Hotbar2,
    Hotbar3,
    Hotbar4,
    Hotbar5,
    Hotbar6,
    Hotbar7,
    Hotbar8,
    Hotbar9,
};

static std::unordered_map<std::string, GameAction> ACTION_MAP = {
    {"key_key.attack", GameAction::Attack},
    {"key_key.use", GameAction::Use},
    {"key_key.forward", GameAction::Forward},
    {"key_key.left", GameAction::Left},
    {"key_key.back", GameAction::Back},
    {"key_key.right", GameAction::Right},
    {"key_key.jump", GameAction::Jump},
    {"key_key.sneak", GameAction::Sneak},
    {"key_key.sprint", GameAction::Sprint},
    {"key_key.drop", GameAction::Drop},
    {"key_key.inventory", GameAction::Inventory},
    {"key_key.chat", GameAction::Chat},
    {"key_key.playerlist", GameAction::PlayerList},
    {"key_key.pickItem", GameAction::PickItem},
    {"key_key.command", GameAction::Command},
    {"key_key.screenshot", GameAction::Screenshot},
    {"key_key.togglePerspective", GameAction::TogglePerspective},
    {"key_key.fullscreen", GameAction::Fullscreen},
    {"key_key.hotbar.1", GameAction::Hotbar1},
    {"key_key.hotbar.2", GameAction::Hotbar2},
    {"key_key.hotbar.3", GameAction::Hotbar3},
    {"key_key.hotbar.4", GameAction::Hotbar4},
    {"key_key.hotbar.5", GameAction::Hotbar5},
    {"key_key.hotbar.6", GameAction::Hotbar6},
    {"key_key.hotbar.7", GameAction::Hotbar7},
    {"key_key.hotbar.8", GameAction::Hotbar8},
    {"key_key.hotbar.9", GameAction::Hotbar9},
};

static std::string VKToString(int vk)
{
    //if (vk == 0)
    //    return "Unbound";

    //// 鼠标
    //switch (vk)
    //{
    //case VK_LBUTTON: return "Mouse Left";
    //case VK_RBUTTON: return "Mouse Right";
    //case VK_MBUTTON: return "Mouse Middle";
    //case VK_XBUTTON1: return "Mouse X1";
    //case VK_XBUTTON2: return "Mouse X2";
    //}

    //// 字母 / 数字
    //if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    //{
    //    return std::string(1, static_cast<char>(vk));
    //}

    //// 功能键
    //if (vk >= VK_F1 && vk <= VK_F24)
    //{
    //    return "F" + std::to_string(vk - VK_F1 + 1);
    //}

    //// 其他常用
    //switch (vk)
    //{
    //case VK_SPACE: return "Space";
    //case VK_TAB: return "Tab";
    //case VK_RETURN: return "Enter";
    //case VK_ESCAPE: return "Esc";
    //case VK_BACK: return "Backspace";

    //case VK_LSHIFT: return "LShift";
    //case VK_RSHIFT: return "RShift";
    //case VK_LCONTROL: return "LCtrl";
    //case VK_RCONTROL: return "RCtrl";
    //case VK_LMENU: return "LAlt";
    //case VK_RMENU: return "RAlt";

    //case VK_UP: return "Up";
    //case VK_DOWN: return "Down";
    //case VK_LEFT: return "Left";
    //case VK_RIGHT: return "Right";

    //case VK_INSERT: return "Insert";
    //case VK_DELETE: return "Delete";
    //case VK_HOME: return "Home";
    //case VK_END: return "End";
    //case VK_PRIOR: return "PageUp";
    //case VK_NEXT: return "PageDown";
    //}

    //// 兜底
    //char buf[32];
    //sprintf_s(buf, "VK_%02X", vk);
    //return buf;
    return keys[vk];
}

static const char* GameActionToString(GameAction action)
{
    switch (action)
    {
    case GameAction::Attack: return "Attack";
    case GameAction::Use: return "Use";
    case GameAction::Forward: return "Forward";
    case GameAction::Left: return "Left";
    case GameAction::Back: return "Back";
    case GameAction::Right: return "Right";
    case GameAction::Jump: return "Jump";
    case GameAction::Sneak: return "Sneak";
    case GameAction::Sprint: return "Sprint";
    case GameAction::Drop: return "Drop";
    case GameAction::Inventory: return "Inventory";
    case GameAction::Chat: return "Chat";
    case GameAction::PlayerList: return "Player List";
    case GameAction::PickItem: return "Pick Item";
    case GameAction::Command: return "Command";
    case GameAction::Screenshot: return "Screenshot";
    case GameAction::TogglePerspective: return "Toggle Perspective";
    case GameAction::Fullscreen: return "Fullscreen";
    case GameAction::Hotbar1: return "Hotbar 1";
    case GameAction::Hotbar2: return "Hotbar 2";
    case GameAction::Hotbar3: return "Hotbar 3";
    case GameAction::Hotbar4: return "Hotbar 4";
    case GameAction::Hotbar5: return "Hotbar 5";
    case GameAction::Hotbar6: return "Hotbar 6";
    case GameAction::Hotbar7: return "Hotbar 7";
    case GameAction::Hotbar8: return "Hotbar 8";
    case GameAction::Hotbar9: return "Hotbar 9";
    default: return "Unknown";
    }
}

static std::unordered_map<GameAction, int> DEFAULT_KEYBINDS = {
    {GameAction::Attack, VK_LBUTTON},
    {GameAction::Use, VK_RBUTTON},

    {GameAction::Forward, 'W'},
    {GameAction::Left, 'A'},
    {GameAction::Back, 'S'},
    {GameAction::Right, 'D'},

    {GameAction::Jump, VK_SPACE},
    {GameAction::Sneak, VK_LSHIFT},
    {GameAction::Sprint, VK_LCONTROL},

    {GameAction::Drop, 'Q'},
    {GameAction::Inventory, 'E'},
    {GameAction::Chat, 'T'},
    {GameAction::PlayerList, VK_TAB},

    {GameAction::PickItem, VK_MBUTTON},
    {GameAction::Command, VK_OEM_2},   // /
    {GameAction::Screenshot, VK_F2},
    {GameAction::TogglePerspective, 'V'},
    {GameAction::Fullscreen, VK_F11},

    {GameAction::Hotbar1, '1'},
    {GameAction::Hotbar2, '2'},
    {GameAction::Hotbar3, '3'},
    {GameAction::Hotbar4, '4'},
    {GameAction::Hotbar5, '5'},
    {GameAction::Hotbar6, '6'},
    {GameAction::Hotbar7, '7'},
    {GameAction::Hotbar8, '8'},
    {GameAction::Hotbar9, '9'},
};

class GameKeyBind
{
public:
    static GameKeyBind& Instance() {
        static GameKeyBind instance;
        return instance;
    }


    void Load(const std::filesystem::path& optionsPath);
    void DrawKeyBindUI();

    int GetVK(GameAction action) const
    {
        auto it = keys.find(action);
        return it != keys.end() ? it->second.vk : 0;
    }
    bool IsSuccess() const { return loadSuccess; }
private:
    std::unordered_map<GameAction, GameKey> keys;
    bool loadSuccess = false;

private:
    void ApplyDefaultKeybinds();

    static int ParseValueToVK(const std::string& value);

    // ================= 新版 =================
    static int ParseStringKey(const std::string& value);

    // ================= 旧版 =================
    static int ParseLegacyKey(const std::string& value);
};