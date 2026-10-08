#include "CantoneseIME.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

// Exercise the real IME keyboard/composition code with an in-memory TSF host.
// No registration, user settings writes, visible windows, or network required.
LONG g_cDllRef = 0;
HINSTANCE g_hInst = GetModuleHandleW(nullptr);
static std::vector<CandidateCallback> requests;
static std::vector<std::wstring> spellings;
static int hidden = 0;
void GoogleInputAPI::QueryAsync(const std::wstring& spelling, CandidateCallback callback) {
    spellings.push_back(spelling);
    requests.push_back(std::move(callback));
}
CandidateWindow::CandidateWindow() = default;
CandidateWindow::~CandidateWindow() = default;
bool CandidateWindow::RegisterClass(HINSTANCE) { return true; }
void CandidateWindow::Hide() { ++hidden; }
void CandidateWindow::Show(const std::wstring&, const std::vector<Candidate>&, POINT,
    int, int, bool, bool, bool) {}

#define UNUSED_METHOD(name, args) STDMETHODIMP name args override { return E_NOTIMPL; }
#define REFCOUNTED \
    ULONG refs = 1; \
    STDMETHODIMP QueryInterface(REFIID, void** out) override { *out = nullptr; return E_NOINTERFACE; } \
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; } \
    STDMETHODIMP_(ULONG) Release() override { auto n = --refs; if (!n) delete this; return n; }

struct Document {
    std::wstring text;
    LONG caret = 0, start = 0, end = 0;
    bool active = false, refuseShift = false, refuseWrite = false, refuseSession = false;
    DWORD flags = 0;
    HRESULT statusResult = S_OK;
    bool hasInsert = true, keyboardDisabled = false, emptyContext = false;
};
class Compartment : public ITfCompartment {
public:
    bool& flag;
    explicit Compartment(bool& value) : flag(value) {}
    REFCOUNTED
    STDMETHODIMP GetValue(VARIANT* value) override {
        VariantInit(value); value->vt = VT_I4; value->lVal = flag ? 1 : 0; return S_OK;
    }
    UNUSED_METHOD(SetValue, (TfClientId, const VARIANT*))
};
class Range : public ITfRange {
public:
    Document& doc;
    LONG start, end;
    Range(Document& d, LONG s, LONG e) : doc(d), start(s), end(e) {}
    REFCOUNTED
    STDMETHODIMP SetText(TfEditCookie, DWORD, const WCHAR* value, LONG count) override {
        if (doc.refuseWrite) return E_ACCESSDENIED;
        const LONG delta = count - (end - start);
        doc.text.replace(start, end - start, value, count);
        if (doc.active) doc.end += delta;
        end = start + count;
        return S_OK;
    }
    STDMETHODIMP ShiftEnd(TfEditCookie, LONG count, LONG* shifted, const TF_HALTCOND*) override {
        const LONG next = (std::clamp)(end + count, start, LONG(doc.text.size()));
        *shifted = next - end; end = next; return S_OK;
    }
    STDMETHODIMP ShiftStart(TfEditCookie, LONG count, LONG* shifted, const TF_HALTCOND*) override {
        const LONG next = (std::clamp)(start + count, LONG(0), end);
        *shifted = next - start; start = next; return S_OK;
    }
    STDMETHODIMP Collapse(TfEditCookie, TfAnchor anchor) override {
        if (anchor == TF_ANCHOR_END) start = end; else end = start;
        return S_OK;
    }
    STDMETHODIMP Clone(ITfRange** out) override { *out = new Range(doc, start, end); return S_OK; }
    UNUSED_METHOD(GetText, (TfEditCookie, DWORD, WCHAR*, ULONG, ULONG*))
    UNUSED_METHOD(GetFormattedText, (TfEditCookie, IDataObject**))
    UNUSED_METHOD(GetEmbedded, (TfEditCookie, REFGUID, REFIID, IUnknown**))
    UNUSED_METHOD(InsertEmbedded, (TfEditCookie, DWORD, IDataObject*))
    UNUSED_METHOD(ShiftStartToRange, (TfEditCookie, ITfRange*, TfAnchor))
    UNUSED_METHOD(ShiftEndToRange, (TfEditCookie, ITfRange*, TfAnchor))
    UNUSED_METHOD(ShiftStartRegion, (TfEditCookie, TfShiftDir, BOOL*))
    UNUSED_METHOD(ShiftEndRegion, (TfEditCookie, TfShiftDir, BOOL*))
    UNUSED_METHOD(IsEmpty, (TfEditCookie, BOOL*))
    UNUSED_METHOD(IsEqualStart, (TfEditCookie, ITfRange*, TfAnchor, BOOL*))
    UNUSED_METHOD(IsEqualEnd, (TfEditCookie, ITfRange*, TfAnchor, BOOL*))
    UNUSED_METHOD(CompareStart, (TfEditCookie, ITfRange*, TfAnchor, LONG*))
    UNUSED_METHOD(CompareEnd, (TfEditCookie, ITfRange*, TfAnchor, LONG*))
    UNUSED_METHOD(AdjustForInsert, (TfEditCookie, ULONG, BOOL*))
    UNUSED_METHOD(GetGravity, (TfGravity*, TfGravity*))
    UNUSED_METHOD(SetGravity, (TfEditCookie, TfGravity, TfGravity))
    UNUSED_METHOD(GetContext, (ITfContext**))
};
class Composition : public ITfComposition {
public:
    Document& doc;
    ITfCompositionSink* sink;
    Composition(Document& d, ITfCompositionSink* s) : doc(d), sink(s) {}
    REFCOUNTED
    STDMETHODIMP GetRange(ITfRange** out) override { *out = new Range(doc, doc.start, doc.end); return S_OK; }
    STDMETHODIMP ShiftStart(TfEditCookie, ITfRange* range) override {
        if (doc.refuseShift) return E_FAIL;
        doc.start = static_cast<Range*>(range)->start; return S_OK;
    }
    UNUSED_METHOD(ShiftEnd, (TfEditCookie, ITfRange*))
    STDMETHODIMP EndComposition(TfEditCookie cookie) override {
        doc.active = false;
        sink->OnCompositionTerminated(cookie, this); // Deliberately reentrant.
        return S_OK;
    }
};
class Context : public ITfContext, public ITfInsertAtSelection, public ITfContextComposition, public ITfCompartmentMgr {
public:
    Document doc;
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        *out = nullptr;
        if (iid == IID_ITfInsertAtSelection && doc.hasInsert) *out = static_cast<ITfInsertAtSelection*>(this);
        if (iid == IID_ITfCompartmentMgr) *out = static_cast<ITfCompartmentMgr*>(this);
        if (iid == IID_ITfContextComposition) *out = static_cast<ITfContextComposition*>(this);
        if (iid == IID_ITfContext || iid == IID_IUnknown) *out = static_cast<ITfContext*>(this);
        return *out ? S_OK : E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP RequestEditSession(TfClientId, ITfEditSession* session, DWORD, HRESULT* result) override {
        if (doc.refuseSession) { *result = TF_E_LOCKED; return S_OK; }
        *result = session->DoEditSession(1); return S_OK;
    }
    STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION* selection, ULONG* fetched) override {
        selection->range = new Range(doc, doc.caret, doc.caret);
        *fetched = 1; return S_OK;
    }
    STDMETHODIMP SetSelection(TfEditCookie, ULONG, const TF_SELECTION* selection) override {
        doc.caret = static_cast<Range*>(selection->range)->end; return S_OK;
    }
    STDMETHODIMP InsertTextAtSelection(TfEditCookie cookie, DWORD flags, const WCHAR* text,
        LONG length, ITfRange** out) override {
        auto range = new Range(doc, doc.caret, doc.caret);
        if (!(flags & TF_IAS_QUERYONLY)) {
            auto hr = range->SetText(cookie, 0, text, length);
            if (FAILED(hr)) { range->Release(); *out = nullptr; return hr; }
        }
        *out = range; return S_OK;
    }
    STDMETHODIMP StartComposition(TfEditCookie, ITfRange* range, ITfCompositionSink* sink,
        ITfComposition** out) override {
        doc.active = true;
        doc.start = static_cast<Range*>(range)->start;
        doc.end = static_cast<Range*>(range)->end;
        *out = new Composition(doc, sink); return S_OK;
    }
    UNUSED_METHOD(InWriteSession, (TfClientId, BOOL*))
    UNUSED_METHOD(GetStart, (TfEditCookie, ITfRange**))
    UNUSED_METHOD(GetEnd, (TfEditCookie, ITfRange**))
    UNUSED_METHOD(GetActiveView, (ITfContextView**))
    UNUSED_METHOD(EnumViews, (IEnumTfContextViews**))
    STDMETHODIMP GetStatus(TF_STATUS* status) override {
        *status = {}; status->dwDynamicFlags = doc.flags; return doc.statusResult;
    }
    STDMETHODIMP GetCompartment(REFGUID id, ITfCompartment** out) override {
        *out = nullptr;
        if (id == GUID_COMPARTMENT_KEYBOARD_DISABLED) *out = new Compartment(doc.keyboardDisabled);
        else if (id == GUID_COMPARTMENT_EMPTYCONTEXT) *out = new Compartment(doc.emptyContext);
        return *out ? S_OK : E_INVALIDARG;
    }
    UNUSED_METHOD(ClearCompartment, (TfClientId, REFGUID))
    UNUSED_METHOD(EnumCompartments, (IEnumGUID**))
    UNUSED_METHOD(GetProperty, (REFGUID, ITfProperty**))
    UNUSED_METHOD(GetAppProperty, (REFGUID, ITfReadOnlyProperty**))
    UNUSED_METHOD(TrackProperties, (const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**))
    UNUSED_METHOD(EnumProperties, (IEnumTfProperties**))
    UNUSED_METHOD(GetDocumentMgr, (ITfDocumentMgr**))
    UNUSED_METHOD(CreateRangeBackup, (TfEditCookie, ITfRange*, ITfRangeBackup**))
    UNUSED_METHOD(InsertEmbeddedAtSelection, (TfEditCookie, DWORD, IDataObject*, ITfRange**))
    UNUSED_METHOD(EnumCompositions, (IEnumITfCompositionView**))
    UNUSED_METHOD(FindComposition, (TfEditCookie, ITfRange*, IEnumITfCompositionView**))
    UNUSED_METHOD(TakeOwnership, (TfEditCookie, ITfCompositionView*, ITfCompositionSink*, ITfComposition**))
};

static void Check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
class InputTest {
    Context context;
    CantoneseIME ime;
public:
    InputTest() {
        ime._SetContext(&context);
        ime.m_caretValid = true;
        ime.m_lastCaretPt = {100, 100};
        ime.m_settings.learnChoices = false;
        WNDCLASSW wc = {};
        wc.hInstance = g_hInst; wc.lpfnWndProc = CantoneseIME::_MarshalWndProc;
        wc.lpszClassName = L"YutpingInputTest";
        RegisterClassW(&wc);
        ime.m_hMarshalWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0,
            0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hInst, nullptr);
        Check(ime.m_hMarshalWnd != nullptr, "marshal window");
        SetWindowLongPtrW(ime.m_hMarshalWnd, GWLP_USERDATA, LONG_PTR(&ime));
        ime.m_queryState->window = ime.m_hMarshalWnd;
    }
    ~InputTest() { ime.Deactivate(); }
    void Key(WPARAM key) {
        BOOL eaten = FALSE;
        ime.OnKeyDown(&context, key, 0, &eaten);
        Check(eaten != FALSE, "expected key consumed");
    }
    void Type(const char* text) { while (*text) Key(*text++ - 'a' + 'A'); }
    void Result(std::vector<Candidate> candidates) {
        ime._SendQuery();
        Check(!requests.empty(), "expected query");
        requests.back()({ std::move(candidates), false, false });
        ime._ReceiveCandidates();
    }
    static void Run() {
        {
            InputTest t;
            const auto before = requests.size();
            t.Type("neiho");
            Check(requests.size() == before, "typing burst must coalesce");
            t.Result({ {L"你好", L"nei hou", 5}, {L"你可", L"nei ho", 5},
                {L"您可", L"nei ho", 5}, {L"您好", L"nei hou", 5},
                {L"妳好", L"nei hou", 5}, {L"你", L"nei", 3} });
            Check(requests.size() == before + 1, "only final spelling requested");
            auto* composition = t.ime.m_pComposition;
            const int beforeHide = hidden;
            t.Key('6');
            Check(t.context.doc.text == L"你ho" && t.ime.m_buffer == L"ho", "prefix preserves ho");
            Check(t.ime.m_pComposition == composition && t.context.doc.start == 1, "composition stays open");
            Check(hidden == beforeHide, "partial selection does not hide popup");
            t.Key(VK_SPACE);
            t.Result({ {L"好", L"hou", 2} });
            Check(t.context.doc.text == L"你好" && t.ime.m_buffer.empty(), "continuous selection");
        }
        {
            InputTest t;
            t.Type("neiho");
            t.Key(VK_SPACE);
            Check(t.context.doc.text == L"neiho" && t.ime.m_deferredCandidate == 0, "Space waits for candidates");
            t.Type("ho"); t.Key(VK_SPACE);
            t.Result({ {L"你好", L"nei hou", 5} });
            Check(t.context.doc.text == L"你好ho" && t.ime.m_buffer == L"ho", "later typing preserved");
            t.Result({ {L"好", L"hou", 2} });
            Check(t.context.doc.text == L"你好好" && t.ime.m_buffer.empty(), "queued Space selects new word");
        }
        for (int size : {4, 6, 9}) {
            InputTest t;
            t.ime.m_settings.pageSize = size;
            t.Type("neiho");
            std::vector<Candidate> candidates;
            for (int i = 0; i < 27; ++i) candidates.push_back({ L"word" + std::to_wstring(i), L"", 5 });
            t.Result(candidates);
            t.Key(VK_OEM_PLUS); t.Key(VK_SPACE);
            Check(t.context.doc.text == L"word" + std::to_wstring(size), "Space selects current page");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Key(VK_OEM_PLUS); t.Key(VK_SPACE);
            std::vector<Candidate> candidates;
            for (int i = 0; i < 12; ++i) candidates.push_back({ L"word" + std::to_wstring(i), L"", 5 });
            t.Result(candidates);
            Check(t.context.doc.text == L"word6", "paging before response preserves key order");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Key(VK_SPACE); t.Key(VK_ESCAPE);
            requests.back()({ {{L"你好", L"", 5}}, false, false });
            t.ime._ReceiveCandidates();
            Check(t.context.doc.text.empty() && t.ime.m_buffer.empty(), "cancel invalidates late response");
            t.Type("neiho"); t.Key(VK_RETURN);
            Check(t.context.doc.text == L"neiho" && t.ime.m_buffer.empty(), "Enter stays raw");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Result({ {L"你好", L"", 5} });
            t.context.doc.refuseSession = true;
            t.Key(VK_SPACE);
            Check(t.ime.m_buffer == L"neiho", "failed edit keeps spelling");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Result({ {L"你好", L"", 5} });
            t.context.doc.refuseWrite = true;
            t.Key(VK_SPACE);
            Check(t.ime.m_buffer == L"neiho" && t.context.doc.active, "failed SetText keeps composition");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Key(VK_SPACE); t.Key(VK_BACK);
            t.Result({ {L"你好", L"", 5} });
            Check(t.context.doc.text == L"你", "queued Backspace deletes committed character");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Key(VK_SPACE); t.Key('0'); t.Key(VK_OEM_1);
            t.Result({ {L"你好", L"", 5} });
            Check(t.context.doc.text == L"你好0；", "digits and punctuation after pending Space are preserved");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Key(VK_SPACE); t.Result({});
            Check(t.ime.m_buffer == L"neiho" && t.context.doc.active, "no result cannot auto-commit English");
        }
        {
            InputTest t;
            t.Type("nei");
            BOOL eaten = FALSE;
            t.ime._HandleKeyDown(&t.context, VK_OEM_7, false, &eaten);
            t.Type("ho");
            t.Result({ {L"你", L"nei", 3} });
            t.Key('1');
            Check(t.context.doc.text == L"你ho" && t.ime.m_buffer == L"ho", "separator removed after partial commit");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Result({ {L"你", L"nei", 3} });
            t.context.doc.refuseShift = true;
            t.Key('1');
            Check(t.context.doc.text == L"neiho" && t.ime.m_buffer == L"neiho", "host refusal restores raw spelling");
        }
        {
            InputTest t;
            auto& ime = t.ime;
            ime._ObserveToggleDown(VK_SHIFT, 0, false, true, false);
            Check(!ime._ReleaseToggle(VK_SHIFT), "Shift alone cannot toggle");
            ime._ObserveToggleDown(VK_CONTROL, 0, true, false, false);
            ime._ObserveToggleDown(VK_SHIFT, 0, true, true, false);
            Check(ime._ReleaseToggle(VK_SHIFT), "Ctrl then Shift toggles");
            Check(!ime._ReleaseToggle(VK_CONTROL), "second modifier release cannot toggle again");
            ime._ObserveToggleDown(VK_RSHIFT, 0, false, true, false);
            ime._ObserveToggleDown(VK_LCONTROL, 0, true, true, false);
            Check(ime._ReleaseToggle(VK_LCONTROL), "Shift then Ctrl toggles");
            ime._ObserveToggleDown(VK_SHIFT, 0, true, true, false);
            ime._ObserveToggleDown('W', 0, true, true, false);
            Check(!ime._ReleaseToggle(VK_SHIFT), "Ctrl Shift W cannot toggle");
            ime._ObserveToggleDown(VK_SHIFT, LPARAM(1) << 30, true, true, false);
            Check(!ime._ReleaseToggle(VK_SHIFT), "repeat cannot rearm used chord");
            ime._ObserveToggleDown(VK_SHIFT, 0, true, true, true);
            Check(!ime._ReleaseToggle(VK_SHIFT), "Alt chord cannot toggle");
            ime.m_settings.f12Toggle = true;
            ime._ObserveToggleDown(VK_SHIFT, 0, true, true, false);
            Check(!ime._ReleaseToggle(VK_SHIFT), "F12 preference disables chord");
        }
        {
            InputTest t;
            auto& ime = t.ime;
            Check(ime._CanAcceptInput(&t.context), "editable chat field accepts input");
            Check(!ime._CanAcceptInput(nullptr), "no text context bypasses IME");
            const auto before = requests.size();
            for (int state = 0; state < 5; ++state) {
                t.context.doc.flags = state == 0 ? TF_SD_READONLY : 0;
                t.context.doc.keyboardDisabled = state == 1;
                t.context.doc.emptyContext = state == 2;
                t.context.doc.hasInsert = state != 3;
                t.context.doc.statusResult = state == 4 ? TF_E_DISCONNECTED : S_OK;
                Check(!ime._CanAcceptInput(&t.context), "non-editable context bypasses input");
                for (WPARAM key : std::initializer_list<WPARAM>{'W', 'A', 'S', 'D', VK_SPACE, VK_OEM_PERIOD}) {
                    BOOL eaten = TRUE;
                    ime.OnTestKeyDown(&t.context, key, 0, &eaten);
                    Check(!eaten, "game control not claimed by test callback");
                    ime.OnKeyDown(&t.context, key, 0, &eaten);
                    Check(!eaten && ime.m_buffer.empty(), "game control not consumed");
                }
            }
            Check(requests.size() == before && t.context.doc.text.empty(), "game controls never query Google or insert text");
            t.context.doc = {};
            t.ime.OnSetFocus(FALSE);
            Check(!ime._CanAcceptInput(&t.context), "background service cannot intercept controls");
            t.ime.OnSetFocus(TRUE);
            Check(ime._CanAcceptInput(&t.context), "editable focus resumes input");
            ime.m_settings.learnChoices = false;
            t.Type("neiho"); t.Result({ {L"你好", L"", 5} }); t.Key(VK_SPACE);
            Check(t.context.doc.text == L"你好", "chat resumes normal candidate selection");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Key(VK_SPACE);
            const auto callback = requests.back();
            t.context.doc.keyboardDisabled = true;
            callback({ {{L"你好", L"", 5}}, false, false });
            t.ime._ReceiveCandidates();
            Check(t.context.doc.text == L"neiho" && t.ime.m_buffer.empty(), "late reply cannot write into disabled game context");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Key(VK_SPACE); t.Type("ho");
            const auto callback = requests.back();
            Check(t.ime._ToggleMode(&t.context) && t.ime.m_englishMode, "mode switch works while query pending");
            Check(t.context.doc.text == L"neihoho" && t.ime.m_buffer.empty(), "switch preserves pending and queued spelling");
            callback({ {{L"你好", L"", 5}}, false, false });
            t.ime._ReceiveCandidates();
            Check(t.context.doc.text == L"neihoho", "late query cannot convert after mode switch");
            for (WPARAM key : std::initializer_list<WPARAM>{'W', 'A', 'S', 'D', VK_SPACE}) {
                BOOL eaten = TRUE;
                t.ime.OnKeyDown(&t.context, key, 0, &eaten);
                Check(!eaten, "English mode leaves control keys alone");
            }
        }
        for (bool refuseSession : {false, true}) {
            InputTest t;
            t.context.doc.refuseSession = refuseSession;
            t.context.doc.refuseWrite = !refuseSession;
            const auto before = requests.size();
            for (WPARAM key : std::initializer_list<WPARAM>{'W', 'A', 'S', 'D', VK_OEM_PERIOD}) {
                BOOL eaten = TRUE;
                t.ime.OnKeyDown(&t.context, key, 0, &eaten);
                Check(!eaten && t.ime.m_buffer.empty(), "refused text writes pass controls to host");
            }
            Check(requests.size() == before && t.context.doc.text.empty(), "refused writes do not query or change text");
            Check(!t.context.doc.active, "refused first letter leaves no empty composition");
        }
        {
            InputTest t;
            t.Type("neiho"); t.Key(VK_SPACE);
            const auto callback = requests.back();
            t.context.doc.keyboardDisabled = true;
            Check(t.ime._ToggleMode(&t.context) && t.ime.m_englishMode, "manual switch works after editable focus disappears");
            callback({ {{L"你好", L"", 5}}, false, false });
            t.ime._ReceiveCandidates();
            Check(t.context.doc.text == L"neiho" && t.ime.m_buffer.empty(), "switch never writes into disabled context");
        }
        std::cout << "Input behavior checks passed\n";
    }
};
int main() {
    try { InputTest::Run(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
