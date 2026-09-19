#pragma once
#include "Item.h"
#include "UpdateModule.h"
#include "WindowModule.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <vector>
#include <string>
#include <chrono>

#pragma comment(lib, "windowsapp.lib")

struct DecodedImage
{
    std::vector<unsigned char> rgba;
    int width = 0;
    int height = 0;
    bool ready = false;
};

class MusicInfoItem : public Item, public UpdateModule, public WindowModule {
public:
    MusicInfoItem()
    {
        type = Hud; // 信息项类型
        name = u8"音乐信息显示";
        description = u8"显示系统当前播放的媒体信息";
        icon = u8"\uE027";
        updateIntervalMs = 1000;
        prevSkipButton = new MyButton(u8"\uE01b", ImVec2(30.0f, 30.0f));
        nextSkipButton = new MyButton(u8"\uE018", ImVec2(30.0f, 30.0f));
        stopButton = new MyButton(u8"\uE019", ImVec2(30.0f, 30.0f));
        lastUpdateTime = std::chrono::steady_clock::now();
        MusicInfoItem::Reset();
    }
    ~MusicInfoItem() override
    {
        delete prevSkipButton;
        delete nextSkipButton;
        delete stopButton;
    }
    static MusicInfoItem& Instance() {
        static MusicInfoItem instance;
        return instance;
    }

    void Toggle() override;
    void Reset() override
    {
        ResetWindow();
        width = 320.0f;
        height = 65.0f;

        isCustomSize = true;
        isEnabled = false;

        customCoverRound = false;
        coverRounding = 0.0f;

        hideWhenNoMedia = false;

        dirtyState.contentDirty = true;
        dirtyState.animating = false;
    }

    void Update() override;
    void HoverSetting() override;
    void DrawContent() override;
    void DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth) override;
    void Load(const nlohmann::json& j) override;
    void Save(nlohmann::json& j) const override;
    void ShutDown();
    //winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager GetMediaManager() const { return mediaManager; }
private:
    void InitMediaManager();
    void RenderPlaybackBar();
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager mediaManager{ nullptr };
    bool mediaManagerReady = false;

    // ===== 媒体信息 =====
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring source;

    // ===== 封面纹理 =====
    Texture coverTexture;
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> lastBytes;
    //ImTextureID coverTexture;
    float coverSize;


    bool hasMedia = false;
    bool hasOthers = false;
    bool paused = false;
    bool lastPaused = false;

    std::unique_ptr<DecodedImage> pendingImage;

    bool customCoverRound;
    float coverRounding;

    bool hideWhenNoMedia;

    MyButton* prevSkipButton;
    MyButton* nextSkipButton;
    MyButton* stopButton;

    ImVec2 barPos;
};
