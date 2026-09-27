#pragma once
#include "common.h"
#include "GoogleInputAPI.h"
#include "CandidateWindow.h"
#include "UserSettings.h"
#include <memory>
#include <mutex>

// The TSF text service. Implements the minimum interfaces for a
// composition-based IME:
//   ITfTextInputProcessorEx  - lifecycle (Activate/Deactivate)
//   ITfThreadMgrEventSink     - focus tracking
//   ITfKeyEventSink           - keyboard interception
//   ITfCompositionSink        - composition end notification
class CantoneseIME :
    public ITfTextInputProcessorEx,
    public ITfThreadMgrEventSink,
    public ITfKeyEventSink,
    public ITfCompositionSink,
    public ITfTextLayoutSink
{
public:
    CantoneseIME();
    ~CantoneseIME();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrev) override;
    STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wp, LPARAM lp, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;
    STDMETHODIMP OnLayoutChange(ITfContext* pic, TfLayoutCode code, ITfContextView* view) override;

    // internal helpers used by edit sessions
    ITfContext*     _GetContext()     { return m_pContext; }
    TfClientId      _GetClientId()    { return m_clientId; }
    ITfComposition* _GetComposition() { return m_pComposition; }
    void _SetComposition(ITfComposition* p) { m_pComposition = p; }

    // Composition state mutation (called inside edit sessions)
    void _StartOrUpdateComposition(TfEditCookie ec, ITfContext* pic, const std::wstring& text);
    void _EndComposition(TfEditCookie ec, ITfContext* pic, const std::wstring& commitText);
    void _InsertText(TfEditCookie ec, ITfContext* pic, const std::wstring& text);

private:
    bool _IsKeyEaten(WPARAM wp) const;
    void _RequestQuery();
    void _UpdateCandidateWindow();
    void _CommitCandidate(int index);
    void _Reset();
    bool _CaptureCaretPos(TfEditCookie ec, ITfContext* pic);
    void _RefreshCaretPos();
    POINT m_lastCaretPt = {};
    bool m_caretValid = false;
    void _SetContext(ITfContext* context);

    LONG m_refCount = 1;

    ITfThreadMgr*   m_pThreadMgr   = nullptr;
    TfClientId      m_clientId     = TF_CLIENTID_NULL;
    ITfContext*     m_pContext     = nullptr;
    ITfComposition* m_pComposition = nullptr;

    DWORD m_threadMgrCookie = TF_INVALID_COOKIE;
    DWORD m_layoutCookie = TF_INVALID_COOKIE;

    std::wstring              m_buffer;
    std::vector<Candidate> m_candidates;
    bool m_queryPending = false;
    bool m_cached = false;
    bool m_offline = false;
    UserSettings m_settings;

    std::unique_ptr<CandidateWindow> m_candWnd;

    DWORD m_uiThreadId = 0;

    // Shift toggles English/Chinese input mode
    bool m_englishMode = false;
    bool m_shiftPending = false;
    bool _ToggleMode(ITfContext* context);

    // Candidate paging
    int m_candPage = 0;
    void _NextCandPage();
    void _PrevCandPage();

    // Hidden HWND for marshaling background callbacks to UI thread
    HWND m_hMarshalWnd = nullptr;
    struct QueryState {
        std::mutex mutex;
        HWND window = nullptr;
        unsigned long long generation = 0;
        CandidateResult result;
        bool ready = false;
    };
    std::shared_ptr<QueryState> m_queryState = std::make_shared<QueryState>();
    void _ReceiveCandidates();
    static LRESULT CALLBACK _MarshalWndProc(HWND, UINT, WPARAM, LPARAM);
    enum { WM_CANDIDATES_READY = WM_APP + 1 };
    enum { WM_LAYOUT_READY = WM_APP + 2 };
};
