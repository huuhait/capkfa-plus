#ifndef FRAME_H
#define FRAME_H

#include <dxgi.h>
#include <stdexcept>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/ocl.hpp>

class Frame {
public:
    Frame() : width_(0), height_(0), pitch_(0), format_(DXGI_FORMAT_UNKNOWN) {}

    // Constructor that allocates and stores GPU memory for the frame
    Frame(const uint8_t* data, int width, int height, int pitch, DXGI_FORMAT format)
        : width_(width), height_(height), pitch_(pitch), format_(format) {

        if (width <= 0 || height <= 0 || pitch < width * 4 || !data) {
            throw std::runtime_error("Invalid frame parameters");
        }

        // Allocate memory for the frame on the GPU (OpenCL/UMat)
        cv::Mat mat(height_, width_, CV_8UC4, const_cast<uint8_t*>(data), pitch_);

        // Copy the data to GPU (if OpenCL is available)
        if (cv::ocl::useOpenCL()) {
            mat.copyTo(umat_);  // Upload to GPU
        } else {
            throw std::runtime_error("OpenCL must be enabled for GPU data handling");
        }
    }

    // Get the UMat (GPU data)
    const cv::UMat& GetData() const { return umat_; }

    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    int GetPitch() const { return pitch_; }
    DXGI_FORMAT GetFormat() const { return format_; }

    bool IsValid() const {
        return !umat_.empty() && width_ > 0 && height_ > 0 && pitch_ >= width_ * 4 &&
               format_ == DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    // Convert GPU data to CPU Mat
    cv::Mat ToMat() const {
        cv::Mat mat(height_, width_, CV_8UC4);
        umat_.copyTo(mat);  // Download GPU data to CPU
        return mat;
    }

    // Convert GPU data to UMat (still on GPU)
    cv::UMat ToUMat() const {
        return umat_;  // Return the GPU data as is
    }
private:
    cv::UMat umat_;  // Store image data on GPU
    int width_, height_, pitch_;
    DXGI_FORMAT format_;
};

#endif
