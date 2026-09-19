#include "OverlayPersistence.h"
#include "FileUtils.h"
#include <algorithm>
#include <cstdio>
#include <ctime>

std::string OverlayPersistence::GetConfigDir()
{
    return FileUtils::appDataPath + "\\config";
}

std::string OverlayPersistence::GetImageDir()
{
    return GetConfigDir() + "\\images";
}

std::string OverlayPersistence::GetImagePath(const std::string& fileName)
{
    return GetImageDir() + "\\" + fileName;
}

std::string OverlayPersistence::CopyImageToBackup(const std::string& sourcePath)
{
    // Generate unique file name
    static int counter = 0;
    std::string ext;
    size_t dot = sourcePath.find_last_of('.');
    if (dot != std::string::npos) ext = sourcePath.substr(dot);
    else ext = ".png";

    char buf[64];
    sprintf_s(buf, "%08x", (unsigned int)time(nullptr) + counter++);
    std::string destName = std::string(buf) + ext;
    std::string destPath = GetImagePath(destName);

    FILE* src = fopen(sourcePath.c_str(), "rb");
    if (!src) return "";
    FILE* dst = fopen(destPath.c_str(), "wb");
    if (!dst) { fclose(src); return ""; }

    unsigned char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0)
        fwrite(buffer, 1, n, dst);

    fclose(src);
    fclose(dst);
    return destName;
}
