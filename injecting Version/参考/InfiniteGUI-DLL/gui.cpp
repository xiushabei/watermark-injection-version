#include "gui.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_win32.h"
#include "opengl_hook.h"

#include <GL/glew.h>
#include <GL/GL.h>

#include "fonts.h"
#include "ImGuiSty.h"
#include "ItemManager.h"
#include "GlobalConfig.h"
#include "GuiFrameLimiter.h"

static ImGuiContext* imGuiContext = nullptr;
static CachedDrawData g_Cache;
static bool s_imguiInitOk = false;
bool g_gl30Available = false;
bool g_vaoAvailable = false;

void Gui::init()
{
	imGuiContext = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = nullptr;
	SetStyleGray();
	ImGui_ImplWin32_Init(opengl_hook::handle_window);

	// detect GL version
	const char* glVer = (const char*)glGetString(GL_VERSION);
	int glMajor = 0, glMinor = 0;
	if (glVer) sscanf_s(glVer, "%d.%d", &glMajor, &glMinor);
	g_gl30Available = (glMajor >= 3);
	const char* glslVer = g_gl30Available ? nullptr : "#version 120";

	char diag[256];
	sprintf_s(diag, "[Watermark] GL=%s major=%d minor=%d gl30=%d\n", glVer ? glVer : "?", glMajor, glMinor, g_gl30Available);
	OutputDebugStringA(diag);

	// GLEW FIRST -- load all GL function pointers before anything else
	glewExperimental = GL_TRUE;
	const GLenum glewErr = glewInit();
	if (GLEW_OK != glewErr) {
		sprintf_s(diag, "[Watermark] glewInit failed: %s\n", (const char*)glewGetErrorString(glewErr));
		OutputDebugStringA(diag);
		if (g_gl30Available) s_imguiInitOk = false;
	}

	// check VAO availability BEFORE calling ImGui (which uses glGenVertexArrays internally)
	g_vaoAvailable = (glGenVertexArrays && glBindVertexArray);
	sprintf_s(diag, "[Watermark] VAO=%d\n", g_vaoAvailable);
	OutputDebugStringA(diag);

	if (g_vaoAvailable)
	{
		s_imguiInitOk = ImGui_ImplOpenGL3_Init(glslVer);

		// P1: verify test shader compiles
		if (s_imguiInitOk) {
			static const char* testVert = "#version 120\nattribute vec2 a;void main(){gl_Position=vec4(a,0,1);}";
			static const char* testFrag = "#version 120\nvoid main(){gl_FragColor=vec4(1);}";
			GLuint vs = glCreateShader(GL_VERTEX_SHADER);
			glShaderSource(vs, 1, &testVert, 0); glCompileShader(vs);
			GLint vOk = 0; glGetShaderiv(vs, GL_COMPILE_STATUS, &vOk);
			GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
			glShaderSource(fs, 1, &testFrag, 0); glCompileShader(fs);
			GLint fOk = 0; glGetShaderiv(fs, GL_COMPILE_STATUS, &fOk);
			GLuint prog = glCreateProgram();
			glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
			GLint linked = 0; glGetProgramiv(prog, GL_LINK_STATUS, &linked);
			sprintf_s(diag, "[Watermark] testShader vert=%d frag=%d linked=%d\n", vOk, fOk, linked);
			OutputDebugStringA(diag);
			if (!vOk || !fOk || !linked) s_imguiInitOk = false;
			glDeleteShader(vs); glDeleteShader(fs); glDeleteProgram(prog);
		}
	}
	else
	{
		s_imguiInitOk = false;
	}

	// P3: max texture size
	{
		GLint maxTex = 512;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
		sprintf_s(diag, "[Watermark] GL_MAX_TEXTURE_SIZE=%d\n", maxTex);
		OutputDebugStringA(diag);
	}

	sprintf_s(diag, "[Watermark] ImGuiOk=%d VAO=%d\n", s_imguiInitOk, g_vaoAvailable);
	OutputDebugStringA(diag);

	// font loading (only needed if ImGui is usable)
	if (s_imguiInitOk)
	{
		ImFontConfig font_cfg;
		font_cfg.FontDataOwnedByAtlas = false;
		font_cfg.OversampleH = 1; font_cfg.OversampleV = 1; font_cfg.PixelSnapH = true;
		float fontSize = 20.0f;

		if (GlobalConfig::Instance().fontPath == "default")
			font = io.Fonts->AddFontFromMemoryTTF(Fonts::alibaba.data, Fonts::alibaba.size, fontSize, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
		else
			font = io.Fonts->AddFontFromFileTTF(GlobalConfig::Instance().fontPath.c_str(), fontSize, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
		if (font == nullptr) {
			MessageBox(NULL, L"font load failed", L"Error", MB_OK);
			font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", fontSize, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
		}
		iconFont = io.Fonts->AddFontFromMemoryTTF(Fonts::icons.data, Fonts::icons.size, fontSize, &font_cfg);
		io.FontDefault = font;
	}

	isInit = true;
}

void Gui::clean()
{
	if(!isInit) return; isInit = false;
	while (opengl_hook::rendering) {}
	g_Cache.Clear();
	if (imGuiContext)ImGui::GetIO().Fonts->Clear();
	if ((ImGui::GetCurrentContext() ? (void*)ImGui::GetIO().BackendRendererUserData : nullptr))ImGui_ImplOpenGL3_Shutdown();
	if ((ImGui::GetCurrentContext() ? (void*)ImGui::GetIO().BackendPlatformUserData : nullptr))ImGui_ImplWin32_Shutdown();
	if (imGuiContext)ImGui::DestroyContext(imGuiContext);
}

void Gui::render()
{
	if(ImGui::GetCurrentContext() == nullptr) return;
	if(!s_imguiInitOk) return;
	ItemManager::Instance().RenderAllBeforeGui();
	if(GlobalConfig::Instance().enableOptimization)
	{
		if (GuiFrameLimiter::Instance().ShouldUpdate() && ItemManager::Instance().IsDirty())
		{
			ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
			ItemManager::Instance().RenderAllGui();
			ImGui::Render(); CacheDrawData(g_Cache, ImGui::GetDrawData());
		}
		RenderCachedDrawData(g_Cache);
	}
	else
	{
		ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
		ItemManager::Instance().RenderAllGui();
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
	ItemManager::Instance().RenderAllAfterGui();
}
