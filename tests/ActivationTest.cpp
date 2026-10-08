#include "common.h"
#include <cstdio>

// Exercise the actual DLL's class factory and activation failure handling in
// native TSF. No registration or changes to selected input methods.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 3;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        std::printf("DLL load failed: %lu\n", GetLastError());
        CoUninitialize(); return 4;
    }
    using GetClassObject = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
    auto getClassObject = reinterpret_cast<GetClassObject>(GetProcAddress(dll, "DllGetClassObject"));
    IClassFactory* factory = nullptr;
    ITfTextInputProcessorEx* service = nullptr;
    ITfThreadMgrEx* manager = nullptr;
    HRESULT result = getClassObject ? getClassObject(CLSID_YutpingIME, IID_IClassFactory,
        reinterpret_cast<void**>(&factory)) : E_NOINTERFACE;
    bool managerActive = false;
    if (SUCCEEDED(result)) result = factory->CreateInstance(nullptr, IID_ITfTextInputProcessorEx,
        reinterpret_cast<void**>(&service));
    if (SUCCEEDED(result)) result = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfThreadMgrEx, reinterpret_cast<void**>(&manager));
    TfClientId clientId = TF_CLIENTID_NULL;
    if (SUCCEEDED(result)) {
        // Prevent Windows from activating any of the user's installed TIPs in
        // this thread. We activate only the supplied DLL explicitly below.
        result = manager->ActivateEx(&clientId, TF_TMAE_NOACTIVATETIP);
        managerActive = SUCCEEDED(result);
    }
    if (SUCCEEDED(result)) {
        for (int attempt = 0; attempt < 3 && SUCCEEDED(result); ++attempt) {
            // A null client cannot own a keyboard sink. The IME must report
            // that error, not claim it is ready while no keys can reach it.
            const HRESULT activated = service->ActivateEx(manager, TF_CLIENTID_NULL, 0);
            std::printf("Rejected invalid TSF client %d: 0x%08lX\n", attempt + 1, static_cast<unsigned long>(activated));
            if (SUCCEEDED(activated)) {
                service->Deactivate();
                std::puts("BUG: activation reported success despite a rejected keyboard sink");
                result = E_FAIL;
            }
        }
    }
    if (service) service->Release();
    if (factory) factory->Release();
    if (managerActive) manager->Deactivate();
    if (manager) manager->Release();
    FreeLibrary(dll);
    CoUninitialize();
    if (FAILED(result)) std::printf("Activation test failed: 0x%08lX\n", static_cast<unsigned long>(result));
    return FAILED(result) ? 1 : 0;
}
