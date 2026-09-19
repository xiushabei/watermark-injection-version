#pragma once
#include <thread>

#include "menuRule.h"
#include "App.h"
#include "ImGuiStd.h"
#include "imgui/imgui.h"
#include "FileUtils.h"
#include "StringConverter.h"

class AboutsPanel
{
public:
	static void Draw()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
		ImGui::BeginChild("About", ImVec2(-padding + ImGui::GetStyle().WindowPadding.x, -padding + ImGui::GetStyle().WindowPadding.y), true, flags);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));

		ImGuiStyle& style = ImGui::GetStyle();
		float basePadding = style.WindowPadding.x;
		float bigPadding = basePadding * 3.0f;

		float contentWidth = ImGui::GetContentRegionAvail().x;
		float centerX = contentWidth * 0.5f;
		float itemWidth = centerX - bigPadding * 4.0f;
		float bigItemWidth = centerX * 2.0f - bigPadding * 4.0f;
		
        ImGui::PushFont(NULL, ImGui::GetFontSize() * 0.8f);
        ImGui::BeginDisabled();
        ImGuiStd::TextShadow(u8"关于");
        ImGui::EndDisabled();
        ImGui::PopFont();

        ImGui::SetCursorPosX(bigPadding);
        ImGuiStd::TextShadow(App::Instance().appName.c_str());
        ImGui::SameLine();
        std::string appVersion = std::to_string(App::Instance().appVersion.major) + "." + std::to_string(App::Instance().appVersion.minor) + "." + std::to_string(App::Instance().appVersion.build);
        ImGuiStd::TextShadow(("v" + appVersion).c_str());
        ImGui::SameLine();
        static std::atomic<bool> checkingUpdate = false;
        static std::atomic<bool> updateFinished = false;
        static bool updateHasNew = false;
        if (ImGui::Button(u8"检查更新") && !checkingUpdate)
        {
            ImGui::OpenPopup(u8"-->检查中...");

            checkingUpdate = true;
            updateFinished = false;

            std::thread([] {
                bool result = App::Instance().CheckUpdate();
                updateHasNew = !result;
                updateFinished = true;
                checkingUpdate = false;
                }).detach();
        }
        if (ImGui::BeginPopupModal(u8"-->检查中...", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (checkingUpdate)
            {
                ImGuiStd::TextShadow(u8"正在检查更新，请稍候...");
            }
            else if (updateFinished)
            {
                if (updateHasNew)
                {
                    ImGuiStd::TextShadow(u8"发现新版本！");
                    std::string cloudVersion =
                        std::to_string(App::Instance().cloudVersion.major) + "." +
                        std::to_string(App::Instance().cloudVersion.minor) + "." +
                        std::to_string(App::Instance().cloudVersion.build);

                    ImGuiStd::TextShadow((u8"最新版：v" + cloudVersion).c_str());
                }
                else
                    ImGuiStd::TextShadow(u8"当前已是最新版本");
                if (ImGui::Button(u8"确定"))
                {
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(bigPadding + centerX);
        ImGuiStd::TextShadow(u8"用户协议：");
        ImGui::SameLine();
        if (ImGui::Button(u8"License"))
        {
            std::wstring path = StringConverter::Utf8ToWstring(FileUtils::modulePath) + L"\\LICENSE.txt";
            ShellExecute(NULL, NULL, path.c_str(), NULL, NULL, SW_SHOWNORMAL);

        }

        ImGui::SetCursorPosX(bigPadding);
        ImGuiStd::TextShadow(u8"作者：");
        ImGui::SameLine();
        if (ImGui::Button(App::Instance().appAuthor.c_str()))
        {
            ShellExecute(NULL, NULL, L"https://space.bilibili.com/399194206", NULL, NULL, SW_SHOWNORMAL);
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(bigPadding + centerX);
        ImGuiStd::TextShadow(u8"赞助链接：");
        ImGui::SameLine();
        if (ImGui::Button(u8"爱发电"))
        {
            ShellExecute(NULL, NULL, L"https://ifdian.net/a/qc_max", NULL, NULL, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        ImGuiStd::TextShadow(u8" & ");

        ImGui::SameLine();
        if (ImGui::Button(u8"GitHub"))
        {
            ShellExecute(NULL, NULL, L"https://github.com/QCMaxcer/InfiniteGUI-Minecraft-DLL", NULL, NULL, SW_SHOWNORMAL);
        }

        ImGui::SetCursorPosX(bigPadding);
		ImGui::BeginChild("InfoChild", ImVec2(contentWidth - bigPadding * 2, -basePadding), true, ImGuiWindowFlags_NoScrollbar);
		ImGuiStd::TextShadow(u8"Watermark Injection");
		ImGui::NewLine();
		ImGuiStd::TextShadow(u8"Minecraft HUD 覆盖层 DLL");
		ImGuiStd::TextShadow(u8"支持拖拽导入水印图片 + 编辑模式");
		ImGui::EndChild();

		ImGui::PopStyleVar();
		ImGui::EndChild();

	}
private:

};
