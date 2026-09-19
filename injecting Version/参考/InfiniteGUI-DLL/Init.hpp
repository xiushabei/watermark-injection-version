#include "opengl_hook.h"
#include "FileUtils.h"
#include "ConfigManager.h"
#include "AudioManager.h"
#include <thread>
#include <atomic>

#include "App.h"
#include "ClickSound.h"
#include "GameKeyBind.h"
#include "HttpUpdateWorker.h"
#include "ItemManager.h"
#include "GuiFrameLimiter.h"
#include "NotificationItem.h"
inline HMODULE g_hModule = NULL;
inline std::thread g_updateThread;
inline bool g_uninitialized = false;

inline static std::atomic_bool g_running = ATOMIC_VAR_INIT(true);
// �̺߳������������� item ״̬
inline void UpdateThread() {
	while (g_running.load()) {
		if(opengl_hook::gui.isInit) ItemManager::Instance().UpdateAll();  // ����UpdateAll()����������item
		std::this_thread::sleep_for(std::chrono::milliseconds(1));  // ����1ms�����Ը���ʵ���������
	}
}

// ���������߳�
inline void StartThreads() {
	g_updateThread = std::thread(UpdateThread);
	g_updateThread.detach();  // ���߳���Ϊ��̨�߳�

}

// ֹͣ�����߳�
inline void StopThreads() {
	if (g_updateThread.joinable()) {
		g_updateThread.join();
	}
}

inline void Uninit() {
	if (g_uninitialized) return;
	opengl_hook::remove_hook();
	opengl_hook::clean();
	g_running = false;
	StopThreads();
	AudioManager::Instance().Shutdown();
	g_uninitialized = true;
}


inline DWORD WINAPI MainApp(LPVOID)
{
    FileUtils::InitPaths(g_hModule);
	//���������ļ�
	ConfigManager::Instance().Init();
	ConfigManager::Instance().LoadGlobal();
	GuiFrameLimiter::Instance().Init();
    opengl_hook::init();
	while (!opengl_hook::gui.isInit)
	{
		std::this_thread::yield();
	}
	ConfigManager::Instance().LoadProfile();
	//��ʼ����Ƶ������
	AudioManager::Instance().Init();
	ClickSound::PlayIntroSound();
	StartThreads();
	GameKeyBind::Instance().Load(FileUtils::optionsPath);
	if(!GameKeyBind::Instance().IsSuccess())
		NotificationItem::Instance().AddNotification(NotificationType_Warning, "Failed to load game keybinds. Please set manually in settings.", 10000);
	while (!opengl_hook::gui.done)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (opengl_hook::handle_window != WindowFromDC(opengl_hook::handle_device_ctx))
		{
			opengl_hook::lwjgl2FullscreenHandler();
		}
	}
	if(opengl_hook::exitByMenu) std::this_thread::sleep_for(std::chrono::milliseconds(300));
	Uninit();
	FreeLibraryAndExitThread(g_hModule, 0);
}