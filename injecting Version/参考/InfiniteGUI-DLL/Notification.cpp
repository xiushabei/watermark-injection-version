#pragma once

#include "Notification.h"
#include "Anim.h"

void Notification::RenderGui()
{
    SetMaxElementPos();
    //获取io
    ImGuiIO& io = ImGui::GetIO();
    float speed = 5.0f * 3.0f * std::clamp(io.DeltaTime, 0.0f, 0.05f);
    if(state != Leaving)
    {
        curElement.pos = ImLerp(curElement.pos, targetElement.pos, speed);
        // 判断动画是否结束
        if (Anim::AlmostEqual(curElement.pos, targetElement.pos, 1.0f))
        {
            curElement.pos = targetElement.pos;
            state = Staying;
        }
    }
    else
    {
        curElement.pos = ImLerp(curElement.pos, startElement.pos, speed);
        // 判断动画是否结束
        if (Anim::AlmostEqual(curElement.pos, startElement.pos, 1.0f))
        {
            done = true;
        }
    }
    ImGui::SetNextWindowPos(curElement.pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    bool open = true;
    std::string windowName = "Notification" + std::to_string(id);

    ImGui::Begin(windowName.c_str(), &open, flags);

    auto now = std::chrono::steady_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - joinTime).count();

    duration = static_cast<float>(elapsedTime) / durationMs;
    duration = std::clamp(duration, 0.0f, 1.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::SetCursorPos(ImVec2(0,0));
    ImVec2 pos = ImGui::GetCursorScreenPos();
    //将颜色背景设置成ImGuiCol_ChildBg
    draw->AddRectFilled(
        ImVec2(pos.x + 3.0f, pos.y + 3.0f),
        ImVec2(pos.x + (size.x - 3.0f) * duration, pos.y + size.y - 3.0f),
        ImGui::GetColorU32(ImGuiCol_ChildBg),
        ImGui::GetStyle().WindowRounding, // 圆角
        ImDrawFlags_RoundCornersAll
    );
    ImGui::SetCursorPos(ImGui::GetStyle().WindowPadding);
    ImGui::PushFont(opengl_hook::gui.iconFont, ImGui::GetFontSize() * 0.8f);
    ImGuiStd::TextShadow(icon.c_str());
    ImGui::PopFont();
    ImGui::PushFont(NULL, ImGui::GetFontSize() * 0.8f);
    ImGui::SameLine();
    float startX = ImGui::GetCursorPosX();
    ImGui::BeginDisabled();
    ImGuiStd::TextShadow(title.c_str());
    ImGui::EndDisabled();
    ImGui::PopFont();

    ImGui::Separator();
    ImGui::SetCursorPosX(startX);
    ImGuiStd::TextShadowEllipsis(message.c_str(), ImGui::GetContentRegionAvail().x);
    ImGui::End();
}

void Notification::RenderBeforeGui()
{
}

void Notification::RenderAfterGui()
{
}

bool Notification::Done() const
{
    return done;
}

void Notification::SetMaxElementPos()
{
    ImVec2 maxScreenSize = ImVec2((float)opengl_hook::screen_size.x, (float)opengl_hook::screen_size.y);
    startElement.pos = ImVec2(maxScreenSize.x + windowPadding, maxScreenSize.y - (placeIndex + 1.5f) * (size.y + windowPadding));
    targetElement.pos = ImVec2(maxScreenSize.x - size.x - windowPadding, startElement.pos.y);
}

void Notification::SetPlaceIndex(int index)
{
    this->placeIndex = index;
}

bool Notification::ShouldLeave()
{        //if (updateIntervalMs == -1) return false;
    auto now = std::chrono::steady_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - joinTime).count();
    if(elapsedTime >= durationMs)
    {
        state = Leaving;
        return true;
    }
    return false;
}
