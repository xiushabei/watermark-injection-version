#pragma once
#include "nlohmann/json.hpp"
#include <string>

#include "UpdateModule.h"
#include "imgui/imgui.h"
#include "opengl_hook.h"
#include "WindowModule.h"

struct notification_element {
    ImVec2 pos;
};

enum NotificationType
{
    NotificationType_Info,
    NotificationType_Success,
    NotificationType_Warning
};

enum NotificationState
{
    Joining,
    Staying,
    Leaving
};

constexpr float windowPadding = 15.0f;
static int uniqueId = 0;
class Notification : public RenderModule
{
public:
    Notification(NotificationType type, const std::string& message, int placeIndex, const int durationMs) {
        Reset();
        this->message = message;
        this->type = type;
        SetTitleByType(type);
        this->placeIndex = placeIndex;
        this->durationMs = durationMs;
        size = ImVec2(300, 100.0f);
        ImVec2 maxScreenSize = ImVec2((float)opengl_hook::screen_size.x, (float)opengl_hook::screen_size.y);
        startElement.pos = ImVec2(maxScreenSize.x + windowPadding, maxScreenSize.y - (placeIndex + 1.5f) * (size.y + windowPadding));
        curElement = startElement;
        targetElement.pos = ImVec2(maxScreenSize.x - size.x - windowPadding, startElement.pos.y);
        joinTime = std::chrono::steady_clock::now();
    }

    void Toggle();
    void Reset()
    {
    }
    void RenderGui() override;
    void RenderBeforeGui() override;
    void RenderAfterGui() override;
    bool Done() const;
    void SetMaxElementPos();
    void SetPlaceIndex(int index);
    bool ShouldLeave();
private:

    void SetTitleByType(const NotificationType& type)
    {
        switch (type)
        {
        case NotificationType_Info:
            icon = u8"\uE009";
            title = u8"提示";
            break;
        case NotificationType_Success:
            icon = "S";
            title = u8"成功";
            break;
        case NotificationType_Warning:
            icon = u8"\uE063";
            title = u8"警告";
            break;
        }
    }

    bool done = false;
    int placeIndex = 0;
    int id = uniqueId++;
    std::string message;
    std::string icon;
    std::string title;
    notification_element startElement;
    notification_element targetElement;
    notification_element curElement;
    NotificationState state = Joining;
    NotificationType type = NotificationType_Info;
    ImVec2 size = ImVec2(0, 0);
    std::chrono::steady_clock::time_point joinTime;  // 记录时间
    int durationMs = 3000;
    float duration = 0.0f;

};