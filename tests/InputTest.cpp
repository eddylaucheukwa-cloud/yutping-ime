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
class Context : public ITfContext, public ITfInsertAtSelection, public ITfContextComposition {
public:
    Document doc;
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        *out = nullptr;
        if (iid == IID_ITfInsertAtSelection) *out = static_cast<ITfInsertAtSelection*>(this);
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
    UNUSED_METHOD(GetStatus, (TF_STATUS*))
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
            ime.m_shiftPending = true; ime.m_shiftPressedAt = 1000;
            Check(ime._ReleaseShift(1150), "short lone Shift toggles");
            Check(!ime._ReleaseShift(1160), "duplicate keyup cannot toggle");
            ime.m_shiftPending = true; ime.m_shiftPressedAt = 1000;
            Check(!ime._ReleaseShift(1500), "held Shift does not toggle");
            ime.m_shiftPending = true;
            ime._ObserveShiftDown('A', 0, false, 1100);
            Check(!ime._ReleaseShift(1200), "uppercase chord does not toggle");
            ime.m_shiftPending = false;
            ime._ObserveShiftDown(VK_SHIFT, LPARAM(1) << 30, false, 1200);
            Check(!ime._ReleaseShift(1300), "Shift repeat does not rearm");
            ime._ObserveShiftDown(VK_LSHIFT, 0, true, 1400);
            Check(!ime._ReleaseShift(1450), "Ctrl/Alt Shift does not toggle");
        }
        std::cout << "Input behavior checks passed\n";
    }
};
int main() {
    try { InputTest::Run(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
