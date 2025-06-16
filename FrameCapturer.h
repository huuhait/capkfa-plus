#ifndef FRAME_CAPTURER_H
#define FRAME_CAPTURER_H

#include <dxgi1_2.h>
#include <d3d11.h>
#include <opencv2/opencv.hpp>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

class FrameCapturer {
public:
    FrameCapturer(ComPtr<IDXGIOutput1> output1, ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context, UINT outputIndex);
    ~FrameCapturer();
    void StartCapture();

private:
    ComPtr<IDXGIOutput1> output1_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> stagingTexture_;
    cv::UMat frame_;
    UINT outputIndex_;
    int refreshRate_;
    UINT timeoutMs_;
};
#endif