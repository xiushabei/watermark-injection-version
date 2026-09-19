#pragma once
#include "Anim.h"
#include "ClickEffectBase.h"
#include "imgui\imgui.h"

struct CircleData
{
	ImVec2 pos;
	float radius;
	float thickness;
	ImVec4 color;
};

struct ClickCircleSettings
{
	float radius = 50.0f;
	ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	float animSpeed = 5.0f;
	void Reset()
	{
		radius = 50.0f;
		color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		animSpeed = 5.0f;
	}
};

class ClickCircle : public Anim, public ClickEffectBase
{
public:
	ClickCircle(const ImVec2& pos, const ClickCircleSettings& settings, const float& thickness = 3.0f)
	{
		startData.pos = pos;
		endData.pos = pos;
		startData.radius = 1.0f;
		endData.radius = settings.radius;
		startData.thickness = thickness;
		endData.thickness = 1.0f;
		startData.color = settings.color;
		ImVec4 endColor = settings.color;
		endColor.w = 0.0f;
		endData.color = endColor;
		animSpeed = settings.animSpeed;
		curData = startData;
	}

	bool Draw(ImDrawList* draw_list, const float& dt) override
	{

		if (IsFinished())
		{
			return false;
		}
		DrawCircle(draw_list, curData);
		LerpCircle(curData, endData, animSpeed, dt);
		return true;
	}
	bool IsFinished() const override
	{
		return IsEqual(curData, endData);
	}
private:
	CircleData startData = {};
	CircleData curData = {};
	CircleData endData = {};
	float animSpeed = 10.0f;
	static void LerpCircle(CircleData & cur, const CircleData & target, const float damping, const float deltaTime)
	{
		SmoothLerp(cur.pos, target.pos, damping, deltaTime);
		SmoothLerp(cur.radius, target.radius, damping, deltaTime);
		SmoothLerp(cur.thickness, target.thickness, damping, deltaTime);
		SmoothLerp(cur.color, target.color, damping, deltaTime);
	}
	static void DrawCircle(ImDrawList* draw_list,const CircleData & cur)
	{
		draw_list->AddCircle(cur.pos,
			cur.radius,
			ImGui::ColorConvertFloat4ToU32(cur.color),
			0,
			cur.thickness);
	}
	static bool IsEqual(const CircleData& a, const CircleData& b)
	{
		return AlmostEqual(a.pos, b.pos)
			&& AlmostEqual(a.radius, b.radius)
			&& AlmostEqual(a.thickness, b.thickness)
			&& AlmostEqual(a.color, b.color);
	}
};

