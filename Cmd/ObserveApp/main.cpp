#include "App.h"
#include <boost/di.hpp>
#include <iostream>
#include <windows.h>
#include "../../Frame/FrameCapturer.h"
#include "../../include/Obfuscate.h"
#include "../../Movement/CommanderClient.h"
#include "../../Movement/KeyWatcher.h"
#include "../../Movement/Km.h"

namespace di = boost::di;

int main() {
    try {
        // Create and store DI injector
        const auto injector = di::make_injector(
            di::bind<CommanderClient>().to(std::make_shared<CommanderClient>()),
            di::bind<DeviceManager>().to<DeviceManager>(),
            di::bind<FrameSlot>().to(std::make_shared<FrameSlot>()),
            di::bind<LicenseClient>().to<LicenseClient>(),
            di::bind<UINT>().to(0U), // Output index
            di::bind<FrameCapturer>().to<FrameCapturer>(),
            di::bind<Colorbot>().to<Colorbot>(),
            di::bind<ObjectDetectionManager>().to<ObjectDetectionManager>(),
            di::bind<YoloModel>().to<YoloModel>(),
            di::bind<CudaModel>().to<CudaModel>(),
            di::bind<LogicManager>().to<LogicManager>(),
            di::bind<Km>().to<Km>(),
            di::bind<KeyWatcher>().to<KeyWatcher>()
        );

        auto app = injector.create<std::shared_ptr<App>>();

        auto start_fn = $om(Start, App, bool);
        if (!$call(app.get(), start_fn)) {
            std::cerr << "Application failed to start" << std::endl;
            return 1;
        }

        while (true) {
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