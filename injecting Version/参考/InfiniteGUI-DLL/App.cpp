#include "App.h"
#include "HttpClient.h"
#include <nlohmann/json.hpp>
#include <string>
#include <Windows.h>

bool App::CheckUpdate()
{
    std::string response;
    bool ok = HttpClient::HttpGet(versionUrl, response);

    if (ok)
    {
        try {
            auto j = nlohmann::json::parse(response);
            cloudVersion.major = j["version"]["major"].get<int>();
            cloudVersion.minor = j["version"]["minor"].get<int>();
            cloudVersion.build = j["version"]["build"].get<int>();
        }
        catch (...) {
            MessageBox(NULL, L"�汾��Ϣ����ʧ�ܣ�����ϵ����", L"��ʾ", MB_OK);
        }
    }
    else {
        MessageBox(NULL, L"��������ʧ�ܣ�������������", L"��ʾ", MB_OK);
    }
    long long cloudNum = cloudVersion.major * 100000000 + cloudVersion.minor * 10000 + cloudVersion.build;
    long long appNum = appVersion.major * 100000000 + appVersion.minor * 10000 + appVersion.build;
    if (cloudNum > appNum)
    {
        return false;
    }
    return true;
}
