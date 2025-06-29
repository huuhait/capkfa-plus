#include "DeviceManager.h"

#include "Obfuscate.h"
#include "Utils.h"

DeviceManager::DeviceManager() {
    ComPtr<IDXGIFactory1> f;
    auto obf_create = $o("CreateDXGIFactory1");
    char buf[20]{};
    $d(obf_create, buf);
    CheckHRESULT($of(CreateDXGIFactory1)(IID_PPV_ARGS(&f)), buf);

    ComPtr<IDXGIAdapter1> a;
    auto obf_ea1 = $o("EA1");
    char buf_ea1[20]{};
    $d(obf_ea1, buf_ea1);
    auto obf_enumAdapters1 = $om(EnumAdapters1, IDXGIFactory1, void);
    CheckHRESULT($call(f.Get(), obf_enumAdapters1, 0, &a), buf_ea1);

    ComPtr<IDXGIOutput> o;
    auto obf_eo = $o("EO");
    char buf_eo[20]{};
    $d(obf_eo, buf_eo);
    auto obf_enumOutputs = $om(EnumOutputs, IDXGIAdapter1, HRESULT);
    CheckHRESULT($call(a.Get(), obf_enumOutputs, 0, &o), buf_eo);
    auto obf_qi1 = $o("QI1");
    char buf_qi1[20]{};
    $d(obf_qi1, buf_qi1);
    CheckHRESULT(o->QueryInterface(IID_PPV_ARGS(&output1_)), buf_qi1);

    D3D_FEATURE_LEVEL fl;
    auto obf_dcd = $o("DCD");
    char buf_dcd[20]{};
    $d(obf_dcd, buf_dcd);
    CheckHRESULT($of(D3D11CreateDevice)(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device_, &fl, &context_), buf_dcd);
}
