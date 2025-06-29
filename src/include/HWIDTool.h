#pragma once
#include <string>

class HWIDTool {
public:
    static std::string GetHWID();
private:
    static std::string GetWmiValue(const std::wstring& className, const std::wstring& property);
    static std::string GetAllDiskSerials();
    static std::string GetAllMacs();
    static std::string GetCurrentUserSid();
    static std::string GetNvidiaGpuUuid();
    static std::string ComputeSha256(const std::string& input);
};
