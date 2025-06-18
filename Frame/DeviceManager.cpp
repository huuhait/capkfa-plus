#include "DeviceManager.h"
#include "../Utils.h"

DeviceManager::DeviceManager() {
    // Initialize DXGI factory
    ComPtr<IDXGIFactory1> factory;
    CheckHRESULT(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

    // Initialize adapter
    ComPtr<IDXGIAdapter1> adapter;
    CheckHRESULT(factory->EnumAdapters1(0, &adapter), "EnumAdapters1");

    // Initialize output
    ComPtr<IDXGIOutput> output;
    CheckHRESULT(adapter->EnumOutputs(0, &output), "EnumOutputs");
    CheckHRESULT(output->QueryInterface(IID_PPV_ARGS(&output1_)), "QueryInterface IDXGIOutput1");

    // Initialize D3D11 device and context
    D3D_FEATURE_LEVEL featureLevel;
    CheckHRESULT(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &device_, &featureLevel, &context_),
                "D3D11CreateDevice");
}