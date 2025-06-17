#include "FrameCapturer.h"
#include "FrameHandler.h"
#include "FrameSlot.h"
#include "Utils.h"
#include <dxgi1_2.h>
#include <d3d11.h>
#include <iostream>
#include <wrl/client.h>
#include <memory>

using Microsoft::WRL::ComPtr;

int main() {
    try {
        ComPtr<IDXGIFactory1> factory;
        CheckHRESULT(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL featureLevel;
        CheckHRESULT(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &device, &featureLevel, &context),
                     "D3D11CreateDevice");

        ComPtr<IDXGIAdapter1> adapter;
        CheckHRESULT(factory->EnumAdapters1(0, &adapter), "EnumAdapters1");
        ComPtr<IDXGIOutput> output;
        CheckHRESULT(adapter->EnumOutputs(0, &output), "EnumOutputs");
        ComPtr<IDXGIOutput1> output1;
        CheckHRESULT(output->QueryInterface(IID_PPV_ARGS(&output1)), "QueryInterface IDXGIOutput1");

        auto frameSlot = std::make_shared<FrameSlot>();
        FrameCapturer capturer(output1, device, context, 0, frameSlot);
        FrameHandler handler(frameSlot, 0);

        capturer.StartCapture();
        handler.Start();

        while (!(GetAsyncKeyState('Q') & 0x8000)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        capturer.StopCapture();
        handler.Stop();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    cv::destroyAllWindows();
    return 0;
}