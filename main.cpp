#include "App.h"
#include <boost/di.hpp>
#include <iostream>
#include <windows.h>

#include "Frame/FrameCapturer.h"
#include "Frame/FrameHandler.h"

namespace di = boost::di;

int main() {
    try {
        // Create and store DI injector
        const auto injector = di::make_injector(
            di::bind<DeviceManager>().to<DeviceManager>(),
            di::bind<FrameSlot>().to(std::make_shared<FrameSlot>()),
            di::bind<LicenseClient>().to<LicenseClient>(),
            di::bind<UINT>().to(0U), // Output index
            di::bind<FrameCapturer>().to<FrameCapturer>(),
            di::bind<FrameHandler>().to<FrameHandler>()
        );

        // Resolve App
        auto app = injector.create<std::shared_ptr<App>>();

        if (!app->Start("user_id")) { // Replace with actual user ID
            std::cerr << "Application failed to start" << std::endl;
            return 1;
        }

        while (!(GetAsyncKeyState('Q') & 0x8000)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        app->Stop();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}