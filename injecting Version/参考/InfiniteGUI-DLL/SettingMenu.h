#pragma once
#include "LeftPanel.h"
#include "MainPanel.h"

constexpr ImVec2 settingMenuSize = ImVec2(900, 556);
class SettingMenu
{
public:
	SettingMenu()
	{
		leftPanel = new LeftPanel();
		mainPanel = new MainPanel();
		exitButton = new MyButton("9", ImVec2(30.0f, 30.0f));
	}
	~SettingMenu()
	{
		delete leftPanel;
		delete mainPanel;
		delete exitButton;
	}
	void Init() const
	{
		mainPanel->Init();
	}
	bool Draw(bool &done)
	{
		ImGui::BeginChild(u8"主控制面板Inner", settingMenuSize, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		//将退出按钮显示在右上角
		ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 30 - 15.0f, 15.0f));
		bool exit = false;
		ImGui::PushFont(opengl_hook::gui.iconFont);
		if (exitButton->Draw())
		{
			exit = true;
		}
		ImGui::PopFont();
		ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
		ImVec2 screenPos = ImGui::GetCursorScreenPos();

		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 LeftPanelBg1 = ImVec2(0.0f, 0.0f);
		ImVec2 LeftPanelBg2 = ImVec2(padding * 2 + 150.0f + screenPos.x, settingMenuSize.y + screenPos.y);
		//ImVec4 LeftPanelBgColor = ImGui::GetColorU32(ImGuiCol_ChildBg)
		draw->AddRectFilled(LeftPanelBg1, LeftPanelBg2, ImGui::GetColorU32(ImGuiCol_WindowBg));

		leftPanel->Draw();
		showExitConfirmDialog(done);
		ImVec2 pos1 = ImVec2(padding * 2 + 150.0f + screenPos.x, screenPos.y);
		ImVec2 pos2 = ImVec2(padding * 2 + 150.0f + screenPos.x, settingMenuSize.y + screenPos.y);
		draw->AddLine(pos1, pos2, ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
		ImGui::SetCursorPos(ImVec2(padding * 2 + 150.0f, 0.0f));
		int index = leftPanel->GetSelectedIndex();
		if (index == 4)
		{
			// 请求退出（只弹窗）
			showExitConfirm = true;
			ImGui::OpenPopup(u8"退出确认");
		}
		else
		{
			if (index != lastSelectedIndex)
				mainPanel->SetPanelType(index);
			lastSelectedIndex = index; //不重复执行SetPanelType
		}
		mainPanel->Draw();
		ImGui::EndChild();
		return exit;
	}

	void Enter() const
	{
		leftPanel->Enter();
		mainPanel->Enter();
	}

private:

	void showExitConfirmDialog(bool &done)
	{
		if (showExitConfirm)
		{
			ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_Always);

			if (ImGui::BeginPopupModal(u8"退出确认", nullptr,
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoCollapse))
			{
				ImGuiStd::TextShadow(u8"确定要退出吗？");

				// 居中按钮
				float buttonWidth = 100.0f;
				float spacing = ImGui::GetStyle().ItemSpacing.x;
				float totalWidth = buttonWidth * 2 + spacing;
				float offsetX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
				if (offsetX > 0)
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

				if (ImGui::Button(u8"确定", ImVec2(buttonWidth, 0)))
				{
					done = true;               // 真正退出
					opengl_hook::exitByMenu = true;
					showExitConfirm = false;
					ImGui::CloseCurrentPopup();
					if (GlobalConfig::Instance().autoSave) {
						ConfigManager::Instance().Save();
					}
					ClickSound::Instance().PlayExitSound();
				}

				ImGui::SameLine();

				if (ImGui::Button(u8"取消", ImVec2(buttonWidth, 0)))
				{
					leftPanel->SetSelectedIndex(lastSelectedIndex); // 取消退出，恢复上一次选择的选项
					showExitConfirm = false;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}
		}
	}
	bool showExitConfirm = false;
	int lastSelectedIndex = 0;
	LeftPanel * leftPanel;
	MainPanel * mainPanel;
	MyButton * exitButton;
};
