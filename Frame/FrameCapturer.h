#ifndef FRAME_CAPTURER_H
#define FRAME_CAPTURER_H

#include <dxgi1_2.h>
#include <d3d11.h>
#include <opencv2/opencv.hpp>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <memory>
#include "DeviceManager.h"
#include "FrameSlot.h"

using Microsoft::WRL::ComPtr;

class FrameCapturer {
public:
    FrameCapturer(const DeviceManager& deviceManager, UINT outputIndex, std::shared_ptr<FrameSlot> frameSlot);
    ~FrameCapturer();
    void StartCapture();
    void StopCapture();

private:
    void CaptureLoop();

    ComPtr<IDXGIOutput1> output1_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> stagingTexture_;
    cv::UMat frame_;
    UINT outputIndex_;
    int refreshRate_;
    UINT timeoutMs_;
    std::shared_ptr<FrameSlot> frameSlot_;
    std::thread captureThread_;
    std::atomic<bool> isCapturing_;
};

#endif