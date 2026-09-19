#pragma once
#include "ConfigManager.h"
#include "GlobalConfig.h"
#include "SelectPanel.h"
#include "menuRule.h"
#include "MyButton.hpp"
#include "opengl_hook.h"
#include "NotificationItem.h"
constexpr ImVec2 headerLogoPos = ImVec2(45.0f, 6.0f);
constexpr ImVec2 heaaderLogoSize = ImVec2(100.0f, 100.0f);



class LeftPanel
{
public:
	LeftPanel()
	{
		selectedPanel = new SelectPanel();
		m_saveButton = new MyButton(GlobalConfig::Instance().currentProfile.c_str(), ImVec2(150.0f, 50.0f));
	}
	~LeftPanel()
	{
		delete selectedPanel;
		delete m_saveButton;
	}
	void Draw()
	{
		DrawHeader();
		ImGui::SetCursorPos(ImVec2(padding, 115.0f));
		ImGui::PushFont(NULL, ImGui::GetFontSize() * 1.20f);
		if(selectedPanel->Draw())
			selectedIndex = selectedPanel->GetSelectedIndex();
		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		ImVec2 pos1 = ImGui::GetCursorScreenPos();
		pos1.x -= padding * 0.5;
		ImVec2 pos2 = pos1;
		pos2.x += padding + 150.0f;

		draw_list->AddLine(pos1, pos2, ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
		ImVec2 configPos = ImGui::GetCursorPos();
		configPos.y += padding;

		ImGui::SetCursorPos(configPos);
		std::string configText = u8"配置：" + GlobalConfig::Instance().currentProfile;
		float baseScale = 1.20f;

		float textWidth = ImGui::CalcTextSize(configText.c_str()).x;
		float buttonWidth = 150.0f;
		float paddingX = ImGui::GetStyle().FramePadding.x * 2.0f;
		float availableWidth = buttonWidth - paddingX;

		float autoScale = 1.0f;
		if (textWidth > availableWidth)
		{
			autoScale = availableWidth / textWidth;
			autoScale = std::clamp(autoScale, 0.6f, 1.0f);
		}
		ImGui::PopFont();
		ImGui::PushFont(NULL, ImGui::GetFontSize() * baseScale * autoScale);
		if (saved)
		{
			m_saveButton->SetLabelText(u8"已保存");
			m_saveButton->SetSelected(true); 
			if(ShouldUpdate()) saved = false;
		}
		else
		{
			if (m_saveButton->GetButtonState() == Hovered || m_saveButton->GetButtonState() == Active)
			{
				m_saveButton->SetLabelText(u8"保存");
			}
			else
			{
				m_saveButton->SetLabelText(configText.c_str());
			}
			m_saveButton->SetSelected(false);
		}
		if (m_saveButton->Draw())
		{
			savedTime = std::chrono::steady_clock::now();
			saved = true;
			ConfigManager::Instance().Save();
			ClickSound::Instance().PlaySaveSound();
			NotificationItem::Instance().AddNotification(NotificationType_Success,u8"配置文件已保存");

		}
		ImGui::PopFont();
	}

	void Enter()
	{
		selectedIndex = 0;
		selectedPanel->SetSelectedIndex(selectedIndex);
	}

	int GetSelectedIndex() const { return selectedIndex; }
	void SetSelectedIndex(int index) { 
		selectedIndex = index;
		selectedPanel->SetSelectedIndex(selectedIndex);
	}
private:

	SelectPanel* selectedPanel;
	int selectedIndex = 0;

	MyButton* m_saveButton;
	bool saved = false;

	// 检查是否到了更新的时间
	bool ShouldUpdate() {
		//if (updateIntervalMs == -1) return false;
		auto now = std::chrono::steady_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - savedTime).count();
		return elapsedTime >= updateIntervalMs;
	}
	int updateIntervalMs = 1000;    // 默认 1 秒
	std::chrono::steady_clock::time_point savedTime = std::chrono::steady_clock::now();
	void DrawHeader()
	{
		// 居中绘制图片
		ImGui::SetCursorPos(headerLogoPos);
		ImGui::Image(opengl_hook::gui.logoTexture.id,
			heaaderLogoSize
		);
	}
};
