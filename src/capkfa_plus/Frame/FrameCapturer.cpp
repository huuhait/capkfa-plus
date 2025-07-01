#include "FrameCapturer.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <windows.h>
#include <opencv2/imgproc.hpp>
#include "Obfuscate.h"
#include "Utils.h"
#include "FrameSlot.h"
#include "DeviceManager.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "comsuppw.lib")

FrameCapturer::FrameCapturer(spdlog::logger& logger, const DeviceManager& deviceManager, UINT outputIndex,
                             std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher)
    : logger_(logger), output1_(deviceManager.GetOutput()), device_(deviceManager.GetDevice()),
      context_(deviceManager.GetContext()), outputIndex_(outputIndex), frameSlot_(frameSlot),
      keyWatcher_(keyWatcher), isCapturing_(false), captureWidth_(0), captureHeight_(0),
      offsetX_(0), offsetY_(0), refreshRate_(0), timeoutMs_(0) {}

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
    int width = config.capture().size();
    int height = config.capture().size();

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
    timeoutMs_ = 1;

    stagingTexture_.Reset();
    CreateStagingTexture();

    logger_.info("Capture config set: size {}x{}, centered offset ({}, {}), {}Hz, timeout {}ms",
                 captureWidth_, captureHeight_, offsetX_, offsetY_, refreshRate_, timeoutMs_);

    StartCapture();
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
        stagingTexture_.Reset();
    }
}

void FrameCapturer::CaptureLoop() {
    // Obfuscate error messages
    constexpr auto obfErrDuplFailed = $o("Initial DuplicateOutput failed: ");
    constexpr auto obfErrDevRemoved = $o("Device removed: ");
    constexpr auto obfErrBadFormat = $o("Unexpected desktop texture format: ");
    constexpr auto obfErrNullData = $o("Mapped data is null");
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
            auto frameStart = std::chrono::steady_clock::now();

            // VM block for DXGI operations
            HRESULT hr = S_OK;
            ComPtr<IDXGIResource> desktopResource;
            ComPtr<ID3D11Texture2D> desktopTexture;
            DXGI_OUTDUPL_FRAME_INFO frameInfo;

            // Generate random key for bytecode randomization
            uint8_t key = static_cast<uint8_t>(rand() % 256);

            // Step 1: DuplicateOutput
            std::array<uint8_t, 1> bytecode_dupl = {static_cast<uint8_t>(1 ^ key)};
            auto vm_block_dupl = [&](uint8_t instr) {
                if (instr == 1) {
                    hr = duplication ? S_OK : output1_->DuplicateOutput(device_.Get(), &duplication);
                    if (FAILED(hr)) {
                        std::cerr << $d_inline(obfErrDuplFailed) << _com_error(hr).ErrorMessage() << std::endl;
                        isCapturing_ = false;
                    }
                }
            };
            OBF_VM_FUNCTION_DYNAMIC(bytecode_dupl, key, vm_block_dupl);
            if (FAILED(hr)) continue;

            // Step 2: AcquireNextFrame
            std::array<uint8_t, 1> bytecode_acquire = {static_cast<uint8_t>(2 ^ key)};
            auto vm_block_acquire = [&](uint8_t instr) {
                if (instr == 2) {
                    hr = $call(duplication.Get(), obfAcquireNextFrame, timeoutMs_, &frameInfo, &desktopResource);
                    if (hr == DXGI_ERROR_ACCESS_LOST) {
                        duplication.Reset();
                        hr = DXGI_ERROR_ACCESS_LOST;
                    } else if (hr == DXGI_ERROR_DEVICE_REMOVED) {
                        std::cerr << $d_inline(obfErrDevRemoved) << _com_error(hr).ErrorMessage() << std::endl;
                        isCapturing_ = false;
                    }
                }
            };
            OBF_VM_FUNCTION_DYNAMIC(bytecode_acquire, key, vm_block_acquire);
            if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_WAIT_TIMEOUT || FAILED(hr)) {
                continue;
            }

            // Step 3: QueryInterface
            std::array<uint8_t, 1> bytecode_query = {static_cast<uint8_t>(3 ^ key)};
            auto vm_block_query = [&](uint8_t instr) {
                if (instr == 3) {
                    CheckHRESULT($call2(desktopResource.Get(), obfQueryInterface, IID_PPV_ARGS(&desktopTexture)), std::string($d_inline(obfQueryInterfaceTexture2D)));
                }
            };
            OBF_VM_FUNCTION_DYNAMIC(bytecode_query, key, vm_block_query);

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

            uint8_t* src = static_cast<uint8_t*>(mapped.pData);
            if (mapped.RowPitch < captureWidth_ * 4) {
                std::cerr << "Invalid RowPitch: " << mapped.RowPitch
                          << ", expected >= " << captureWidth_ * 4 << std::endl;
                context_->Unmap(stagingTexture_.Get(), 0);
                duplication->ReleaseFrame();
                continue;
            }

            bool allZero = true;
            for (size_t y = 0; y < captureHeight_ && allZero; ++y) {
                for (size_t x = 0; x < captureWidth_ * 4; x += 4) {
                    size_t index = y * mapped.RowPitch + x;
                    if (index >= captureHeight_ * mapped.RowPitch) {
                        std::cerr << "Out-of-bounds access: index=" << index
                                  << ", max=" << captureHeight_ * mapped.RowPitch << std::endl;
                        context_->Unmap(stagingTexture_.Get(), 0);
                        duplication->ReleaseFrame();
                        continue;
                    }
                    if (src[index] || src[index + 1] || src[index + 2] || src[index + 3]) {
                        allZero = false;
                        break;
                    }
                }
            }

            if (allZero || !src || captureWidth_ <= 0 || captureHeight_ <= 0 || mapped.RowPitch < captureWidth_ * 4) {
                if (allZero) {
                    std::cerr << "All-zero frame detected" << std::endl;
                } else {
                    std::cerr << "Invalid frame parameters: src=" << (void*)src
                              << ", width=" << captureWidth_ << ", height=" << captureHeight_
                              << ", pitch=" << mapped.RowPitch << std::endl;
                }
                context_->Unmap(stagingTexture_.Get(), 0);
                duplication->ReleaseFrame();
                continue;
            }

            try {
                // Create UMat with standard constructor
                cv::UMat bgra(captureHeight_, captureWidth_, CV_8UC4, cv::USAGE_ALLOCATE_DEVICE_MEMORY);
                // Copy mapped data to UMat
                cv::Mat temp(captureHeight_, captureWidth_, CV_8UC4, mapped.pData, mapped.RowPitch);
                temp.copyTo(bgra);
                cv::UMat rgb;
                cv::cvtColor(bgra, rgb, cv::COLOR_BGRA2BGR);
                auto frame = std::make_shared<Frame>(rgb, captureWidth_, captureHeight_);
                $call(frameSlot_.get(), $om(StoreFrame, FrameSlot, void), frame);
            } catch (const std::exception& e) {
                std::cerr << "Failed to create Frame: " << e.what() << std::endl;
                context_->Unmap(stagingTexture_.Get(), 0);
                duplication->ReleaseFrame();
                continue;
            }

            context_->Unmap(stagingTexture_.Get(), 0);
            duplication->ReleaseFrame();

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
                logger_.info("FrameCapturer FPS: {:.2f}, Frame Time Variance: {:.2f}ms", fps, variance);
                frameCount = 0;
                frameTimes.clear();
                lastTime = currentTime;
            }
        }
    } catch (const std::exception& e) {
        logger_.error("{} {}", $d_inline(obfErrCrash), e.what());
        isCapturing_ = false;
    } catch (...) {
        logger_.error($d_inline(obfErrUnknown));
        isCapturing_ = false;
    }
}
