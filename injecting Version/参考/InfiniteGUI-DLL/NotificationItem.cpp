#pragma once

#include "NotificationItem.h"

void NotificationItem::AddNotification(NotificationType type, const std::string& message, int durationMs)
{
	int index = (int)notifications.size();
	Notification notification(type, message, index, durationMs);
	notifications.push_back(notification);
	dirtyState.animating = true;
}

void NotificationItem::PopNotification()
{
	notifications.pop_front();
	for (int i = 0; i < notifications.size(); i++)
	{
		notifications[i].SetPlaceIndex(i);
	}
}

void NotificationItem::Update()
{
	if (notifications.empty())
	{
		dirtyState.animating = false;
		return;
	}
	if (notifications.front().Done())
	{
		PopNotification();
	}
	for (auto& notification : notifications)
	{
		notification.ShouldLeave();
	}
}

void NotificationItem::Toggle()
{
}

void NotificationItem::RenderGui()
{
	PushRounding(itemStyle.windowRounding);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, itemStyle.bgColor);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, childBg);
	ImGui::PushStyleColor(ImGuiCol_Border, itemStyle.borderColor);
	if (itemStyle.rainbowFont) processRainbowFont();
	else ImGui::PushStyleColor(ImGuiCol_Text, itemStyle.fontColor);

	ImGui::PushFont(NULL, itemStyle.fontSize);
	for (auto& notification : notifications)
	{
		notification.RenderGui();
	}

	ImGui::PopFont();
	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar(7);
}

void NotificationItem::RenderBeforeGui()
{
}

void NotificationItem::RenderAfterGui()
{
}

void NotificationItem::DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth)
{
	float bigItemWidth = centerX * 2.0f - bigPadding * 4.0f;

	ImGui::SetCursorPosX(bigPadding);

	if (ImGui::Button(u8"点我测试",ImVec2(bigItemWidth, 0.0f)))
	{
		AddNotification(NotificationType_Info, u8"这是一条测试弹窗~");
	}
	DrawStyleSettings(bigPadding, centerX, itemWidth);

	ImGui::SameLine();
	ImGui::SetCursorPosX(centerX + bigPadding);
	ImGuiStd::EditColor(u8"进度条颜色", childBg, itemStyle.bgColor);

}

void NotificationItem::Load(const nlohmann::json& j)
{
	LoadItem(j);
	LoadStyle(j);
	ImGuiStd::LoadImVec4(j, "childBg", childBg);

}

void NotificationItem::Save(nlohmann::json& j) const
{
	SaveItem(j);
	SaveStyle(j);
	ImGuiStd::SaveImVec4(j, "childBg", childBg);
}
