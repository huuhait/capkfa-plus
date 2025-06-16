#include "FrameCapturer.h"
#include "Utils.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <windows.h>

// Link required libraries
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "comsuppw.lib")

int GetMonitorRefreshRate(ComPtr<IDXGIOutput> output) {
    std::vector<DXGI_MODE_DESC> modes;
    UINT numModes = 0;
    output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &numModes, nullptr);
    modes.resize(numModes);
    CheckHRESULT(output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &numModes, modes.data()), "GetDisplayModeList");

    int maxRefreshRate = 60; // Default fallback
    for (const auto& mode : modes) {
        int refreshRate = mode.RefreshRate.Numerator / mode.RefreshRate.Denominator;
        if (refreshRate > maxRefreshRate) {
            maxRefreshRate = refreshRate;
        }
    }
    std::cout << "Detected max refresh rate: " << maxRefreshRate << "Hz" << std::endl;
    return maxRefreshRate;
}

FrameCapturer::FrameCapturer(ComPtr<IDXGIOutput1> output1, ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context, UINT outputIndex)
    : output1_(output1), device_(device), context_(context), outputIndex_(outputIndex) {
    // Validate output dimensions
    DXGI_OUTPUT_DESC outputDesc;
    CheckHRESULT(output1_->GetDesc(&outputDesc), "GetDesc");
    int width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    int height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    if (width < 500 || height < 500) {
        throw std::runtime_error("Output " + std::to_string(outputIndex_) + " too small for 500x500 capture");
    }

    // Get refresh rate and set dynamic timeout
    ComPtr<IDXGIOutput> output;
    CheckHRESULT(output1_->QueryInterface(IID_PPV_ARGS(&output)), "QueryInterface IDXGIOutput");
    refreshRate_ = GetMonitorRefreshRate(output);
    timeoutMs_ = std::max(1U, static_cast<UINT>(1000 / refreshRate_)); // Dynamic timeout
    std::cout << "Output " << outputIndex_ << ": " << refreshRate_ << "Hz, timeout " << timeoutMs_ << "ms" << std::endl;

    // Create staging texture
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

    // Initialize OpenCV UMat
    frame_ = cv::UMat(500, 500, CV_8UC4);
}

FrameCapturer::~FrameCapturer() {}

void FrameCapturer::StartCapture() {
    int frameCount = 0;
    auto lastTime = std::chrono::steady_clock::now();
    bool capturing = true;
    std::vector<float> frameTimes; // For variance
    ComPtr<IDXGIOutputDuplication> duplication;

    // Cache duplication object
    HRESULT hr = output1_->DuplicateOutput(device_.Get(), &duplication);
    if (FAILED(hr)) {
        std::cerr << "Initial DuplicateOutput failed: " << _com_error(hr).ErrorMessage() << std::endl;
        return;
    }

    while (capturing) {
        auto frameStart = std::chrono::steady_clock::now();

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        auto acquireStart = std::chrono::steady_clock::now();
        hr = duplication->AcquireNextFrame(timeoutMs_, &frameInfo, &desktopResource);
        auto acquireEnd = std::chrono::steady_clock::now();
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Reinitialize duplication
            hr = output1_->DuplicateOutput(device_.Get(), &duplication);
            if (FAILED(hr)) {
                std::cerr << "Reinit DuplicateOutput failed: " << _com_error(hr).ErrorMessage() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Exponential backoff
                continue;
            }
            continue;
        }
        if (hr == DXGI_ERROR_DEVICE_REMOVED) {
            std::cerr << "Device removed: " << _com_error(hr).ErrorMessage() << std::endl;
            return;
        }
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            continue; // Skip sleep to reduce latency
        }
        if (FAILED(hr)) continue;

        // Log Acquire time if high
        auto acquireTime = std::chrono::duration_cast<std::chrono::microseconds>(acquireEnd - acquireStart).count() / 1000.0;
        if (acquireTime > 1.0) {
            // std::cout << "High Acquire time: " << acquireTime << "ms" << std::endl;
        }

        ComPtr<ID3D11Texture2D> desktopTexture;
        CheckHRESULT(desktopResource->QueryInterface(IID_PPV_ARGS(&desktopTexture)), "QueryInterface Texture2D");

        // Validate desktop texture format
        D3D11_TEXTURE2D_DESC desc;
        desktopTexture->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            std::cerr << "Unexpected desktop texture format: " << desc.Format << std::endl;
            duplication->ReleaseFrame();
            continue;
        }

        // Define 500x500 centered region
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

        // Copy 500x500 region
        context_->CopySubresourceRegion(stagingTexture_.Get(), 0, 0, 0, 0, desktopTexture.Get(), 0, &srcBox);

        D3D11_MAPPED_SUBRESOURCE mapped;
        CheckHRESULT(context_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map");

        // Copy directly to UMat data
        uint8_t* src = (uint8_t*)mapped.pData;
        uint8_t* dst = frame_.getMat(cv::ACCESS_WRITE).data;
        if (mapped.RowPitch == 500 * 4) {
            memcpy(dst, src, 500 * 500 * 4);
        } else {
            for (int i = 0; i < 500; ++i) {
                memcpy(dst + i * 500 * 4, src + i * mapped.RowPitch, 500 * 4);
            }
        }
        context_->Unmap(stagingTexture_.Get(), 0);

        if (frame_.empty()) {
            std::cerr << "Empty frame for output " << outputIndex_ << std::endl;
            duplication->ReleaseFrame();
            continue;
        }

        // Remove artificial frame cap to reduce stalls
        auto frameEnd = std::chrono::steady_clock::now();
        auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count();
        frameTimes.push_back(frameDuration / 1000.0f);

        // Check for 'Q' key to exit
        if (GetAsyncKeyState('Q') & 0x8000) {
            capturing = false;
        }

        // Calculate FPS and variance
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