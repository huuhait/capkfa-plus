#include "FrameCapturer.h"
#include <opencv2/core/ocl.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <windows.h>
#include "FrameSlot.h"
#include "DeviceManager.h"
#include "../Utils.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "comsuppw.lib")

int GetMonitorRefreshRate(ComPtr<IDXGIOutput> output) {
    std::vector<DXGI_MODE_DESC> modes;
    UINT numModes = 0;
    output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &numModes, nullptr);
    modes.resize(numModes);
    CheckHRESULT(output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &numModes, modes.data()), "GetDisplayModeList");

    int maxRefreshRate = 60;
    for (const auto& mode : modes) {
        int refreshRate = mode.RefreshRate.Numerator / mode.RefreshRate.Denominator;
        if (refreshRate > maxRefreshRate) {
            maxRefreshRate = refreshRate;
        }
    }
    std::cout << "Detected max refresh rate: " << maxRefreshRate << "Hz" << std::endl;
    return maxRefreshRate;
}

FrameCapturer::FrameCapturer(const DeviceManager& deviceManager, UINT outputIndex, std::shared_ptr<FrameSlot> frameSlot)
    : output1_(deviceManager.GetOutput()), device_(deviceManager.GetDevice()), context_(deviceManager.GetContext()),
      outputIndex_(outputIndex), frameSlot_(frameSlot), isCapturing_(false) {
    // Rest of the constructor remains unchanged
    DXGI_OUTPUT_DESC outputDesc;
    CheckHRESULT(output1_->GetDesc(&outputDesc), "GetDesc");
    int width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    int height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    std::cout << "Monitor size: " << width << "x" << height << ", capturing region: ["
              << (width/2 - 250) << "," << (height/2 - 250) << "] to ["
              << (width/2 + 250) << "," << (height/2 + 250) << "]" << std::endl;

    if (width < 500 || height < 500) {
        throw std::runtime_error("Output " + std::to_string(outputIndex_) + " too small for 500x500 capture");
    }

    ComPtr<IDXGIOutput> output;
    CheckHRESULT(output1_->QueryInterface(IID_PPV_ARGS(&output)), "QueryInterface IDXGIOutput");
    refreshRate_ = GetMonitorRefreshRate(output);
    timeoutMs_ = std::max(1U, static_cast<UINT>(1000 / refreshRate_));
    std::cout << "Output " << outputIndex_ << ": " << refreshRate_ << "Hz, timeout " << timeoutMs_ << "ms" << std::endl;

    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = 500;
    stagingDesc.Height = 500;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CheckHRESULT(device_->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture_), "CreateTexture2D");

    frame_ = cv::UMat(500, 500, CV_8UC4);
}

FrameCapturer::~FrameCapturer() {
    StopCapture();
}

void FrameCapturer::StartCapture() {
    if (!isCapturing_) {
        isCapturing_ = true;
        captureThread_ = std::thread(&FrameCapturer::CaptureLoop, this);
    }
}

void FrameCapturer::StopCapture() {
    if (isCapturing_) {
        isCapturing_ = false;
        if (captureThread_.joinable()) {
            captureThread_.join();
        }
    }
}

void FrameCapturer::CaptureLoop() {
    int frameCount = 0;
    auto lastTime = std::chrono::steady_clock::now();
    std::vector<float> frameTimes;
    ComPtr<IDXGIOutputDuplication> duplication;

    HRESULT hr = output1_->DuplicateOutput(device_.Get(), &duplication);
    if (FAILED(hr)) {
        std::cerr << "Initial DuplicateOutput failed: " << _com_error(hr).ErrorMessage() << std::endl;
        isCapturing_ = false;
        return;
    }

    while (isCapturing_) {
        auto frameStart = std::chrono::steady_clock::now();

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        hr = duplication->AcquireNextFrame(timeoutMs_, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            hr = output1_->DuplicateOutput(device_.Get(), &duplication);
            if (FAILED(hr)) {
                std::cerr << "Reinit DuplicateOutput failed: " << _com_error(hr).ErrorMessage() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            continue;
        }
        if (hr == DXGI_ERROR_DEVICE_REMOVED) {
            std::cerr << "Device removed: " << _com_error(hr).ErrorMessage() << std::endl;
            isCapturing_ = false;
            return;
        }
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }
        if (FAILED(hr)) continue;

        ComPtr<ID3D11Texture2D> desktopTexture;
        CheckHRESULT(desktopResource->QueryInterface(IID_PPV_ARGS(&desktopTexture)), "QueryInterface Texture2D");

        D3D11_TEXTURE2D_DESC desc;
        desktopTexture->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            std::cerr << "Unexpected desktop texture format: " << desc.Format << std::endl;
            duplication->ReleaseFrame();
            continue;
        }

        int x_center = desc.Width / 2;
        int y_center = desc.Height / 2;
        D3D11_BOX srcBox = {
            static_cast<UINT>(x_center - 250),
            static_cast<UINT>(y_center - 250),
            0,
            static_cast<UINT>(x_center + 250),
            static_cast<UINT>(y_center + 250),
            1
        };

        context_->CopySubresourceRegion(stagingTexture_.Get(), 0, 0, 0, 0, desktopTexture.Get(), 0, &srcBox);

        D3D11_MAPPED_SUBRESOURCE mapped;
        CheckHRESULT(context_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map");

        if (mapped.pData == nullptr) {
            std::cerr << "Mapped data is null" << std::endl;
            context_->Unmap(stagingTexture_.Get(), 0);
            duplication->ReleaseFrame();
            continue;
        }

        uint8_t* src = (uint8_t*)mapped.pData;
        cv::Mat temp(500, 500, CV_8UC4); // Temporary Mat for CPU copy
        uint8_t* dst = temp.data;
        bool allZero = true;
        if (mapped.RowPitch == 500 * 4) {
            memcpy(dst, src, 500 * 500 * 4);
            for (size_t i = 0; i < 500 * 500 * 4; ++i) {
                if (dst[i] != 0) {
                    allZero = false;
                    break;
                }
            }
        } else {
            for (int i = 0; i < 500; ++i) {
                memcpy(dst + i * 500 * 4, src + i * mapped.RowPitch, 500 * 4);
            }
            for (size_t i = 0; i < 500 * 500 * 4; ++i) {
                if (dst[i] != 0) {
                    allZero = false;
                    break;
                }
            }
        }
        context_->Unmap(stagingTexture_.Get(), 0);

        if (allZero) {
            std::cerr << "Captured frame data is all zeros" << std::endl;
            duplication->ReleaseFrame();
            continue;
        }

        temp.copyTo(frame_); // Copy to UMat
        cv::ocl::finish(); // Synchronize OpenCL

        if (frame_.empty()) {
            std::cerr << "Captured frame is empty" << std::endl;
            duplication->ReleaseFrame();
            continue;
        }

        frameSlot_->StoreFrame(frame_);

        auto frameEnd = std::chrono::steady_clock::now();
        auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count();
        frameTimes.push_back(frameDuration / 1000.0f);

        if (GetAsyncKeyState('Q') & 0x8000) {
            isCapturing_ = false;
        }

        frameCount++;
        auto currentTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();
        if (duration >= 1000) {
            float fps = (float)frameCount * 1000.0f / duration;
            float mean = 0.0f;
            for (float t : frameTimes) mean += t;
            mean /= frameTimes.size();
            float variance = 0.0f;
            for (float t : frameTimes) variance += (t - mean) * (t - mean);
            variance /= frameTimes.size();
            std::cout << "Output " << outputIndex_ << " FPS: " << fps << ", Frame Time Variance: " << variance << "ms^2" << std::endl;
            frameCount = 0;
            frameTimes.clear();
            lastTime = currentTime;
        }

        duplication->ReleaseFrame();
    }
}