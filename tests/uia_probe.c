/*
 * Manual accessibility check: walks the TurkishDPI window with native UI
 * Automation (the API Narrator and NVDA use) and prints what a screen reader
 * gets. Needs the same integrity level as the app (run it elevated).
 *
 *   uia_probe dump                  list every element on the visible page
 *   uia_probe invoke <name-prefix>  press / toggle the first visible match
 *   uia_probe focus                 name of the element with keyboard focus
 *   uia_probe tray                  where the shell shows our notification icon
 *
 * Build: x86_64-w64-mingw32-gcc -O1 tests/uia_probe.c -lole32 -loleaut32 -luuid -o build/uia_probe.exe
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <uiautomationclient.h>
#include <stdio.h>
#include <wchar.h>

static IUIAutomation *g_uia;

static void out(const wchar_t *s) {
    char b[2048];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, b, sizeof(b) - 1, NULL, NULL);
    if (n > 0) fputs(b, stdout);
}

static const wchar_t *type_name(CONTROLTYPEID t) {
    switch (t) {
    case UIA_ButtonControlTypeId: return L"Button";
    case UIA_CheckBoxControlTypeId: return L"CheckBox";
    case UIA_EditControlTypeId: return L"Edit";
    case UIA_HyperlinkControlTypeId: return L"Hyperlink";
    case UIA_TextControlTypeId: return L"Text";
    case UIA_PaneControlTypeId: return L"Pane";
    case UIA_WindowControlTypeId: return L"Window";
    case UIA_GroupControlTypeId: return L"Group";
    case UIA_TabControlTypeId: return L"Tab";
    case UIA_TabItemControlTypeId: return L"TabItem";
    case UIA_ImageControlTypeId: return L"Image";
    case UIA_ProgressBarControlTypeId: return L"ProgressBar";
    case UIA_StatusBarControlTypeId: return L"StatusBar";
    default: return L"Other";
    }
}

static IUIAutomationElement *main_window(void) {
    HWND h = FindWindowW(L"TurkishDPI_main", NULL);
    if (!h) h = FindWindowW(L"TurkishDPI_preview", NULL);   /* --ui-preview, no admin needed */
    IUIAutomationElement *e = NULL;
    if (!h) return NULL;
    IUIAutomation_ElementFromHandle(g_uia, h, &e);
    return e;
}

static void describe(IUIAutomationElement *e, int depth) {
    BSTR name = NULL, help = NULL;
    CONTROLTYPEID t = 0;
    BOOL foc = FALSE, off = FALSE;
    IUnknown *p = NULL;
    wchar_t line[1024];
    IUIAutomationElement_get_CurrentName(e, &name);
    IUIAutomationElement_get_CurrentHelpText(e, &help);
    IUIAutomationElement_get_CurrentControlType(e, &t);
    IUIAutomationElement_get_CurrentIsKeyboardFocusable(e, &foc);
    IUIAutomationElement_get_CurrentIsOffscreen(e, &off);
    _snwprintf(line, 1023, L"%*ls%ls name='%ls'%ls%ls", depth * 2, L"", type_name(t),
               name ? name : L"", foc ? L" focusable" : L"", off ? L" OFFSCREEN" : L"");
    line[1023] = 0;
    out(line);
    if (help && help[0]) { out(L" help='"); out(help); out(L"'"); }
    if (SUCCEEDED(IUIAutomationElement_GetCurrentPattern(e, UIA_InvokePatternId, &p)) && p) {
        out(L" [invoke]");
        IUnknown_Release(p);
        p = NULL;
    }
    if (SUCCEEDED(IUIAutomationElement_GetCurrentPattern(e, UIA_TogglePatternId, &p)) && p) {
        enum ToggleState s = ToggleState_Indeterminate;
        IUIAutomationTogglePattern_get_CurrentToggleState((IUIAutomationTogglePattern *)p, &s);
        out(s == ToggleState_On ? L" [toggle: on]" : s == ToggleState_Off ? L" [toggle: off]" : L" [toggle: ?]");
        IUnknown_Release(p);
    }
    out(L"\n");
    SysFreeString(name);
    SysFreeString(help);
}

static void walk(IUIAutomationTreeWalker *w, IUIAutomationElement *e, int depth) {
    IUIAutomationElement *c = NULL, *n;
    describe(e, depth);
    IUIAutomationTreeWalker_GetFirstChildElement(w, e, &c);
    while (c) {
        BOOL off = FALSE;
        IUIAutomationElement_get_CurrentIsOffscreen(c, &off);
        if (!off) walk(w, c, depth + 1);
        n = NULL;
        IUIAutomationTreeWalker_GetNextSiblingElement(w, c, &n);
        IUIAutomationElement_Release(c);
        c = n;
    }
}

static IUIAutomationElement *find_prefix(IUIAutomationTreeWalker *w, IUIAutomationElement *e, const wchar_t *pre) {
    IUIAutomationElement *c = NULL, *n, *hit = NULL;
    IUIAutomationTreeWalker_GetFirstChildElement(w, e, &c);
    while (c && !hit) {
        BSTR name = NULL;
        BOOL off = FALSE;
        IUIAutomationElement_get_CurrentIsOffscreen(c, &off);
        IUIAutomationElement_get_CurrentName(c, &name);
        if (!off && name && !wcsncmp(name, pre, wcslen(pre))) {
            hit = c;
            IUIAutomationElement_AddRef(hit);
        } else if (!off) {
            hit = find_prefix(w, c, pre);
        }
        SysFreeString(name);
        n = NULL;
        IUIAutomationTreeWalker_GetNextSiblingElement(w, c, &n);
        IUIAutomationElement_Release(c);
        c = n;
    }
    if (c) IUIAutomationElement_Release(c);
    return hit;
}

int wmain(int argc, wchar_t **argv) {
    IUIAutomationElement *win;
    IUIAutomationTreeWalker *w = NULL;
    int rc = 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void **)&g_uia))) {
        puts("no UI Automation");
        return 2;
    }
    if (argc >= 2 && !wcscmp(argv[1], L"tray")) {
        NOTIFYICONIDENTIFIER id;
        RECT r;
        HRESULT hr;
        ZeroMemory(&id, sizeof(id));
        id.cbSize = sizeof(id);
        id.hWnd = FindWindowW(L"TurkishDPI_main", NULL);
        id.uID = 1;
        if (!id.hWnd) { puts("window not found"); return 3; }
        hr = Shell_NotifyIconGetRect(&id, &r);
        if (SUCCEEDED(hr)) printf("tray icon at %ld,%ld-%ld,%ld (%ldx%ld)\n", r.left, r.top, r.right, r.bottom,
                                  r.right - r.left, r.bottom - r.top);
        else printf("tray icon not found: 0x%08lx\n", (unsigned long)hr);
        return SUCCEEDED(hr) ? 0 : 7;
    }
    win = main_window();
    if (!win) { puts("window not found"); return 3; }
    IUIAutomation_get_ControlViewWalker(g_uia, &w);
    if (argc >= 2 && !wcscmp(argv[1], L"invoke") && argc >= 3) {
        IUIAutomationElement *e = find_prefix(w, win, argv[2]);
        IUnknown *p = NULL;
        if (!e) { out(L"not found: "); out(argv[2]); out(L"\n"); return 4; }
        describe(e, 0);
        /* BM_CLICK (behind Invoke) can be dropped while the window is not
         * active, e.g. under a full-screen game: bring it to the front first */
        IUIAutomationElement_SetFocus(win);
        if (SUCCEEDED(IUIAutomationElement_GetCurrentPattern(e, UIA_TogglePatternId, &p)) && p) {
            rc = FAILED(IUIAutomationTogglePattern_Toggle((IUIAutomationTogglePattern *)p)) ? 5 : 0;
        } else if (SUCCEEDED(IUIAutomationElement_GetCurrentPattern(e, UIA_InvokePatternId, &p)) && p) {
            rc = FAILED(IUIAutomationInvokePattern_Invoke((IUIAutomationInvokePattern *)p)) ? 5 : 0;
        } else {
            out(L"no invoke/toggle pattern\n");
            rc = 6;
        }
        if (p) IUnknown_Release(p);
        IUIAutomationElement_Release(e);
    } else if (argc >= 2 && !wcscmp(argv[1], L"focus")) {
        IUIAutomationElement *f = NULL;
        IUIAutomation_GetFocusedElement(g_uia, &f);
        if (f) { describe(f, 0); IUIAutomationElement_Release(f); }
    } else {
        walk(w, win, 0);
    }
    IUIAutomationTreeWalker_Release(w);
    IUIAutomationElement_Release(win);
    IUIAutomation_Release(g_uia);
    CoUninitialize();
    return rc;
}
