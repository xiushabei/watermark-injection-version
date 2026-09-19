#include "ClickEffect.h"
#include "ImGuiStd.h"
#include "GameStateDetector.h"
void ClickEffect::Toggle()
{
	if (!isEnabled)
	{
		ClearClickEffects();
	}
}

void ClickEffect::Update()
{
	if (clickEffects.empty()) {
		dirtyState.animating = false;
	}
	else
	{
		if (clickEffects.front()->IsFinished())
		{
			delete clickEffects.front();
			clickEffects.pop_front();
		}
	}
	if(!GameStateDetector::Instance().IsInGameWindow()) return;
	GameState state = GameStateDetector::Instance().GetCurrentState();
	bool yes = false;
	if (enabledInGame && state == InGame)
		yes = true;
	else if (enabledInGameMenu && state == InGameMenu)
		yes = true;
	else if (enabledInMenu && state == InMenu)
		yes = true;
	if(!yes) return;
	//用windows.h函数来获取客户端中鼠标位置
	POINT point;
	if (state == InGame)
	{
		//获取窗口中心位置
		RECT rect;
		GetClientRect(opengl_hook::handle_window, &rect);
		point.x = rect.left + rect.right / 2 - 1.0f;
		point.y = rect.top + rect.bottom / 2 + 1.0f;
	}
	else
	{
		GetCursorPos(&point);
		ScreenToClient(opengl_hook::handle_window, &point);
	}

	ImVec2 mousePos = ImVec2(point.x, point.y);

	if (keyStateHelper.GetKeyClick(VK_LBUTTON) && enableLeftClick || keyStateHelper.GetKeyClick(VK_RBUTTON) && enableRightClick)
	{
		ClickCircle* clickCircle = new ClickCircle(mousePos, clickCircleSettings);
		AddClickEffect(clickCircle);
		dirtyState.animating = true;
	}
}

void ClickEffect::RenderGui()
{
	Draw();
}

void ClickEffect::RenderBeforeGui()
{
}

void ClickEffect::RenderAfterGui()
{
}

void ClickEffect::Load(const nlohmann::json& j)
{
	LoadItem( j );
	if(j.contains("enabledInGame")) enabledInGame = j["enabledInGame"];
	if(j.contains("enabledInGameMenu")) enabledInGameMenu = j["enabledInGameMenu"];
	if(j.contains("enabledInMenu")) enabledInMenu = j["enabledInMenu"];
	if(j.contains("enableLeftClick")) enableLeftClick = j["enableLeftClick"];
	if(j.contains("enableRightClick")) enableRightClick = j["enableRightClick"];
	if(j.contains("clickCircleSettings.animSpeed")) clickCircleSettings.animSpeed = j["clickCircleSettings.animSpeed"];
    ImGuiStd::LoadImVec4(j, "clickCircleSettings.color", clickCircleSettings.color);
	if(j.contains("clickCircleSettings.radius")) clickCircleSettings.radius = j["clickCircleSettings.radius"];
}

void ClickEffect::Save(nlohmann::json& j) const
{
	SaveItem( j );
	j["enabledInGame"] = enabledInGame;
	j["enabledInGameMenu"] = enabledInGameMenu;
	j["enabledInMenu"] = enabledInMenu;
	j["enableLeftClick"] = enableLeftClick;
	j["enableRightClick"] = enableRightClick;
	j["clickCircleSettings.animSpeed"] = clickCircleSettings.animSpeed;
	ImGuiStd::SaveImVec4(j, "clickCircleSettings.color", clickCircleSettings.color);
	j["clickCircleSettings.radius"] = clickCircleSettings.radius;
}

void ClickEffect::DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth)
{
	float bigItemWidth = centerX * 2.0f - bigPadding * 4.0f;

	ImGui::SetCursorPosX(bigPadding);
	ImGui::SetNextItemWidth(itemWidth);
	//DrawItemSettings();
	ImGui::Checkbox(u8"游戏内启用", &enabledInGame);
	ImGui::SameLine();
	ImGui::SetCursorPosX(bigPadding + centerX);
	ImGui::SetNextItemWidth(itemWidth);
	ImGui::Checkbox(u8"游戏内菜单启用", &enabledInGameMenu);

	ImGui::SetCursorPosX(bigPadding);
	ImGui::SetNextItemWidth(itemWidth);
	ImGui::Checkbox(u8"Watermark Injection菜单内启用", &enabledInMenu);
	ImGui::SetCursorPosX(bigPadding);
	ImGui::SetNextItemWidth(itemWidth);
	ImGui::Checkbox(u8"左键", &enableLeftClick);
	ImGui::SameLine();
	ImGui::SetCursorPosX(bigPadding + centerX);
	ImGui::SetNextItemWidth(itemWidth);
	ImGui::Checkbox(u8"右键", &enableRightClick);

	ImGui::PushFont(NULL, ImGui::GetFontSize() * 0.8f);
	ImGui::BeginDisabled();
	ImGuiStd::TextShadow(u8"圆圈特效设置");
	ImGui::EndDisabled();
	ImGui::PopFont();

	ImGui::SetCursorPosX(bigPadding);
	ImGui::SetNextItemWidth(bigItemWidth);
	ImGui::SliderFloat(u8"动画速度", &clickCircleSettings.animSpeed, 1.0f, 30.0f, "%.1f");
	ImGui::SetCursorPosX(bigPadding);
	ImGui::SetNextItemWidth(bigItemWidth);
	ImGui::SliderFloat(u8"半径", &clickCircleSettings.radius, 5.0f, 100.0f, "%.1f");
	ImGui::SetCursorPosX(bigPadding);
	ImGui::SetNextItemWidth(itemWidth);
	ImGuiStd::EditColor(u8"颜色", clickCircleSettings.color, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

}
