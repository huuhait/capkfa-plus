#include "FrameCapturer.h"
#include <opencv2/core/ocl.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <windows.h>
#include "../include/Obfuscate.h"
#include "FrameSlot.h"
#include "DeviceManager.h"
#include "../include/Utils.h"

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

FrameCapturer::FrameCapturer(const DeviceManager& deviceManager, UINT outputIndex, std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher)
    : output1_(deviceManager.GetOutput()), device_(deviceManager.GetDevice()), context_(deviceManager.GetContext()),
      outputIndex_(outputIndex), frameSlot_(frameSlot), keyWatcher_(keyWatcher), isCapturing_(false), captureWidth_(0), captureHeight_(0),
      offsetX_(0), offsetY_(0), refreshRate_(0), timeoutMs_(0) {
}

FrameCapturer::~FrameCapturer() {
    StopCapture();
}

void FrameCapturer::CreateStagingTexture() {
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = captureWidth_;
    stagingDesc.Height = captureHeight_;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CheckHRESULT(device_->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture_), "CreateTexture2D");
}

void FrameCapturer::SetConfig(const ::capkfa::RemoteConfig& config) {
    int width = config.aim().fov();
    int height = config.aim().fov();

    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid capture size: " + std::to_string(width) + "x" + std::to_string(height));
    }

    StopCapture();

    DXGI_OUTPUT_DESC outputDesc;
    CheckHRESULT(output1_->GetDesc(&outputDesc), "GetDesc");
    int monitorWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    int monitorHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    if (monitorWidth < width || monitorHeight < height) {
        throw std::runtime_error("Capture size " + std::to_string(width) + "x" + std::to_string(height) +
                                 " exceeds monitor dimensions " + std::to_string(monitorWidth) + "x" + std::to_string(monitorHeight));
    }

    captureWidth_ = width;
    captureHeight_ = height;
    offsetX_ = (monitorWidth - captureWidth_) / 2;
    offsetY_ = (monitorHeight - captureHeight_) / 2;

    ComPtr<IDXGIOutput> output;
    CheckHRESULT(output1_->QueryInterface(IID_PPV_ARGS(&output)), "QueryInterface IDXGIOutput");
    refreshRate_ = GetMonitorRefreshRate(output);
    timeoutMs_ = std::max(1U, static_cast<UINT>(1000 / refreshRate_));

    stagingTexture_.Reset();
    CreateStagingTexture();
    frame_ = cv::UMat(captureHeight_, captureWidth_, CV_8UC4);

    std::cout << "Capture config set: size " << captureWidth_ << "x" << captureHeight_
              << ", centered offset (" << offsetX_ << "," << offsetY_ << "), "
              << refreshRate_ << "Hz, timeout " << timeoutMs_ << "ms" << std::endl;

    StartCapture();
}

void FrameCapturer::StartCapture() {
    if (!isCapturing_) {
        if (frame_.empty()) {
            std::cerr << "Warning: FrameCapturer not configured. Call SetConfig to enable capture." << std::endl;
        }
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
        // Clear resources
        stagingTexture_.Reset();
        frame_ = cv::UMat();
    }
}

void FrameCapturer::CaptureLoop() {
    // Obfuscate error messages
    constexpr auto obfErrDuplFailed = $o("Initial DuplicateOutput failed: ");
    constexpr auto obfErrDevRemoved = $o("Device removed: ");
    constexpr auto obfErrBadFormat = $o("Unexpected desktop texture format: ");
    constexpr auto obfErrNullData = $o("Mapped data is null");
    constexpr auto obfErrAllZero = $o("Captured frame data is all zeros");
    constexpr auto obfErrEmptyFrame = $o("Captured frame is empty");
    constexpr auto obfErrCrash = $o("CaptureLoop crashed: ");
    constexpr auto obfErrUnknown = $o("CaptureLoop crashed: Unknown error");
    constexpr auto obfQueryInterfaceTexture2D = $o("QueryInterface Texture2D");

    auto obfAcquireNextFrame = $om(AcquireNextFrame, IDXGIOutputDuplication, HRESULT);
    auto obfQueryInterface = $om2(QueryInterface, IDXGIResource, HRESULT);
    auto obfGetDesc = $om(GetDesc, ID3D11Texture2D, void);
    auto obfMap = $om(Map, ID3D11DeviceContext, HRESULT);

    try {
        int frameCount = 0;
        auto lastTime = std::chrono::steady_clock::now();
        std::vector<float> frameTimes;
        ComPtr<IDXGIOutputDuplication> duplication;

        while (isCapturing_) {
            if (frame_.empty()) {
                continue; // Skip if not configured
            }

            if (!keyWatcher_->IsCaptureKeyDown()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto frameStart = std::chrono::steady_clock::now();

            HRESULT hr = duplication ? S_OK : output1_->DuplicateOutput(device_.Get(), &duplication);
            if (FAILED(hr)) {
                std::cerr << $d_inline(obfErrDuplFailed) << _com_error(hr).ErrorMessage() << std::endl;
                isCapturing_ = false;
                return;
            }

            DXGI_OUTDUPL_FRAME_INFO frameInfo;
            ComPtr<IDXGIResource> desktopResource;
            hr = $call(duplication.Get(), obfAcquireNextFrame, timeoutMs_, &frameInfo, &desktopResource);
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                duplication.Reset();
                continue;
            }
            if (hr == DXGI_ERROR_DEVICE_REMOVED) {
                std::cerr << $d_inline(obfErrDevRemoved) << _com_error(hr).ErrorMessage() << std::endl;
                isCapturing_ = false;
                return;
            }
            if (hr == DXGI_ERROR_WAIT_TIMEOUT || FAILED(hr)) {
                continue;
            }

            ComPtr<ID3D11Texture2D> desktopTexture;
            CheckHRESULT($call2(desktopResource.Get(), obfQueryInterface, IID_PPV_ARGS(&desktopTexture)), std::string($d_inline(obfQueryInterfaceTexture2D)));

            D3D11_TEXTURE2D_DESC desc;
            $call(desktopTexture.Get(), obfGetDesc, &desc);
            if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
                std::cerr << $d_inline(obfErrBadFormat) << desc.Format << std::endl;
                duplication->ReleaseFrame();
                continue;
            }

            D3D11_BOX srcBox = {
                static_cast<UINT>(offsetX_),
                static_cast<UINT>(offsetY_),
                0,
                static_cast<UINT>(offsetX_ + captureWidth_),
                static_cast<UINT>(offsetY_ + captureHeight_),
                1
            };

            context_->CopySubresourceRegion(stagingTexture_.Get(), 0, 0, 0, 0, desktopTexture.Get(), 0, &srcBox);

            D3D11_MAPPED_SUBRESOURCE mapped;
            CheckHRESULT($call(context_.Get(), obfMap, stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map");

            if (mapped.pData == nullptr) {
                std::cerr << $d_inline(obfErrNullData) << std::endl;
                context_->Unmap(stagingTexture_.Get(), 0);
                duplication->ReleaseFrame();
                continue;
            }

            // Obfuscate frame data validation and copying with VM
            uint8_t* src = (uint8_t*)mapped.pData;
            cv::Mat temp(captureHeight_, captureWidth_, CV_8UC4);
            uint8_t* dst = temp.data;
            bool allZero = true;
            constexpr uint8_t bytecode[] = {1, 2, 3};
            auto vm_block = [this, &temp, &allZero, src, dst, &mapped](uint8_t instr) {
                switch (instr) {
                    VM_CASE(1) {
                        if (mapped.RowPitch == captureWidth_ * 4) {
                            memcpy(dst, src, captureWidth_ * captureHeight_ * 4);
                        } else {
                            for (int i = 0; i < captureHeight_; ++i) {
                                memcpy(dst + i * captureWidth_ * 4, src + i * mapped.RowPitch, captureWidth_ * 4);
                            }
                        }
                    }
                    VM_CASE(2) {
                        for (size_t i = 0; i < captureWidth_ * captureHeight_ * 4; ++i) {
                            if (dst[i] != 0) {
                                allZero = false;
                                break;
                            }
                        }
                    }
                    VM_CASE(3) {
                        context_->Unmap(stagingTexture_.Get(), 0);
                    }
                }
            };
            OBF_VM_FUNCTION(bytecode, vm_block);

            if (allZero) {
                std::cerr << $d_inline(obfErrAllZero) << std::endl;
                duplication->ReleaseFrame();
                continue;
            }

            temp.copyTo(frame_);
            cv::ocl::finish();

            if (frame_.empty()) {
                std::cerr << $d_inline(obfErrEmptyFrame) << std::endl;
                duplication->ReleaseFrame();
                continue;
            }

            // Obfuscate FrameSlot::StoreFrame call
            auto obfStoreFrame = $om(StoreFrame, FrameSlot, void);
            $call(frameSlot_.get(), obfStoreFrame, frame_);

            auto frameEnd = std::chrono::steady_clock::now();
            auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count();
            frameTimes.push_back(frameDuration / 1000.0f);

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
                // std::cout << "Output " << outputIndex_ << " FPS: " << fps << ", Frame Time Variance: " << variance << "ms^2" << std::endl;
                frameCount = 0;
                frameTimes.clear();
                lastTime = currentTime;
            }

            duplication->ReleaseFrame();
        }
    } catch (const std::exception& e) {
        std::cerr << $d_inline(obfErrCrash) << e.what() << std::endl;
        isCapturing_ = false;
    } catch (...) {
        std::cerr << $d_inline(obfErrUnknown) << std::endl;
        isCapturing_ = false;
    }
}