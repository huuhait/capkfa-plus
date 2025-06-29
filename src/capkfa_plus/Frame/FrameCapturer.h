#ifndef FRAME_CAPTURER_H
#define FRAME_CAPTURER_H
#define _WINSOCKAPI_

#include <winsock2.h>
#include <Windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <memory>
#include "DeviceManager.h"
#include "FrameSlot.h"
#include "../Movement/KeyWatcher.h"

using Microsoft::WRL::ComPtr;

class FrameCapturer {
public:
    FrameCapturer(const DeviceManager& deviceManager, UINT outputIndex, std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher);
    ~FrameCapturer();
    void StartCapture();
    void StopCapture();
    void SetConfig(const ::capkfa::RemoteConfig& config);

private:
    void CaptureLoop();
    void CreateStagingTexture();

    ComPtr<IDXGIOutput1> output1_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> stagingTexture_;
    UINT outputIndex_;
    int refreshRate_;
    UINT timeoutMs_;
    int captureWidth_;
    int captureHeight_;
    int offsetX_;
    int offsetY_;
    std::shared_ptr<FrameSlot> frameSlot_;
    std::shared_ptr<KeyWatcher> keyWatcher_;
    std::thread captureThread_;
    std::atomic<bool> isCapturing_;
};

#endif
