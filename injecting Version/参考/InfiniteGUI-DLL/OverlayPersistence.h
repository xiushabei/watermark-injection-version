#pragma once
#include <string>
#include <vector>

class OverlayPersistence {
public:
    static std::string GetConfigDir();
    static std::string GetImageDir();
    static std::string GetImagePath(const std::string& fileName);
    static std::string CopyImageToBackup(const std::string& sourcePath);
};
