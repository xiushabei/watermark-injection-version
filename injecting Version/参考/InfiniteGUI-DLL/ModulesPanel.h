#pragma once
#include "CategoryBar.h"
#include "CounterItem.h"
#include "FileCountItem.h"
#include "ModuleButton.hpp"
#include "ModuleCard.h"
#include "ModuleSettings.h"
#include "ItemManager.h"
#include "TextItem.h"

class ModulesPanel
{
public:
	ModulesPanel()
	{
		m_categoryBar = new CategoryBar();
		filter = m_categoryBar->GetFilter();
	}
	~ModulesPanel()
	{
		delete m_categoryBar;
		for (auto moduleCard : m_moduleCard)
		{
			delete moduleCard;
		}
		for (auto moduleButton : m_moduleButtons)
		{
			delete moduleButton;
		}
	}

	void Init()
	{
		for (auto moduleCard : m_moduleCard)
		{
			delete moduleCard;
		}
		for (auto item : ItemManager::Instance().GetItems())
		{
			if (item->type != Hidden)
				m_moduleCard.push_back(new ModuleCard(item));
		}
	}
	void Draw()
	{
		if (!isInModuleSettings)
		{
			if (DrawModuleList())
			{
				isInModuleSettings = true;
			}
		}
		else
		{
			if (DrawModuleSettings())
			{
				isInModuleSettings = false;
			}
		}
	}

	void Enter()
	{
		selectedItem = nullptr;
		isInModuleSettings = false;
		selectedType = ItemType::All;
		m_categoryBar->SetSelectedIndex(0);
		needReset = true;
	}

private:

	bool DrawModuleList()
	{
		bool joinModuleSettings = false;
		//获取窗口是否在scroll
		ImVec2 startPos = ImGui::GetCursorPos();
		if (m_categoryBar->Draw())
		{
			switch (m_categoryBar->GetSelectedIndex())
			{
			case 0: selectedType = ItemType::All; break;
			case 1: selectedType = ItemType::Hud; break;
			case 2: selectedType = ItemType::Visual; break;
			case 3: selectedType = ItemType::Util; break;
			case 4: selectedType = ItemType::Server; break;
			default: selectedType = ItemType::All; break;
			}
			filter = m_categoryBar->GetFilter();
		}

		startPos.y += m_categoryBar->GetButtonHeight() + 13;
		ImGui::SetCursorPos(startPos);
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar;
		ImGui::BeginChild("ModuleList", ImVec2(-padding + ImGui::GetStyle().WindowPadding.x, -padding + ImGui::GetStyle().WindowPadding.y), true, flags);
		ImGui::SetCursorPos(ImVec2(padding, padding));

		if (needReset)
		{
			ImGui::SetScrollHereY(0.0f);
			needReset = false;
		}

		for (auto moduleCard : m_moduleCard)
		{
			auto* item = moduleCard->GetItem();

			if (selectedType != All && selectedType != item->type)
				continue;

			if (filter->IsActive() && !filter->PassFilter(item->name.c_str()) && !filter->PassFilter(item->description.c_str()))
				continue;

			if (moduleCard->Draw())
			{
				selectedItem = item;
				joinModuleSettings = true;
			}
		}
		ImGui::Dummy(ImVec2(0, 10));
		ImGui::EndChild();
		return joinModuleSettings;
	}

	bool DrawModuleSettings() const
	{
		bool exit = false;
		if(ModuleSettings::Draw(selectedItem))
			exit = true;
		return exit;
	}

	static bool IsWindowScrollingY()
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (!window)
			return false;

		static float lastScrollY = 0.0f;
		float curScrollY = window->Scroll.y;

		bool scrolling = (curScrollY != lastScrollY);
		lastScrollY = curScrollY;
		return scrolling;
	}
	static bool UsingMouseWheel()
	{
		return ImGui::GetIO().MouseWheel != 0.0f;
	}
	bool isInModuleSettings = false;
	Item *selectedItem = nullptr;
	ItemType selectedType = All;
	ImGuiTextFilter *filter;
	CategoryBar* m_categoryBar;
	std::vector<ModuleCard*> m_moduleCard;
	std::vector<ModuleButton*> m_moduleButtons;

	bool needReset = false;
};
