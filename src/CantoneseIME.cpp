#include "CantoneseIME.h"
#include <new>
#include <cstdint>
#include <cstdio>

// ============================================================
// Debug logging
// ============================================================
static void _Dbg(const wchar_t* fmt) {
#ifdef _DEBUG
    OutputDebugStringW(fmt);
#else
    (void)fmt;
#endif
}

// ============================================================
// Edit session helpers
//   TSF forbids touching document text outside an edit session.
//   We wrap each mutation in a tiny ITfEditSession implementation.
// ============================================================
// Activation results only; no keys, spelling, or document text.
// File access may be denied in app containers; logging must not affect input.
static void TraceActivation(const char* stage, HRESULT result) {
    wchar_t local[32768];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (!length || length >= 32768) return;
    const std::wstring directory = std::wstring(local) + L"\\YutpingIME";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    const std::wstring path = directory + L"\\activation.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 65536) {
        CloseHandle(file);
        file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
    }
    SYSTEMTIME time;
    GetSystemTime(&time);
    char line[180];
    const int count = sprintf_s(line, "%04u-%02u-%02uT%02u:%02u:%02uZ pid=%lu %s hr=0x%08lX\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        GetCurrentProcessId(), stage, static_cast<unsigned long>(result));
    DWORD written;
    if (count > 0) WriteFile(file, line, static_cast<DWORD>(count), &written, nullptr);
    CloseHandle(file);
}

namespace {

// Generic edit session that runs a std::function under an edit cookie.
class FuncEditSession : public ITfEditSession {
public:
    FuncEditSession(std::function<void(TfEditCookie)> fn)
        : m_ref(1), m_fn(std::move(fn)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        m_fn(ec);
        return S_OK;
    }
private:
    LONG m_ref;
    std::function<void(TfEditCookie)> m_fn;
};

// Convenience: request a synchronous read/write edit session
bool RunEditSession(ITfContext* pic, TfClientId tid,
                    std::function<void(TfEditCookie)> fn) {
    if (!pic) { _Dbg(L"RunEditSession: pic=NULL\n"); return false; }
    auto* session = new (std::nothrow) FuncEditSession(std::move(fn));
    if (!session) return false;
    HRESULT hr = E_FAIL;
    HRESULT request = pic->RequestEditSession(tid, session,
        TF_ES_SYNC | TF_ES_READWRITE, &hr);
    wchar_t dbuf[80];
    wsprintfW(dbuf, L"RunEditSession: hr=0x%08X\n", hr);
    _Dbg(dbuf);
    session->Release();
    return SUCCEEDED(request) && SUCCEEDED(hr);
}

} // namespace

// ============================================================
// Construction
// ============================================================
CantoneseIME::CantoneseIME() {
    extern LONG g_cDllRef;
    InterlockedIncrement(&g_cDllRef);
    m_candWnd = std::make_unique<CandidateWindow>();
}

CantoneseIME::~CantoneseIME() {
    extern LONG g_cDllRef;
    InterlockedDecrement(&g_cDllRef);
}

// ============================================================
// IUnknown
// ============================================================
STDMETHODIMP CantoneseIME::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor)) {
        *ppv = static_cast<ITfTextInputProcessor*>(this);
    } else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfTextLayoutSink)) {
        *ppv = static_cast<ITfTextLayoutSink*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CantoneseIME::AddRef() {
    return ++m_refCount;
}

STDMETHODIMP_(ULONG) CantoneseIME::Release() {
    LONG c = --m_refCount;
    if (c == 0) delete this;
    return c;
}

// ============================================================
// Lifecycle
// ============================================================
STDMETHODIMP CantoneseIME::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP CantoneseIME::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD) {
    TraceActivation("ActivateEx.start", ptim ? S_OK : E_INVALIDARG);
    if (!ptim) return E_INVALIDARG;
    m_foreground = true;
    m_settings = LoadUserSettings();
    m_pThreadMgr = ptim;
    m_pThreadMgr->AddRef();
    m_clientId = tid;
    m_uiThreadId = GetCurrentThreadId();

    m_candWnd->SetSelectCallback([this](int idx) {
        _CommitCandidate(m_candPage * m_settings.pageSize + idx);
    });
    m_candWnd->SetPageCallback([this](int delta) {
        if (delta > 0) _NextCandPage();
        else _PrevCandPage();
    });
    extern HINSTANCE g_hInst;
    CandidateWindow::RegisterClass(g_hInst);

    // Sink for focus changes
    ITfSource* pSource = nullptr;
    HRESULT sourceResult = m_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource);
    TraceActivation("ThreadSource.QI", sourceResult);
    if (SUCCEEDED(sourceResult) && pSource) {
        HRESULT advised = pSource->AdviseSink(IID_ITfThreadMgrEventSink,
            static_cast<ITfThreadMgrEventSink*>(this), &m_threadMgrCookie);
        TraceActivation("ThreadSink.Advise", advised);
        pSource->Release();
    }

    // Keyboard sink
    ITfKeystrokeMgr* pKeyMgr = nullptr;
    HRESULT keyResult = m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeyMgr);
    TraceActivation("KeyboardManager.QI", keyResult);
    if (SUCCEEDED(keyResult) && pKeyMgr) {
        HRESULT advised = pKeyMgr->AdviseKeyEventSink(m_clientId,
            static_cast<ITfKeyEventSink*>(this), TRUE);
        TraceActivation("KeyboardSink.Advise", advised);
        pKeyMgr->Release();
    }

    // Hidden marshaling window for background-thread callbacks
    {
        std::wstring className = L"YutpingMarshal_" +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(g_hInst));
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = _MarshalWndProc;
        wc.hInstance      = g_hInst;
        wc.lpszClassName  = className.c_str();
        RegisterClassExW(&wc);
        m_hMarshalWnd = CreateWindowExW(0, className.c_str(), L"", 0,
            0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hInst, this);
        if (m_hMarshalWnd)
            SetWindowLongPtrW(m_hMarshalWnd, GWLP_USERDATA, (LONG_PTR)this);
    }
    if (!m_hMarshalWnd) {
        HRESULT error = HRESULT_FROM_WIN32(GetLastError());
        TraceActivation("MarshalWindow.Create", error);
        Deactivate();
        return error;
    }
    {
        std::lock_guard<std::mutex> lock(m_queryState->mutex);
        m_queryState->window = m_hMarshalWnd;
    }
    ITfDocumentMgr* focus = nullptr;
    if (SUCCEEDED(ptim->GetFocus(&focus)) && focus) {
        ITfContext* context = nullptr;
        if (SUCCEEDED(focus->GetTop(&context)) && context) {
            _SetContext(context);
            context->Release();
        }
        focus->Release();
    }

    TraceActivation("ActivateEx.complete", S_OK);
    return S_OK;
}

STDMETHODIMP CantoneseIME::Deactivate() {
    _Reset();
    {
        std::lock_guard<std::mutex> lock(m_queryState->mutex);
        m_queryState->window = nullptr;
    }

    ITfKeystrokeMgr* pKeyMgr = nullptr;
    if (m_pThreadMgr &&
        SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeyMgr)) && pKeyMgr) {
        pKeyMgr->UnadviseKeyEventSink(m_clientId);
        pKeyMgr->Release();
    }

    if (m_pThreadMgr && m_threadMgrCookie != TF_INVALID_COOKIE) {
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource)) && pSource) {
            pSource->UnadviseSink(m_threadMgrCookie);
            pSource->Release();
        }
        m_threadMgrCookie = TF_INVALID_COOKIE;
    }

    _SetContext(nullptr);
    if (m_pThreadMgr) { m_pThreadMgr->Release(); m_pThreadMgr = nullptr; }
    if (m_hMarshalWnd) { DestroyWindow(m_hMarshalWnd); m_hMarshalWnd = nullptr; }
    m_clientId = TF_CLIENTID_NULL;
    return S_OK;
}

// ============================================================
// Focus tracking
// ============================================================
STDMETHODIMP CantoneseIME::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr*) {
    _Reset();
    ITfContext* context = nullptr;
    if (pdimFocus) {
        pdimFocus->GetTop(&context);
    }
    _SetContext(context);
    if (context) context->Release();
    return S_OK;
}

void CantoneseIME::_SetContext(ITfContext* context) {
    if (context != m_pContext && m_pComposition) {
        // The host owns the old composition's lifetime; never edit its range
        // with a cookie belonging to a newly focused document.
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    if (m_pContext && m_layoutCookie != TF_INVALID_COOKIE) {
        ITfSource* source = nullptr;
        if (SUCCEEDED(m_pContext->QueryInterface(IID_ITfSource, (void**)&source))) {
            source->UnadviseSink(m_layoutCookie);
            source->Release();
        }
    }
    m_layoutCookie = TF_INVALID_COOKIE;
    if (m_pContext) m_pContext->Release();
    m_pContext = context;
    m_caretValid = false;
    if (m_pContext) {
        m_pContext->AddRef();
        ITfSource* source = nullptr;
        if (SUCCEEDED(m_pContext->QueryInterface(IID_ITfSource, (void**)&source))) {
            source->AdviseSink(IID_ITfTextLayoutSink,
                static_cast<ITfTextLayoutSink*>(this), &m_layoutCookie);
            source->Release();
        }
    }
}

STDMETHODIMP CantoneseIME::OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) {
    // TSF may still hold a document lock during this callback. Read the new
    // geometry after the callback returns to the host message loop.
    if (context == m_pContext && code != TF_LC_DESTROY && !m_buffer.empty() && m_hMarshalWnd)
        PostMessageW(m_hMarshalWnd, WM_LAYOUT_READY, 0, 0);
    return S_OK;
}

// ============================================================
// Keyboard
// ============================================================

// Chinese punctuation: OEM VK code → Chinese char
// VK_OEM_1 (;:), VK_OEM_PLUS (+=), VK_OEM_COMMA (,<),
// VK_OEM_MINUS (_-), VK_OEM_PERIOD (.>), VK_OEM_2 (/?),
// VK_OEM_3 (`~), VK_OEM_4 ([{), VK_OEM_5 (\\|), VK_OEM_6 (]}),
// VK_OEM_7 ('")
static wchar_t VkToChinesePunct(WPARAM wp, bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
    switch (wp) {
    case VK_OEM_PERIOD:  return shift ? L'。'  : L'。';   // . / >
    case VK_OEM_COMMA:   return shift ? L'，'  : L'，';   // , / <
    case VK_OEM_1:       return shift ? L'：'  : L'；';   // : / ;
    case VK_OEM_2:       return shift ? L'？'  : L'？';   // ? / /
    case VK_OEM_MINUS:   return shift ? L'＿'  : L'－';   // _ / -
    case VK_OEM_PLUS:    return shift ? L'＋'  : L'＋';   // + / =
    case VK_OEM_4:       return shift ? L'『'  : L'「';   // { / [
    case VK_OEM_6:       return shift ? L'』'  : L'」';   // } / ]
    case VK_OEM_7:       return shift ? L'」'  : L'「';   // " / '
    case VK_OEM_5:       return shift ? L'｜'  : L'、';   // pipe / backslash
    case VK_OEM_102:     return L'《';
    case VK_OEM_3:       return shift ? L'～'  : L'～';   // ~ / `
    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':
        return 0;   // digits: don't map, used for candidate selection
    case '0': case VK_SPACE: case VK_RETURN:
    case VK_BACK: case VK_ESCAPE:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE: case VK_TAB:
        return 0;
    default:
        // Only map real OEM punctuation above. Function keys (VK_F1..F24 =
        // 0x70..0x87) and numpad keys (0x60..0x69) also fall inside the old
        // 0x20..0x7E test, which is why F5 was being swallowed as U+FF55.
        return 0;
    }
}

// Which keys does the IME want to consume?
bool CantoneseIME::_CanAcceptInput(ITfContext* context) const {
    if (!m_foreground || !context) return false;
    TF_STATUS status = {};
    if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TF_SD_READONLY))
        return false;

    ITfCompartmentMgr* manager = nullptr;
    if (SUCCEEDED(context->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(&manager))) && manager) {
        bool disabled = false;
        for (const GUID& id : { GUID_COMPARTMENT_KEYBOARD_DISABLED, GUID_COMPARTMENT_EMPTYCONTEXT }) {
            ITfCompartment* compartment = nullptr;
            if (SUCCEEDED(manager->GetCompartment(id, &compartment)) && compartment) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(compartment->GetValue(&value)) && value.vt == VT_I4 && value.lVal)
                    disabled = true;
                VariantClear(&value);
                compartment->Release();
            }
        }
        manager->Release();
        if (disabled) return false;
    }
    ITfInsertAtSelection* insert = nullptr;
    const HRESULT result = context->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&insert));
    if (insert) insert->Release();
    return SUCCEEDED(result) && insert;
}

void CantoneseIME::_SuspendInput() {
    _Reset(); // Also invalidates queued API replies and hides the candidate popup.
    if (m_pComposition) {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
}

STDMETHODIMP CantoneseIME::OnSetFocus(BOOL foreground) {
    m_foreground = foreground != FALSE;
    if (!m_foreground) _SuspendInput();
    return S_OK;
}

bool CantoneseIME::_IsKeyEaten(WPARAM wp) const {
    // In English mode, let everything pass through
    if (m_englishMode) return false;

    bool composing = !m_buffer.empty();

    // Text typed after a deferred selection must wait too. Otherwise the host
    // can insert it inside the old composition and the result replaces it.
    if ((m_deferredCandidate >= 0 || !m_queuedKeys.empty()) &&
        ((wp >= '0' && wp <= '9') || wp == VK_TAB || VkToChinesePunct(wp))) return true;

    // Chinese mode: letters start/update composition
    if (wp >= 'A' && wp <= 'Z') return true;

    // Chinese mode: punctuation keys get mapped to Chinese
    if (!composing && m_settings.chinesePunctuation && VkToChinesePunct(wp)) return true;

    if (composing) {
        if (wp == VK_OEM_7) return true; // apostrophe separates syllables
        if (wp == VK_BACK || wp == VK_ESCAPE || wp == VK_SPACE || wp == VK_RETURN)
            return true;
        if (wp >= '1' && wp <= ('0' + m_settings.pageSize))
            return true;
        if (!m_candidates.empty() || m_queryPending) {
            switch (wp) {
            case VK_OEM_PLUS: case VK_OEM_PERIOD: case VK_OEM_6:
            case VK_NEXT:     case VK_DOWN: case VK_RIGHT:
            case VK_OEM_MINUS: case VK_OEM_COMMA: case VK_OEM_4:
            case VK_PRIOR:    case VK_UP:   case VK_LEFT:
                return true;   // paging keys
            }
        }
    }
    return false;
}

static bool IsShiftKey(WPARAM wp) {
    return wp == VK_SHIFT || wp == VK_LSHIFT || wp == VK_RSHIFT;
}

static bool IsControlKey(WPARAM wp) {
    return wp == VK_CONTROL || wp == VK_LCONTROL || wp == VK_RCONTROL;
}

void CantoneseIME::_ObserveToggleDown(WPARAM wp, LPARAM lp, bool control, bool shift, bool alt) {
    if (m_settings.f12Toggle || alt || (!IsShiftKey(wp) && !IsControlKey(wp))) {
        m_togglePending = false;
        return;
    }
    if (lp & (LPARAM(1) << 30)) return; // A used chord must not re-arm on repeat.
    if (control && shift) {
        m_togglePending = true;
        for (int key = 1; m_togglePending && key < 256; ++key)
            if (!IsShiftKey(key) && !IsControlKey(key) && (GetKeyState(key) & 0x8000))
                m_togglePending = false;
    }
}

bool CantoneseIME::_ReleaseToggle(WPARAM wp) {
    const bool toggle = m_togglePending && !m_settings.f12Toggle &&
        (IsShiftKey(wp) || IsControlKey(wp));
    m_togglePending = false;
    return toggle;
}

STDMETHODIMP CantoneseIME::OnTestKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* pfEaten) {
    m_settings = LoadUserSettings();
    wchar_t buf[80];
    wsprintfW(buf, L"OnTestKeyDown: VK=0x%X english=%d composing=%d\n", (int)wp, m_englishMode, !m_buffer.empty());
    _Dbg(buf);
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    _ObserveToggleDown(wp, lp, control, shift, alt);
    // Request modifier callbacks but let their actual down/up events reach the
    // host. Toggle once on the first release of a Ctrl+Shift-only chord.
    if (IsShiftKey(wp) || IsControlKey(wp)) {
        *pfEaten = m_settings.f12Toggle ? FALSE : TRUE;
        return S_OK;
    }
    // Modifier keys held → let it pass (e.g. Ctrl+C)
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) {
        *pfEaten = FALSE;
        return S_OK;
    }
    if (wp == VK_F12 && m_settings.f12Toggle) {
        *pfEaten = TRUE;
        return S_OK;
    }
    if (!_CanAcceptInput(pic)) {
        _SuspendInput();
        *pfEaten = FALSE;
        return S_OK;
    }
    *pfEaten = _IsKeyEaten(wp) ? TRUE : FALSE;
    wsprintfW(buf, L"  eaten=%d punct=%d\n", *pfEaten, (int)VkToChinesePunct(wp));
    _Dbg(buf);
    return S_OK;
}

STDMETHODIMP CantoneseIME::OnKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* pfEaten) {
    if (!IsShiftKey(wp) && !IsControlKey(wp)) m_togglePending = false;
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    if (IsShiftKey(wp) || IsControlKey(wp)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    if (wp == VK_F12 && m_settings.f12Toggle) {
        _ToggleMode(pic);
        *pfEaten = TRUE;
        return S_OK;
    }
    if (!_CanAcceptInput(pic)) {
        _SuspendInput();
        *pfEaten = FALSE;
        return S_OK;
    }
    if (pic != m_pContext) {
        _SuspendInput();
        _SetContext(pic);
    }

    if (!_IsKeyEaten(wp)) {
        *pfEaten = FALSE;
        return S_OK;
    }
    *pfEaten = TRUE;

    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if ((m_deferredCandidate >= 0 || !m_queuedKeys.empty()) && wp != VK_ESCAPE) {
        m_queuedKeys.push_back({ wp, shift });
        return S_OK;
    }
    _HandleKeyDown(pic, wp, shift, pfEaten);
    return S_OK;
}

void CantoneseIME::_HandleKeyDown(ITfContext* pic, WPARAM wp, bool shift, BOOL* pfEaten) {
    if (m_englishMode) { *pfEaten = FALSE; return; }

    bool composing = !m_buffer.empty();

    // Letter → append to buffer (always lowercase for composition)
    if (wp >= 'A' && wp <= 'Z') {
        wchar_t ch = (wchar_t)(wp - 'A' + 'a');
        const std::wstring previous = m_buffer;
        m_buffer += ch;
        bool written = false;
        if (!RunEditSession(pic, m_clientId, [this, pic, &written](TfEditCookie ec) {
            written = _StartOrUpdateComposition(ec, pic, m_buffer);
            if (written) _CaptureCaretPos(ec, pic);
        }) || !written) {
            m_buffer = previous;
            *pfEaten = FALSE; // A host refusing text must still receive its controls.
            return;
        }
        _RequestQuery();
        return;
    }

    if (wp == VK_OEM_7 && composing) {
        m_buffer += L'\'';
        RunEditSession(pic, m_clientId, [this, pic](TfEditCookie ec) {
            _StartOrUpdateComposition(ec, pic, m_buffer);
            _CaptureCaretPos(ec, pic);
        });
        _RequestQuery();
        return;
    }

    // Chinese punctuation → insert directly (no composition needed)
    if (!composing && m_settings.chinesePunctuation) {
        wchar_t zh = VkToChinesePunct(wp, shift);
        wchar_t logbuf[80];
        wsprintfW(logbuf, L"  punct: VK=0x%X zh=U+%04X\n", (int)wp, (int)zh);
        _Dbg(logbuf);
        if (zh) {
            bool written = false;
            if (!RunEditSession(pic, m_clientId, [this, pic, zh, &written](TfEditCookie ec) {
                written = _InsertText(ec, pic, std::wstring(1, zh));
            }) || !written) *pfEaten = FALSE;
            return;
        }
    }

    if (!composing) { *pfEaten = FALSE; return; }

    switch (wp) {
    case VK_BACK:
        m_buffer.pop_back();
        if (m_buffer.empty()) {
            bool ended = false;
            if (RunEditSession(pic, m_clientId, [this, pic, &ended](TfEditCookie ec) {
                ended = _EndComposition(ec, pic, L"");
            }) && ended) _Reset();
        } else {
            RunEditSession(pic, m_clientId, [this, pic](TfEditCookie ec) {
                _StartOrUpdateComposition(ec, pic, m_buffer);
                _CaptureCaretPos(ec, pic);
            });
            _RequestQuery();
        }
        break;

    case VK_ESCAPE: {
        bool ended = false;
        if (RunEditSession(pic, m_clientId, [this, pic, &ended](TfEditCookie ec) {
            ended = _EndComposition(ec, pic, L"");
        }) && ended) _Reset();
        break;
    }

    case VK_SPACE:
        // Space selects Chinese, even if it arrives before the API response.
        _CommitCandidate(m_candPage * m_settings.pageSize);
        break;

    case VK_RETURN:
        // Spec: Enter commits the raw romaji (English), Space picks a candidate.
        if (composing) {
            std::wstring raw = m_buffer;
            wchar_t dbuf[80];
            wsprintfW(dbuf, L"RETURN: commit raw=%s\n", raw.c_str());
            _Dbg(dbuf);
            bool ended = false;
            if (RunEditSession(pic, m_clientId, [this, pic, raw, &ended](TfEditCookie ec) {
                ended = _EndComposition(ec, pic, raw);
            }) && ended) _Reset();
        } else {
            *pfEaten = FALSE;
            return;
        }
        break;

    case VK_OEM_PLUS:    // =
    case VK_OEM_PERIOD:  // .
    case VK_OEM_6:       // ]
    case VK_NEXT:        // Page Down
    case VK_DOWN:
    case VK_RIGHT:
        if (m_queryPending) {
            m_queuedKeys.push_back({ wp, shift });
            _SendQuery();
        } else _NextCandPage();
        break;

    case VK_OEM_MINUS:   // -
    case VK_OEM_COMMA:   // ,
    case VK_OEM_4:       // [
    case VK_PRIOR:       // Page Up
    case VK_UP:
    case VK_LEFT:
        if (m_queryPending) {
            m_queuedKeys.push_back({ wp, shift });
            _SendQuery();
        } else _PrevCandPage();
        break;

    default:
        if (wp >= '1' && wp <= ('0' + m_settings.pageSize)) {
            int idx = (int)(wp - '1') + m_candPage * m_settings.pageSize;
            _CommitCandidate(idx);
        } else *pfEaten = FALSE;
        break;
    }
    return;
}

STDMETHODIMP CantoneseIME::OnTestKeyUp(ITfContext*, WPARAM wp, LPARAM, BOOL* pfEaten) {
    *pfEaten = (IsShiftKey(wp) || IsControlKey(wp)) && !m_settings.f12Toggle ? TRUE : FALSE;
    return S_OK;
}
STDMETHODIMP CantoneseIME::OnKeyUp(ITfContext* pic, WPARAM wp, LPARAM, BOOL* pfEaten) {
    if (_ReleaseToggle(wp)) {
        _ToggleMode(pic);
    }
    *pfEaten = FALSE;
    return S_OK;
}

bool CantoneseIME::_ToggleMode(ITfContext* pic) {
    if (!_CanAcceptInput(pic) || pic != m_pContext) {
        _SuspendInput();
        if (pic != m_pContext) _SetContext(pic);
    }
    auto queued = std::move(m_queuedKeys);
    if (!m_buffer.empty()) {
        // Switching modes is also the manual escape from pending conversion.
        // Preserve spelling rather than guessing a candidate or waiting online.
        const std::wstring raw = m_buffer;
        bool ended = false;
        if (!RunEditSession(pic, m_clientId, [this, pic, raw, &ended](TfEditCookie ec) {
            ended = _EndComposition(ec, pic, raw);
        }) || !ended) {
            m_queuedKeys = std::move(queued);
            return false;
        }
        _Reset();
    }
    m_englishMode = !m_englishMode;
    m_queuedKeys = std::move(queued);
    _DrainQueuedKeys();
    return true;
}
STDMETHODIMP CantoneseIME::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten) {
    *pfEaten = FALSE;
    return S_OK;
}

// ============================================================
// Composition
// ============================================================
STDMETHODIMP CantoneseIME::OnCompositionTerminated(TfEditCookie, ITfComposition* pComposition) {
    // Our own EndComposition detached the pointer and its caller owns reset.
    // Some hosts notify synchronously; others deliver an old notification later.
    if (pComposition != m_pComposition) return S_OK;
    if (m_pComposition) {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    _Reset();
    return S_OK;
}

bool CantoneseIME::_StartOrUpdateComposition(TfEditCookie ec, ITfContext* pic, const std::wstring& text) {
    if (!pic) return false;
    const bool starting = !m_pComposition;

    if (!m_pComposition) {
        // Start a new composition at the current selection
        ITfInsertAtSelection* pInsert = nullptr;
        if (FAILED(pic->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsert)) || !pInsert)
            return false;

        ITfRange* pRange = nullptr;
        if (SUCCEEDED(pInsert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &pRange)) && pRange) {
            ITfContextComposition* pCtxComp = nullptr;
            if (SUCCEEDED(pic->QueryInterface(IID_ITfContextComposition, (void**)&pCtxComp)) && pCtxComp) {
                pCtxComp->StartComposition(ec, pRange,
                    static_cast<ITfCompositionSink*>(this), &m_pComposition);
                pCtxComp->Release();
            }
            pRange->Release();
        }
        pInsert->Release();
    }

    if (!m_pComposition) return false;

    // Replace composition range text
    ITfRange* pRange = nullptr;
    if (SUCCEEDED(m_pComposition->GetRange(&pRange)) && pRange) {
        if (FAILED(pRange->SetText(ec, 0, text.c_str(), (LONG)text.length()))) {
            pRange->Release();
            if (starting) {
                ITfComposition* composition = m_pComposition;
                m_pComposition = nullptr;
                composition->EndComposition(ec);
                composition->Release();
            }
            return false;
        }

        // Underline the composition to show it's pending
        ITfProperty* pProp = nullptr;
        if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &pProp)) && pProp) {
            // (display attribute wiring omitted in POC — text still shows underlined
            //  by default composition rendering in most controls)
            pProp->Release();
        }

        // Move caret to end of composition
        ITfRange* pEnd = nullptr;
        if (SUCCEEDED(pRange->Clone(&pEnd)) && pEnd) {
            pEnd->Collapse(ec, TF_ANCHOR_END);
            TF_SELECTION sel;
            sel.range = pEnd;
            sel.style.ase = TF_AE_NONE;
            sel.style.fInterimChar = FALSE;
            pic->SetSelection(ec, 1, &sel);
            pEnd->Release();
        }
        pRange->Release();
        return true;
    }
    return false;
}

bool CantoneseIME::_EndComposition(TfEditCookie ec, ITfContext* pic, const std::wstring& commitText) {
    wchar_t dbuf[120];
    wsprintfW(dbuf, L"EndComposition: comp=%p text=%s\n", m_pComposition, commitText.c_str());
    _Dbg(dbuf);
    if (!m_pComposition) {
        return commitText.empty() || _InsertText(ec, pic, commitText);
    }

    ITfRange* pRange = nullptr;
    if (FAILED(m_pComposition->GetRange(&pRange)) || !pRange) return false;
    {
        // Replace the composition text with the committed characters
        if (FAILED(pRange->SetText(ec, 0, commitText.c_str(), (LONG)commitText.length()))) {
            pRange->Release();
            return false;
        }

        // Move selection to end so typing continues after the committed text
        ITfRange* pEnd = nullptr;
        if (SUCCEEDED(pRange->Clone(&pEnd)) && pEnd) {
            pEnd->Collapse(ec, TF_ANCHOR_END);
            TF_SELECTION sel;
            sel.range = pEnd;
            sel.style.ase = TF_AE_NONE;
            sel.style.fInterimChar = FALSE;
            pic->SetSelection(ec, 1, &sel);
            pEnd->Release();
        }
        pRange->Release();
    }

    // EndComposition can synchronously invoke OnCompositionTerminated. Detach
    // our pointer first so the callback cannot release it a second time.
    ITfComposition* composition = m_pComposition;
    m_pComposition = nullptr;
    if (FAILED(composition->EndComposition(ec))) {
        m_pComposition = composition;
        _StartOrUpdateComposition(ec, pic, m_buffer);
        return false;
    }
    composition->Release();
    return true;
}

// Insert text directly at cursor (no composition needed)
bool CantoneseIME::_InsertText(TfEditCookie ec, ITfContext* pic, const std::wstring& text) {
    if (!pic) return false;
    ITfInsertAtSelection* pInsert = nullptr;
    if (FAILED(pic->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsert)) || !pInsert)
        return false;

    ITfRange* pRange = nullptr;
    const HRESULT inserted = pInsert->InsertTextAtSelection(ec, 0, text.c_str(),
            (LONG)text.length(), &pRange);
    if (SUCCEEDED(inserted) && pRange) {
        // Move caret past inserted text
        pRange->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel;
        sel.range = pRange;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        pic->SetSelection(ec, 1, &sel);
        pRange->Release();
    }
    pInsert->Release();
    return SUCCEEDED(inserted);
}

// ============================================================
// Candidate query & window
// ============================================================
void CantoneseIME::_RequestQuery() {
    m_candPage = 0;  // reset to first page on new query
    m_candidates.clear();
    m_queryPending = true;
    {
        std::lock_guard<std::mutex> lock(m_queryState->mutex);
        ++m_queryState->generation;
        m_queryState->ready = false;
    }
    _UpdateCandidateWindow();
    // Coalesce a burst of letters; a selection key flushes this timer immediately.
    m_queryScheduled = true;
    if (!m_hMarshalWnd || !SetTimer(m_hMarshalWnd, QUERY_TIMER, 35, nullptr)) _SendQuery();
}

void CantoneseIME::_SendQuery() {
    if (!m_queryScheduled) return;
    m_queryScheduled = false;
    if (m_hMarshalWnd) KillTimer(m_hMarshalWnd, QUERY_TIMER);
    const std::wstring snapshot = m_buffer;
    const auto state = m_queryState;
    unsigned long long generation;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        generation = state->generation;
    }
    GoogleInputAPI::QueryAsync(snapshot, [state, generation](CandidateResult result) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (generation != state->generation || !state->window) return;
        state->result = std::move(result);
        state->ready = true;
        PostMessageW(state->window, WM_CANDIDATES_READY, 0, 0);
    });
}

void CantoneseIME::_ReceiveCandidates() {
    if (!_CanAcceptInput(m_pContext)) {
        _SuspendInput();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_queryState->mutex);
        if (!m_queryState->ready) return;
        m_candidates = std::move(m_queryState->result.candidates);
        m_cached = m_queryState->result.cached;
        m_offline = m_queryState->result.offline;
        m_queryState->ready = false;
    }
    m_queryPending = false;
    if (m_settings.learnChoices) RankCandidates(m_buffer, m_candidates);
    m_candPage = 0;
    _UpdateCandidateWindow();
    const int deferred = m_deferredCandidate;
    m_deferredCandidate = -1;
    // A final commit resets IME state, but must not discard keys typed after Space.
    auto queued = std::move(m_queuedKeys);
    if (deferred >= 0) _CommitCandidate(deferred);
    m_queuedKeys = std::move(queued);
    _DrainQueuedKeys();
}

void CantoneseIME::_DrainQueuedKeys() {
    auto queued = std::move(m_queuedKeys);
    m_queuedKeys.clear();
    while (!queued.empty()) {
        const auto input = queued.front();
        queued.pop_front();
        BOOL eaten = TRUE;
        _HandleKeyDown(m_pContext, input.key, input.shift, &eaten);
        // Once the previous word is committed, a queued Space/digit/Enter
        // belongs to the application instead of the old candidate list.
        if (!eaten) {
            if (input.key == VK_BACK) RunEditSession(m_pContext, m_clientId, [this](TfEditCookie ec) {
                TF_SELECTION selection;
                ULONG fetched = 0;
                if (SUCCEEDED(m_pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) && fetched) {
                    LONG shifted = 0;
                    if (SUCCEEDED(selection.range->ShiftStart(ec, -1, &shifted, nullptr)) && shifted == -1) {
                        selection.range->SetText(ec, 0, L"", 0);
                        selection.range->Collapse(ec, TF_ANCHOR_END);
                        m_pContext->SetSelection(ec, 1, &selection);
                    }
                    selection.range->Release();
                }
            });
            std::wstring literal;
            if (input.key == VK_SPACE) literal = L" ";
            else if (input.key == VK_RETURN) literal = L"\r\n";
            else if (input.key == VK_TAB) literal = L"\t";
            else if (input.key != VK_BACK && input.key != VK_ESCAPE) {
                BYTE keyboard[256] = {};
                keyboard[VK_SHIFT] = input.shift ? 0x80 : 0;
                WCHAR text[4] = {};
                const HKL layout = GetKeyboardLayout(0);
                const UINT scan = MapVirtualKeyExW(static_cast<UINT>(input.key), MAPVK_VK_TO_VSC, layout);
                const int length = ToUnicodeEx(static_cast<UINT>(input.key), scan, keyboard,
                    text, 4, 4, layout); // Do not alter Windows' dead-key state.
                if (length > 0) literal.assign(text, length);
            }
            if (!literal.empty()) RunEditSession(m_pContext, m_clientId, [this, literal](TfEditCookie ec) {
                if (m_buffer.empty()) _InsertText(ec, m_pContext, literal);
                else {
                    m_buffer += literal;
                    _StartOrUpdateComposition(ec, m_pContext, m_buffer);
                }
            });
            if (!literal.empty() && !m_buffer.empty()) _RequestQuery();
        }
        if (m_deferredCandidate >= 0 || !m_queuedKeys.empty()) break;
    }
    m_queuedKeys.insert(m_queuedKeys.end(), queued.begin(), queued.end());
}

void CantoneseIME::_NextCandPage() {
    if (m_candidates.empty()) return;
    int totalPages = ((int)m_candidates.size() + m_settings.pageSize - 1) / m_settings.pageSize;
    wchar_t dbuf[110];
    wsprintfW(dbuf, L"NextPage: cands=%d perPage=%d totalPages=%d page=%d\n",
        (int)m_candidates.size(), m_settings.pageSize, totalPages, m_candPage);
    _Dbg(dbuf);
    if (totalPages <= 1) return;
    m_candPage = (m_candPage + 1) % totalPages;
    _UpdateCandidateWindow();
}

void CantoneseIME::_PrevCandPage() {
    if (m_candidates.empty()) return;
    int totalPages = ((int)m_candidates.size() + m_settings.pageSize - 1) / m_settings.pageSize;
    if (totalPages <= 1) return;
    m_candPage = (m_candPage + totalPages - 1) % totalPages;
    _UpdateCandidateWindow();
}

void CantoneseIME::_UpdateCandidateWindow() {
    if (m_buffer.empty()) {
        m_candWnd->Hide();
        return;
    }
    // Extract current page of candidates
    int start = m_candPage * m_settings.pageSize;
    int end = min(start + m_settings.pageSize, (int)m_candidates.size());
    std::vector<Candidate> page(m_candidates.begin() + start, m_candidates.begin() + end);

    const bool hadCaret = m_caretValid;
    const POINT previousCaret = m_lastCaretPt;
    _RefreshCaretPos();
    if (!m_caretValid && hadCaret) {
        m_lastCaretPt = previousCaret;
        m_caretValid = true;
    }
    if (!m_caretValid) { m_candWnd->Hide(); return; }
    m_candWnd->Show(m_buffer, page, m_lastCaretPt, m_candPage + 1,
        (std::max)(1, ((int)m_candidates.size() + m_settings.pageSize - 1) / m_settings.pageSize),
        m_queryPending, m_cached, m_offline);
}

void CantoneseIME::_CommitCandidate(int index) {
    if (!_CanAcceptInput(m_pContext)) {
        _SuspendInput();
        return;
    }
    if (m_queryPending) {
        m_deferredCandidate = index;
        _SendQuery();
        return;
    }
    if (index < 0 || index >= (int)m_candidates.size()) return;
    const Candidate candidate = m_candidates[index];
    const std::wstring text = candidate.text;
    const size_t consumed = candidate.matchedLength > 0 && candidate.matchedLength <= m_buffer.size()
        ? candidate.matchedLength : m_buffer.size();
    const std::wstring spelling = m_buffer.substr(0, consumed);
    std::wstring remainder = m_buffer.substr(consumed);
    const LONG tailLength = static_cast<LONG>(remainder.size());
    // Explicit syllable separators following the consumed prefix are not text.
    remainder.erase(0, remainder.find_first_not_of(L"' "));
    ITfContext* pic = m_pContext;
    wchar_t dbuf[120];
    wsprintfW(dbuf, L"CommitCandidate[%d]: text=%s ctx=%p comp=%p\n",
        index, text.c_str(), pic, m_pComposition);
    _Dbg(dbuf);
    if (!remainder.empty() && m_pComposition) {
        bool committed = false;
        if (!RunEditSession(pic, m_clientId, [this, pic, text, remainder, tailLength, &committed](TfEditCookie ec) {
            ITfRange* range = nullptr;
            if (FAILED(m_pComposition->GetRange(&range)) || !range) return;
            // Replace only the matched prefix and move the composition start
            // past it. The remainder and popup stay in the same composition.
            LONG shifted = 0;
            if (SUCCEEDED(range->ShiftEnd(ec, -tailLength, &shifted, nullptr)) &&
                shifted == -tailLength &&
                SUCCEEDED(range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.size())))) {
                committed = SUCCEEDED(range->Collapse(ec, TF_ANCHOR_END)) &&
                    SUCCEEDED(m_pComposition->ShiftStart(ec, range));
                if (committed) _StartOrUpdateComposition(ec, pic, remainder);
                else {
                    // Keep a readable spelling if the host refuses ShiftStart.
                    _StartOrUpdateComposition(ec, pic, m_buffer);
                }
                _CaptureCaretPos(ec, pic);
            }
            range->Release();
        }) || !committed) return;
    } else if (m_pComposition) {
        bool ended = false;
        if (!RunEditSession(pic, m_clientId, [this, pic, text, &ended](TfEditCookie ec) {
            ended = _EndComposition(ec, pic, text);
        }) || !ended) return;
    } else {
        // No composition active — insert text directly
        bool inserted = false;
        if (!RunEditSession(pic, m_clientId, [this, pic, text, remainder, &inserted](TfEditCookie ec) {
            inserted = _InsertText(ec, pic, text);
            if (inserted && !remainder.empty()) _StartOrUpdateComposition(ec, pic, remainder);
        }) || !inserted) return;
    }
    if (m_settings.learnChoices) RememberSelection(spelling, text);
    if (remainder.empty()) _Reset();
    else {
        m_buffer = remainder;
        _RequestQuery();
    }
}

void CantoneseIME::_Reset() {
    if (m_hMarshalWnd) KillTimer(m_hMarshalWnd, QUERY_TIMER);
    m_queryScheduled = false;
    m_deferredCandidate = -1;
    m_queuedKeys.clear();
    m_togglePending = false;
    {
        std::lock_guard<std::mutex> lock(m_queryState->mutex);
        ++m_queryState->generation;
        m_queryState->ready = false;
    }
    m_buffer.clear();
    m_candidates.clear();
    m_queryPending = false;
    m_cached = m_offline = false;
    m_candPage = 0;
    m_caretValid = false;
    if (m_candWnd) m_candWnd->Hide();
}

// Capture caret position while we already hold an edit cookie.
// Nested RunEditSession would fail with TF_E_LOCKED (0x80040209), which is why
// the old code always fell back to {100,100}.
bool CantoneseIME::_CaptureCaretPos(TfEditCookie ec, ITfContext* pic) {
    if (!pic) return false;
    TF_SELECTION sel;
    ULONG fetched = 0;
    if (SUCCEEDED(pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched) {
        ITfContextView* pView = nullptr;
        if (SUCCEEDED(pic->GetActiveView(&pView)) && pView) {
            RECT rc;
            BOOL clipped;
            if (SUCCEEDED(pView->GetTextExt(ec, sel.range, &rc, &clipped))
                && !clipped && (rc.left || rc.top || rc.right || rc.bottom)) {
                m_lastCaretPt.x = rc.left;
                m_lastCaretPt.y = rc.bottom;
                m_caretValid = true;
                pView->Release();
                sel.range->Release();
                return true;
            }
            pView->Release();
        }
        sel.range->Release();
    }
    return false;
}

void CantoneseIME::_RefreshCaretPos() {
    if (!m_pContext) return;
    // Layout callbacks arrive after composition text is rendered. A read-only
    // session here uses the current selection and yields screen coordinates.
    m_caretValid = false;
    auto* context = m_pContext;
    auto* session = new (std::nothrow) FuncEditSession([this, context](TfEditCookie ec) {
        _CaptureCaretPos(ec, context);
    });
    if (session) {
        HRESULT sessionResult = E_FAIL;
        context->RequestEditSession(m_clientId, session, TF_ES_SYNC | TF_ES_READ, &sessionResult);
        session->Release();
    }
    if (m_caretValid) return;
    GUITHREADINFO info = { sizeof(info) };
    HWND foreground = GetForegroundWindow();
    if (foreground && GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info)
        && info.hwndCaret) {
        POINT point = { info.rcCaret.left, info.rcCaret.bottom };
        if (ClientToScreen(info.hwndCaret, &point)) {
            m_lastCaretPt = point;
            m_caretValid = true;
        }
    }
}

LRESULT CALLBACK CantoneseIME::_MarshalWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == QUERY_TIMER) {
        auto* self = reinterpret_cast<CantoneseIME*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) self->_SendQuery();
        return 0;
    }
    if (msg == WM_CANDIDATES_READY) {
        auto* self = reinterpret_cast<CantoneseIME*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) self->_ReceiveCandidates();
        return 0;
    }
    if (msg == WM_LAYOUT_READY) {
        auto* self = reinterpret_cast<CantoneseIME*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && !self->m_buffer.empty()) self->_UpdateCandidateWindow();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
