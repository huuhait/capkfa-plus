#include <iostream>
#include <conio.h>
#include "HWIDTool.h"
#include "../../cmake-build-debug/vcpkg_installed/x64-windows-static/include/spdlog/spdlog.h"

int main()
{
    auto logger = spdlog::default_logger();
    logger->set_level(spdlog::level::debug);
    auto hwid = HWIDTool::GetHWID();
    logger->info("HWID: {}", hwid);
    logger->info("Press any key to continue...");
    _getch();
    return 0;
}
