#ifndef CUDAMODEL_H
#define CUDAMODEL_H

#include <vector>
#include "../Frame/Frame.h"

class CudaModel {
public:
    CudaModel();
    ~CudaModel();

    std::vector<float> Predict(std::shared_ptr<Frame>& input);
private:
};

#endif
