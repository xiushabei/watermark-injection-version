#pragma once
#include <deque>
#include "Item.h"
#include "nlohmann/json.hpp"
#include "Notification.h"

class NotificationItem : public Item, public RenderModule, public UpdateModule, public WindowStyleModule
{
public:
    NotificationItem() {
        type = Hud;
        name = u8"提示弹窗";
        description = u8"在右下角弹出提示信息窗";
        icon = "6";
        updateIntervalMs = 50;
        lastUpdateTime = std::chrono::steady_clock::now();
        NotificationItem::Reset();
    }

    static NotificationItem& Instance() {
        static NotificationItem instance;
        return instance;
    }
    void AddNotification(NotificationType type, const std::string& message, int durationMs = 3000);
    void PopNotification();
    void Update() override;
    void Toggle() override;
    void Reset() override
    {
        isEnabled = true;
        childBg = itemStyle.bgColor;
        //notifications.clear();
        ResetWindowStyle();
    }
    void RenderGui() override;
    void RenderBeforeGui() override;
    void RenderAfterGui() override;
    void DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth) override;
    void Load(const nlohmann::json& j) override;
    void Save(nlohmann::json& j) const override;
private:
    std::deque<Notification> notifications;
    ImVec4 childBg;
};