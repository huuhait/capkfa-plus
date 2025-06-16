#ifndef UTILS_H
#define UTILS_H

#include <comdef.h>
#include <string>

inline void CheckHRESULT(HRESULT hr, const std::string& context) {
    if (FAILED(hr)) {
        _com_error err(hr);
        throw std::runtime_error(context + ": " + err.ErrorMessage());
    }
}

#endif // UTILS_H