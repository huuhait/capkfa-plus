#define SECURITY_WIN32
#define _WINSOCKAPI_

#include "HWIDTool.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <vector>
#include <winsock2.h>
#include <Windows.h>
#include <comdef.h>
#include <iostream>
#include <Wbemidl.h>
#include <iphlpapi.h>
#include <security.h>
#include <openssl/sha.h>
#include <sddl.h>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "libcrypto.lib")

std::string HWIDTool::GetHWID() {
    std::string uuid = GetWmiValue(L"Win32_ComputerSystemProduct", L"UUID");
    std::string biosSerial = GetWmiValue(L"Win32_BIOS", L"SerialNumber");
    std::string macs = GetAllMacs();
    std::string gpuUuid = GetNvidiaGpuUuid();
    std::string diskSerials = GetAllDiskSerials();
    std::string raw = uuid + biosSerial + macs + gpuUuid + diskSerials;
    std::string hwid = ComputeSha256(raw);
    return hwid;
}

std::string HWIDTool::GetWmiValue(const std::wstring& className, const std::wstring& property) {
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) return "";
    hres = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                               RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    if (FAILED(hres)) { CoUninitialize(); return ""; }
    IWbemLocator* pLoc = NULL;
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hres)) { CoUninitialize(); return ""; }
    IWbemServices* pSvc = NULL;
    hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hres)) { pLoc->Release(); CoUninitialize(); return ""; }
    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(hres)) { pSvc->Release(); pLoc->Release(); CoUninitialize(); return ""; }
    std::wstring query = L"SELECT " + property + L" FROM " + className;
    IEnumWbemClassObject* pEnumerator = NULL;
    hres = pSvc->ExecQuery(_bstr_t(L"WQL"), _bstr_t(query.c_str()),
                          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
    if (FAILED(hres)) { pSvc->Release(); pLoc->Release(); CoUninitialize(); return ""; }
    IWbemClassObject* pclsObj = NULL;
    ULONG uReturn = 0;
    std::string result;
    while (pEnumerator) {
        hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
        if (uReturn == 0) break;
        VARIANT vtProp;
        VariantInit(&vtProp);
        hres = pclsObj->Get(property.c_str(), 0, &vtProp, 0, 0);
        if (SUCCEEDED(hres) && vtProp.vt == VT_BSTR && vtProp.bstrVal) {
            _bstr_t bstrVal(vtProp.bstrVal);
            result = std::string(bstrVal, bstrVal.length());
            result.erase(std::remove(result.begin(), result.end(), ' '), result.end());
        }
        VariantClear(&vtProp);
        pclsObj->Release();
    }
    pEnumerator->Release();
    pSvc->Release();
    pLoc->Release();
    CoUninitialize();
    return result;
}

std::string HWIDTool::GetAllDiskSerials() {
    std::vector<std::string> serials;
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) return "";
    IWbemLocator* pLoc = NULL;
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hres)) { CoUninitialize(); return ""; }
    IWbemServices* pSvc = NULL;
    hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hres)) { pLoc->Release(); CoUninitialize(); return ""; }
    IEnumWbemClassObject* pEnumerator = NULL;
    hres = pSvc->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT SerialNumber FROM Win32_PhysicalMedia"),
                          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
    if (FAILED(hres)) { pSvc->Release(); pLoc->Release(); CoUninitialize(); return ""; }
    IWbemClassObject* pclsObj = NULL;
    ULONG uReturn = 0;
    while (pEnumerator) {
        hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
        if (uReturn == 0) break;
        VARIANT vtProp;
        VariantInit(&vtProp);
        hres = pclsObj->Get(L"SerialNumber", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hres) && vtProp.vt == VT_BSTR && vtProp.bstrVal) {
            _bstr_t bstrVal(vtProp.bstrVal);
            std::string sn(bstrVal, bstrVal.length());
            if (!sn.empty()) serials.push_back(sn);
        }
        VariantClear(&vtProp);
        pclsObj->Release();
    }
    pEnumerator->Release();
    pSvc->Release();
    pLoc->Release();
    CoUninitialize();
    std::sort(serials.begin(), serials.end());
    std::string result;
    for (const auto& s : serials) result += s;
    return result;
}

std::string HWIDTool::GetAllMacs() {
    std::vector<std::string> macs;
    ULONG ulSize = 0;
    GetAdaptersInfo(NULL, &ulSize);
    std::vector<BYTE> buffer(ulSize);
    PIP_ADAPTER_INFO pAdapterInfo = (PIP_ADAPTER_INFO)buffer.data();
    if (GetAdaptersInfo(pAdapterInfo, &ulSize) == ERROR_SUCCESS) {
        while (pAdapterInfo) {
            if (pAdapterInfo->Type != MIB_IF_TYPE_LOOPBACK && pAdapterInfo->AddressLength == 6) {
                std::ostringstream oss;
                for (DWORD i = 0; i < pAdapterInfo->AddressLength; ++i) {
                    oss << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << (int)pAdapterInfo->Address[i];
                    if (i < pAdapterInfo->AddressLength - 1) oss << "-";
                }
                macs.push_back(oss.str());
            }
            pAdapterInfo = pAdapterInfo->Next;
        }
    }
    std::sort(macs.begin(), macs.end());
    std::string result;
    for (const auto& m : macs) result += m;
    return result;
}

std::string HWIDTool::GetNvidiaGpuUuid() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return "";
    si.hStdOutput = hWritePipe;
    si.dwFlags = STARTF_USESTDHANDLES;
    std::wstring cmd = L"nvidia-smi --query-gpu=uuid --format=csv,noheader";
    if (CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWritePipe);
        std::string output;
        CHAR buffer[128];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = 0;
            output += buffer;
        }
        CloseHandle(hReadPipe);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        size_t pos = output.find('\n');
        if (pos != std::string::npos) output = output.substr(0, pos);
        return output;
    }
    CloseHandle(hReadPipe);
    CloseHandle(hWritePipe);
    return "";
}

std::string HWIDTool::ComputeSha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input.c_str(), input.length(), hash);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << (int)hash[i];
    }
    return oss.str();
}