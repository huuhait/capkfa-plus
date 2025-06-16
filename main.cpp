#include "FrameCapturer.h"
#include "Utils.h"
#include <dxgi1_2.h>
#include <d3d11.h>
#include <iostream>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

int main() {
    try {
        // Create DXGI factory
        ComPtr<IDXGIFactory1> factory;
        CheckHRESULT(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

        // D3D11 device and context
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL featureLevel;
        CheckHRESULT(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, &device, &featureLevel, &context),
                     "D3D11CreateDevice");

        // Test single output
        ComPtr<IDXGIAdapter1> adapter;
        CheckHRESULT(factory->EnumAdapters1(0, &adapter), "EnumAdapters1");
        ComPtr<IDXGIOutput> output;
        CheckHRESULT(adapter->EnumOutputs(0, &output), "EnumOutputs");
        ComPtr<IDXGIOutput1> output1;
        CheckHRESULT(output->QueryInterface(IID_PPV_ARGS(&output1)), "QueryInterface IDXGIOutput1");

        // Create and run capturer
        FrameCapturer capturer(output1, device, context, 0);
        capturer.StartCapture();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    cv::destroyAllWindows();
    return 0;
}