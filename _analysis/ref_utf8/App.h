#pragma once
#include <string>

struct version
{
	int major;
	int minor;
	int build;
};

class App
{
public:
	std::string appName = u8"Watermark Injection";
	version appVersion = { 1, 0, 5 };
	std::string appAuthor = u8"·����Max";
	std::string appDescription = "Watermark Injection - A dynamic-link library for injecting HUD overlays into Minecraft.";
	version cloudVersion = { 0, 0, 0 };
	std::wstring versionUrl = L"https://gitee.com/qc_max/InfiniteGUI/raw/master/version.json";
	static App& Instance()
	{
		static App instance;
		return instance;
	}

	bool CheckUpdate();
};