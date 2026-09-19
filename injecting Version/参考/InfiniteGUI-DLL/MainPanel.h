#pragma once
#include "AboutsPanel.h"
#include "GlobalSettings.h"
#include "menuRule.h"
#include "ModulesPanel.h"
#include "MoresPanel.h"

enum PanelType
{
	ModulePanel,
	SettingsPanel,
	EditPanel,
	AboutPanel,
};

class MainPanel
{
public:
	MainPanel()
	{
		m_modulesPanel = new ModulesPanel();
		m_globalSettings = new GlobalSettings();
		m_aboutsPanel = new AboutsPanel();
		m_editPanel = new MoresPanel();
	}
	~MainPanel()
	{
		delete m_modulesPanel;
		delete m_globalSettings;
		delete m_aboutsPanel;
		delete m_editPanel;
	}
	void Draw() const
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec2 screenPos = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();

		ImVec2 pos1 = ImVec2(screenPos.x + padding * 0.5f, screenPos.y + 60);
		ImVec2 pos2 = ImVec2(screenPos.x + mainPanelSize.x - padding * 0.5f, screenPos.y + 60);

		draw->AddLine(pos1, pos2, ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

		ImGui::PushFont(NULL, style.FontSizeBase * 1.5f);
		ImVec2 titleSize = ImGui::CalcTextSize(u8"模组");
		ImGui::SetCursorScreenPos(ImVec2(screenPos.x + padding, screenPos.y + 60 - titleSize.y - padding * (1 - 0.618f)));
		switch (m_panelType)
		{
		case ModulePanel:
			ImGuiStd::TextShadow(u8"模组");
			ImGui::PopFont();
			ImGui::SetCursorScreenPos(ImVec2(screenPos.x + padding, screenPos.y + 60 + padding * 0.625f));
			m_modulesPanel->Draw();
			// Draw Module Panel
			break;
		case SettingsPanel:
			ImGuiStd::TextShadow(u8"设置");
			ImGui::PopFont();
			ImGui::SetCursorScreenPos(ImVec2(screenPos.x + padding, screenPos.y + 60 + padding * 0.625f));
			m_globalSettings->Draw();
			// Draw Settings Panel
			break;
		case EditPanel:
			ImGuiStd::TextShadow(u8"编辑");
			ImGui::PopFont();
			ImGui::SetCursorScreenPos(ImVec2(screenPos.x + padding, screenPos.y + 60 + padding * 0.625f));
			m_editPanel->Draw();
			// Draw More Panel
			break;
		case AboutPanel:
			ImGuiStd::TextShadow(u8"关于");
			ImGui::PopFont();
			ImGui::SetCursorScreenPos(ImVec2(screenPos.x + padding, screenPos.y + 60 + padding * 0.625f));
			m_aboutsPanel->Draw();
			// Draw About Panel
			break;
		}
		//draw category bar
	}

	void Enter() const
	{
		m_modulesPanel->Enter();
	}

	void Init() const
	{
		m_modulesPanel->Init();
	}

	void SetPanelType(const int& index)
	{
		switch (index)
		{
		case 0:
			m_modulesPanel->Enter();
			m_panelType = ModulePanel;
			break;
		case 1:
			m_panelType = SettingsPanel;
			break;
		case 2:
			m_panelType = EditPanel;
			break;
		case 3:
			m_panelType = AboutPanel;
			break;
		default:
			m_panelType = ModulePanel;
		}
	}
	PanelType GetPanelType() const { return m_panelType; }

private:
	PanelType m_panelType;
	ModulesPanel *m_modulesPanel;
	GlobalSettings *m_globalSettings;
	AboutsPanel *m_aboutsPanel;
	MoresPanel* m_editPanel;
};
