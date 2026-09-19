#include "MusicInfoItem.h"

#include <stb_image.h>

#include "StringConverter.h"
#include "NotificationItem.h"

#include "pics\NeteaseMusicLogo.h"
#include "pics\MCLogo.h"
#include <psapi.h>

#include "pics/KuGouMusicLogo.h"

void MusicInfoItem::Toggle()
{
}

static bool IsCloudMusicProcess(DWORD pid)
{
    bool result = false;

    HANDLE hProc = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
        FALSE,
        pid
    );

    if (!hProc)
        return false;

    wchar_t path[MAX_PATH];
    if (GetModuleFileNameExW(hProc, nullptr, path, MAX_PATH))
    {
        // 只看 exe 文件名
        const wchar_t* exe = wcsrchr(path, L'\\');
        exe = exe ? exe + 1 : path;

        if (_wcsicmp(exe, L"cloudmusic.exe") == 0)
            result = true;
    }

    CloseHandle(hProc);
    return result;
}

static HWND FindNeteaseWindow()
{
    HWND result = nullptr;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
        {
            if (!IsWindowVisible(hwnd))
                return TRUE;

            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);

            if (!IsCloudMusicProcess(pid))
                return TRUE;

            // 过滤掉子窗口 / 工具窗口
            if (GetWindow(hwnd, GW_OWNER) != nullptr)
                return TRUE;

            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);
            
            // 排除桌面歌词窗口
            if (wcsstr(title, L"桌面歌词"))
                return TRUE;

            *(HWND*)lParam = hwnd;
            return FALSE;

        }, (LPARAM)&result);

    return result;
}

HWND FindKuGouWindow()
{
    HWND result = nullptr;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
        {
            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);

            if (wcslen(title) == 0)
                return TRUE;

            // 排除桌面歌词窗口
            if (wcsstr(title, L"桌面歌词"))
                return TRUE;

            // 关键字判断
            if (wcsstr(title, L" - 酷狗音乐"))
            {
                *(HWND*)lParam = hwnd;
                return FALSE; // 找到就停
            }

            return TRUE;
        }, (LPARAM)&result);

    return result;
}


static bool ParseNeteaseTitle(
    const std::wstring& title,
    std::wstring& outSong,
    std::wstring& outArtist)
{

    // 按 '-' 拆
    size_t pos = title.find(L" - ");
    if (pos == std::wstring::npos)
        return false;

    outSong = title.substr(0, pos);
    outArtist = title.substr(pos + 3);

    return true;
}

static bool ParseKuGouTitle(
    const std::wstring& title,
    std::wstring& outSong,
    std::wstring& outArtist)
{

    size_t pos = title.find(L" - 酷狗音乐");
    if (pos == std::wstring::npos)
        return false;
    std::wstring main = title.substr(0, pos);

    // 按 '-' 拆
    size_t pos2 = main.find(L" - ");
    if (pos2 == std::wstring::npos)
        return false;

    outArtist = main.substr(0, pos2);
    outSong = main.substr(pos2 + 3);

    return true;
}

static std::vector<uint8_t> ReadStream(
    winrt::Windows::Storage::Streams::IRandomAccessStream const& stream)
{
    using namespace winrt::Windows::Storage::Streams;

    uint32_t size = static_cast<uint32_t>(stream.Size());

    std::vector<uint8_t> data(size);
    DataReader reader(stream);
    reader.LoadAsync(size).get();
    reader.ReadBytes(data);
    return data;
}

struct MediaKey
{
    std::wstring title;
    std::wstring artist;
    std::wstring album;

    bool operator==(const MediaKey& rhs) const
    {
        return title == rhs.title &&
            artist == rhs.artist &&
            album == rhs.album;
    }
};

void MusicInfoItem::InitMediaManager()
{
    if (mediaManagerReady)
        return;
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        mediaManager =
            winrt::Windows::Media::Control::
            GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

        mediaManagerReady = true;
    }
    catch (...)
    {
        mediaManagerReady = false;
    }
}

void MusicInfoItem::Update()
{
    InitMediaManager();

    if (!mediaManagerReady)
        return;

    hasOthers = false;
    bool hasNetease = false;
    bool hasKuGou = false;

    auto session = mediaManager.GetCurrentSession();
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties mediaProps{ nullptr };
    if (session)
    {
        hasOthers = true;
        try
        {
            mediaProps = session.TryGetMediaPropertiesAsync().get();
        }
        catch (const winrt::hresult_error&)
        {
            hasOthers = false;
        }
    }
    HWND neteaseHwnd = FindNeteaseWindow();
    HWND kuGouHwnd = FindKuGouWindow();
    if (neteaseHwnd) hasNetease = true;
    if (kuGouHwnd) hasKuGou = true;

    static MediaKey lastKey;
    MediaKey currentKey;

    bool needDecode = false;

    if (hasOthers)
    {
        auto playbackInfo = session.GetPlaybackInfo();

        switch (playbackInfo.PlaybackStatus())
        {
        case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
            paused = true;
            break;
        case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
        case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
        default:
            paused = false;
        }

        if (lastPaused != paused)
        {
            dirtyState.contentDirty = true;
            lastPaused = paused;
        }


        currentKey = {
            mediaProps.Title().c_str(),
            mediaProps.Artist().c_str(),
            mediaProps.AlbumTitle().c_str()
        };

        if (currentKey == lastKey) return;

        title = mediaProps.Title().c_str();
        artist = mediaProps.Artist().c_str();
        album = mediaProps.AlbumTitle().c_str();
        source = session.SourceAppUserModelId().c_str();

        //如果source的后缀为.exe，去除.exe
        const std::wstring exeSuffix = L".exe";
        if (source.size() > exeSuffix.size() &&
            source.compare(source.size() - exeSuffix.size(), exeSuffix.size(), exeSuffix) == 0)
        {
            source.erase(source.size() - exeSuffix.size());
        }

        // ===== 封面 =====
        auto thumb = mediaProps.Thumbnail();
        if (thumb)
        {
            auto stream = thumb.OpenReadAsync().get();
            bytes = ReadStream(stream);
            needDecode = true;
        }
    }
    else if (hasNetease)
    {
        wchar_t titleBuf[256];
        GetWindowTextW(neteaseHwnd, titleBuf, 256);

        std::wstring song, artist;
        if (!ParseNeteaseTitle(titleBuf, song, artist))
        {
            song = L"未知";
            artist = L"未知";
        }

        currentKey = { song, artist, L"" };
        if (currentKey == lastKey)
            return;

        title = song;
        this->artist = artist;
        album.clear();
        paused = false;

        if (source != L"网易云音乐")
        {
            bytes.assign(
                NeteaseMusicLogo,
                NeteaseMusicLogo + NeteaseMusicLogoSize
            );

            needDecode = true;

            source = L"网易云音乐";
        }
    }
    else if (hasKuGou)
    {
        wchar_t titleBuf[256];
        GetWindowTextW(kuGouHwnd, titleBuf, 256);

        std::wstring song, artist;
        if (!ParseKuGouTitle(titleBuf, song, artist))
        {
            song = L"未知";
            artist = L"未知";
        }

        currentKey = { song, artist, L"" };
        if (currentKey == lastKey)
            return;

        title = song;
        this->artist = artist;
        album.clear();
        paused = false;

        if (source != L"酷狗音乐")
        {
            bytes.assign(
                KuGouMusicLogo,
                KuGouMusicLogo + KuGouMusicLogoSize
            );

            needDecode = true;

            source = L"酷狗音乐";
        }
    }
    else
    {
        hasMedia = !hideWhenNoMedia;
        if (!hasMedia) 
        {
            dirtyState.contentDirty = true;
            return;
        }

        std::wstring failTitle = L"没有音频播放喔~";
        std::wstring failArtist = L"QC_Max";
        std::wstring failSource = L"无限Gui";

        currentKey = { failTitle, failArtist, L"" };
        if (currentKey == lastKey)
            return;

        title = failTitle;
        artist = failArtist;
        album.clear();
        paused = false;

        if (source != failSource)
        {
            bytes.assign(
                MCLogo,
                MCLogo + MCLogoSize
            );

            needDecode = true;

            source = failSource;
        }
    }

    if (needDecode)
    {
        // 开后台线程解码
        std::thread([this, data = bytes]()
            {
                int w, h, ch;
                unsigned char* decoded =
                    stbi_load_from_memory(data.data(), data.size(), &w, &h, &ch, 4);

                if (!decoded)
                    return;

                auto img = std::make_unique<DecodedImage>();
                img->rgba.assign(decoded, decoded + w * h * 4);
                img->width = w;
                img->height = h;
                img->ready = true;

                stbi_image_free(decoded);

                //std::lock_guard<std::mutex> lock(imageMutex);
                pendingImage = std::move(img);

            }).detach();
    }

    hasMedia = !title.empty();
    lastKey = currentKey;

    std::string message =
        u8"音乐信息显示：已切歌。\n" +
        StringConverter::WstringToUtf8(currentKey.title);

    NotificationItem::Instance().AddNotification(
        NotificationType_Info, message, 5000);

    dirtyState.contentDirty = true;
}

void MusicInfoItem::RenderPlaybackBar()
{
    auto session = mediaManager.GetCurrentSession();
    if (!session) return;
    auto playbackInfo = session.GetPlaybackInfo();

    switch (playbackInfo.PlaybackStatus())
    {
    case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
        paused = true;
        break;
    case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
    case winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
    default:
        paused = false;
    }
    ImGui::PushFont(opengl_hook::gui.iconFont);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.8f, 0.8f, .8f, 1.0f));
    if (prevSkipButton->Draw())
    {
        session.TrySkipPreviousAsync(); // 上一首
    }
    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(barPos.x + 30 + ImGui::GetStyle().ItemSpacing.x, barPos.y));
    stopButton->SetLabelText(paused ? u8"\uE01a" : u8"\uE019");
    if (stopButton->Draw())
    {
        session.TryTogglePlayPauseAsync(); // 停止播放
    }
    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(barPos.x + 60 + ImGui::GetStyle().ItemSpacing.x * 2, barPos.y));
    if (nextSkipButton->Draw())
    {
        session.TrySkipNextAsync(); // 下一首
    }
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
}

void MusicInfoItem::HoverSetting()
{
    if(!hasMedia) return;
    ImGui::SetCursorPos(ImGui::GetStyle().WindowPadding);
    ImGui::Dummy(ImVec2(coverSize, coverSize));

    ImGui::SameLine();

    // ===== 右侧 =====
    ImGui::BeginGroup();
    ImVec2 startPos = ImGui::GetCursorPos();
    if (hasOthers)
    {
        ImVec2 buttonSize = ImVec2(30.0f, 30.0f);
        ImVec2 barSize = ImVec2(buttonSize.x * 3 + ImGui::GetStyle().ItemSpacing.x * 2, buttonSize.y);
        barPos = ImVec2(startPos.x + ImGui::GetContentRegionAvail().x * 0.5f - barSize.x * 0.5f, (height - barSize.y) * 0.5f);
        ImGui::SetCursorPos(barPos);
        RenderPlaybackBar();
    }
    else
    {

        ImVec2 msgSize = ImGui::CalcTextSize(u8"该音频不支持暂停/跳转");
        ImVec2 msgPos = ImVec2(startPos.x + ImGui::GetContentRegionAvail().x * 0.5f - msgSize.x * 0.5f, (height - msgSize.y) * 0.5f);
        ImGui::SetCursorPos(msgPos);
        ImGui::BeginDisabled();
        ImGuiStd::TextShadowWrapped(u8"该音频不支持暂停/跳转");
        ImGui::EndDisabled();
    }

    ImGui::EndGroup();

}

void MusicInfoItem::DrawContent()
{
    if (closed)
    {
        isEnabled = false;
        closed = false;
    }
    if (!hasMedia)
    {
        return;
    }

    {
        //std::lock_guard<std::mutex> lock(imageMutex);

        if (pendingImage && pendingImage->ready)
        {
            // 释放旧纹理
            if (coverTexture.id)
            {
                glDeleteTextures(1, &coverTexture.id);
                coverTexture.id = 0;
            }

            GLuint tex;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA,
                pendingImage->width,
                pendingImage->height,
                0, GL_RGBA, GL_UNSIGNED_BYTE,
                pendingImage->rgba.data()
            );

            glBindTexture(GL_TEXTURE_2D, 0);

            coverTexture.id = tex;
            coverTexture.width = pendingImage->width;
            coverTexture.height = pendingImage->height;

            pendingImage.reset(); // 消费完成
        }
    }


    coverSize = height - 2 * ImGui::GetStyle().WindowPadding.y;

    ImGui::BeginGroup();

    // ===== 左侧封面 =====
    if (coverTexture.id)
    {
        //ImGui::Image(coverTexture.id, ImVec2(coverSize, coverSize));
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::GetWindowDrawList()->AddImageRounded(
            (ImTextureID)coverTexture.id,
            p,
            ImVec2(p.x + coverSize, p.y + coverSize),
            ImVec2(0, 0),
            ImVec2(1, 1),
            IM_COL32_WHITE,
            customCoverRound ? coverRounding : ImGui::GetStyle().WindowRounding
        );
    }
    ImGui::Dummy(ImVec2(coverSize, coverSize));

    ImGui::SameLine();

    // ===== 右侧信息 =====
    ImGui::BeginGroup();
    ImGui::PushFont(opengl_hook::gui.iconFont);
    // 右上角 icon
    ImGui::SameLine();
    float iconWidth = ImGui::GetFontSize();

    ImGui::SetCursorPosX(width - iconWidth - ImGui::GetStyle().WindowPadding.x);
    ImGuiStd::TextShadow(paused ? u8"\uE019" : u8"\uE027"); // iconfont 音乐图标
    ImGui::PopFont();

    float titleHeight = ImGui::CalcTextSize(StringConverter::WstringToUtf8(title).c_str()).y;

    float artistHeight = titleHeight * 0.8f;

    float allHeight = titleHeight + artistHeight + ImGui::GetStyle().ItemSpacing.y;

    // 歌名（居中偏上）
    ImGui::SetCursorPosY(height * 0.5f - allHeight * 0.5f);
    ImGuiStd::TextShadowEllipsis(StringConverter::WstringToUtf8(title).c_str(), ImGui::GetContentRegionAvail().x - iconWidth);

    ImGui::PushFont(opengl_hook::gui.font, ImGui::GetFontSize() * 0.8f);
    float sourceWidth = ImGui::CalcTextSize(StringConverter::WstringToUtf8(source).c_str()).x;
    float sourceHeight = ImGui::CalcTextSize(StringConverter::WstringToUtf8(source).c_str()).y;
    // 作者
    ImGui::BeginDisabled();
    ImGuiStd::TextShadowEllipsis(StringConverter::WstringToUtf8(artist).c_str(), ImGui::GetContentRegionAvail().x - sourceWidth);
    // 右下角来源
    ImGui::SetCursorPos(ImVec2(width - sourceWidth - ImGui::GetStyle().WindowPadding.x, height - sourceHeight - ImGui::GetStyle().WindowPadding.y));

    ImGuiStd::TextShadow(StringConverter::WstringToUtf8(source).c_str());
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::EndGroup();

}

void MusicInfoItem::DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth)
{
    float bigItemWidth = centerX * 2.0f - bigPadding * 4.0f;
    ImGui::PushItemWidth(itemWidth);
    ImGui::SetCursorPosX(bigPadding);
    ImGui::Checkbox(u8"自定义封面圆角", &customCoverRound);
    ImGui::SameLine();
    ImGui::PushItemWidth(itemWidth);
    ImGui::SetCursorPosX(bigPadding + centerX);
    ImGui::Checkbox(u8"无音频播放时隐藏默认界面", &hideWhenNoMedia);

    if (customCoverRound)
    {
        ImGui::PushItemWidth(bigItemWidth);
        ImGui::SetCursorPosX(bigPadding);
        ImGui::SliderFloat(u8"封面圆角半径", &coverRounding, 0.0f, 15.0f, "%.1f");
    }


    DrawWindowSettings(bigPadding, centerX, itemWidth);
}

void MusicInfoItem::Load(const nlohmann::json& j)
{
    LoadItem(j);
    LoadWindow(j);
    if (j.contains("customCoverRound")) customCoverRound = j.at("customCoverRound").get<bool>();
    if (j.contains("coverRounding")) coverRounding = j.at("coverRounding").get<float>();
    if (j.contains("hideWhenNoMedia")) hideWhenNoMedia = j.at("hideWhenNoMedia").get<bool>();
}

void MusicInfoItem::Save(nlohmann::json& j) const
{
    SaveItem(j);
    SaveWindow(j);
    j["customCoverRound"] = customCoverRound;
    j["coverRounding"] = coverRounding;
    j["hideWhenNoMedia"] = hideWhenNoMedia;
}

void MusicInfoItem::ShutDown()
{
    if (coverTexture.id)
    {
        glDeleteTextures(1, &coverTexture.id);
        coverTexture.id = 0;
    }
}
