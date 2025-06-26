#ifndef CUDAMODEL_H
#define CUDAMODEL_H

#include <vector>
#include <opencv2/core/mat.hpp>

class CudaModel {
public:
    CudaModel();
    ~CudaModel();

    std::vector<float> Predict(const cv::UMat& input);
private:
};

#endif
