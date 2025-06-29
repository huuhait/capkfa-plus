#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager() = default;

    ComPtr<ID3D11Device> GetDevice() const { return device_; }
    ComPtr<ID3D11DeviceContext> GetContext() const { return context_; }
    ComPtr<IDXGIOutput1> GetOutput() const { return output1_; }

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutput1> output1_;
};

#endif