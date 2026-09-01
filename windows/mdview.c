/*
 * mdview — Windows build (Win32 + WebView2 + RichEdit)
 *
 * Reuses the Markdown->HTML parser from the Linux build (glib-based).
 *
 * Build (MSYS2 / MinGW-w64):
 *   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf \
 *             mingw-w64-x86_64-glib2 mingw-w64-x86_64-winpthreads
 *   # download WebView2.h + WebView2Loader.dll (see download-webview2.ps1)
 *   ./build-mingw.sh
 *   # produces mdview.exe (WebView2Loader.dll must sit next to it)
 *   # needs the WebView2 Runtime: preinstalled on Windows 11 / Edge systems
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <richedit.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <glib.h>

#include "WebView2.h"
#include "parser.c"

/* ------------------------------------------------------------------ */
/* WebView2 loader (dynamic — no .lib import needed)                   */
/* ------------------------------------------------------------------ */
typedef HRESULT(STDAPICALLTYPE *CreateCoreWebView2EnvironmentWithOptionsFn)(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions *environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler);

static CreateCoreWebView2EnvironmentWithOptionsFn g_createEnv = NULL;
static ICoreWebView2Environment *g_env = NULL;

/* ------------------------------------------------------------------ */
/* IDs                                                                 */
/* ------------------------------------------------------------------ */
#define ID_OPEN       1001
#define ID_NEWTAB     1002
#define ID_CLOSETAB   1003
#define ID_TOGGLE     1004
#define ID_COPY       1005
#define ID_SAVE       1006
#define ID_ZOOMIN     1007
#define ID_ZOOMOUT    1008
#define ID_ZOOMRESET  1009

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
typedef struct TabWin {
    HWND hwnd;      /* content container */
    HWND hEdit;     /* RichEdit editor   */
    HWND hWebHost;  /* WebView2 host     */
    ICoreWebView2 *webview;
    ICoreWebView2Controller *controller;
    WCHAR *path;
    char  *source;  /* UTF-8             */
    BOOL  editing;
} TabWin;

static HINSTANCE g_hInst = NULL;
static HWND g_hwnd = NULL, g_hTab = NULL;
static HWND g_btnOpen, g_btnNew, g_btnClose, g_btnToggle, g_btnCopy, g_hZoomLabel;
static HACCEL g_hAccel = NULL;
static double g_zoom = 1.0;
static TabWin **g_tabs = NULL;
static int g_tab_count = 0, g_tab_cap = 0, g_current = -1;

static const WCHAR *CSS_DARK =
    L"body{font-family:ui-sans-serif,system-ui,-apple-system,'Segoe UI',Roboto,Arial,sans-serif;"
    L"max-width:840px;margin:0 auto;padding:40px 28px 80px;line-height:1.7;color:#e6edf3;background:#0d1117;"
    L"word-wrap:break-word;}"
    L"h1,h2,h3,h4{line-height:1.3;margin-top:1.6em;margin-bottom:.6em;font-weight:600}"
    L"h1{font-size:1.9em;padding-bottom:.35em;border-bottom:1px solid #21262d}"
    L"h2{font-size:1.5em;padding-bottom:.3em;border-bottom:1px solid #21262d}"
    L"h3{font-size:1.22em}h4{font-size:1.05em}"
    L"a{color:#58a6ff;text-decoration:none}a:hover{text-decoration:underline}"
    L"code{background:#21262d;border-radius:5px;padding:.2em .45em;font-family:Consolas,monospace;font-size:.88em}"
    L"pre{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px 18px;overflow-x:auto;line-height:1.55}"
    L"pre code{background:transparent;padding:0;font-size:.86em}"
    L"blockquote{border-left:4px solid #3d444d;margin:1.2em 0;padding:.1em 1.1em;color:#9198a1}"
    L"ul,ol{padding-left:1.5em;margin:.6em 0 1.2em}li{margin:.3em 0}"
    L"table{border-collapse:collapse;margin:1.2em 0;display:block;overflow-x:auto;font-size:.95em}"
    L"th,td{border:1px solid #30363d;padding:8px 14px}th{background:#161b22;font-weight:600}"
    L"hr{border:0;border-top:1px solid #30363d;margin:2em 0}img{max-width:100%}del{color:#8b949e}";

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static TabWin *new_tab(const WCHAR *path);
static void open_file(const WCHAR *path);
static void switch_tab(int idx);
static void create_controller(TabWin *t);
static void show_preview(TabWin *t);
static void refresh_ui(void);

/* ------------------------------------------------------------------ */
/* UTF-8 / UTF-16 helpers                                              */
/* ------------------------------------------------------------------ */
static char *wide_to_utf8(const WCHAR *s) {
    if (!s) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *out = g_malloc((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out, n, NULL, NULL);
    return out;
}

static WCHAR *utf8_to_wide(const char *s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    WCHAR *out = g_malloc(sizeof(WCHAR) * (size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n);
    return out;
}

/* ------------------------------------------------------------------ */
/* File helpers                                                        */
/* ------------------------------------------------------------------ */
static char *read_file_utf8(const WCHAR *path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(h, NULL);
    char *buf = g_malloc(size + 1);
    DWORD rd = 0;
    ReadFile(h, buf, size, &rd, NULL);
    CloseHandle(h);
    buf[rd] = 0;
    return buf;
}

static int write_file_utf8(const WCHAR *path, const char *data, int len) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD wr = 0;
    WriteFile(h, data, len < 0 ? (DWORD)strlen(data) : (DWORD)len, &wr, NULL);
    CloseHandle(h);
    return 1;
}

static WCHAR *dir_of(const WCHAR *path) {
    WCHAR *p = _wcsdup(path);
    PathRemoveFileSpecW(p);
    return p;
}

static int has_md_txt_ext(const WCHAR *path) {
    WCHAR *dot = wcsrchr(path, L'.');
    if (!dot) return 0;
    if (_wcsicmp(dot, L".md") == 0 || _wcsicmp(dot, L".markdown") == 0 ||
        _wcsicmp(dot, L".txt") == 0 || _wcsicmp(dot, L".text") == 0)
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* RichEdit editor                                                     */
/* ------------------------------------------------------------------ */
static void editor_set_theme(HWND hEdit, double zoom) {
    if (!hEdit) return;
    SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(13, 17, 23));
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = RGB(230, 237, 243);
    wcscpy(cf.szFaceName, L"Consolas");
    cf.yHeight = (LONG)(200 * zoom); /* 10pt = 200 twips */
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

static char *editor_get_text(TabWin *t) {
    int len = (int)SendMessageW(t->hEdit, WM_GETTEXTLENGTH, 0, 0);
    if (len <= 0) return g_strdup("");
    WCHAR *buf = g_malloc(sizeof(WCHAR) * ((size_t)len + 2));
    SendMessageW(t->hEdit, WM_GETTEXT, (WPARAM)(len + 1), (LPARAM)buf);
    char *utf8 = wide_to_utf8(buf);
    g_free(buf);
    return utf8 ? utf8 : g_strdup("");
}

/* ------------------------------------------------------------------ */
/* Preview (HTML)                                                      */
/* ------------------------------------------------------------------ */
static char *build_html_win(const char *source, const WCHAR *path) {
    GString *html = g_string_new(NULL);
    g_string_append(html, "<!DOCTYPE html><html><head><meta charset='utf-8'><base href=\"");
    if (path) {
        WCHAR *dir = dir_of(path);
        WCHAR uri[MAX_PATH];
        DWORD len = MAX_PATH;
        if (UrlCreateFromPathW(dir, uri, &len, 0) == S_OK) {
            char *u = wide_to_utf8(uri);
            if (u) { g_string_append(html, u); g_free(u); }
        }
        free(dir);
    }
    g_string_append(html, "\"/><style>");
    WCHAR *css = _wcsdup(CSS_DARK);
    char *css8 = wide_to_utf8(css);
    if (css8) { g_string_append(html, css8); g_free(css8); }
    free(css);
    g_string_append(html, "</style></head><body>");
    GString *body = markdown_to_html(source ? source : "");
    g_string_append_len(html, body->str, body->len);
    g_string_free(body, TRUE);
    g_string_append(html, "</body></html>");
    return g_string_free(html, FALSE);
}

static void show_preview(TabWin *t) {
    if (!t->webview) return;
    if (t->editing) {
        char *txt = editor_get_text(t);
        g_free(t->source);
        t->source = txt;
    }
    char *html = build_html_win(t->source ? t->source : "", t->path);
    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    WCHAR fn[MAX_PATH];
    GetTempFileNameW(tmp, L"mdv", 0, fn);
    write_file_utf8(fn, html, (int)strlen(html));
    g_free(html);
    WCHAR uri[MAX_PATH];
    DWORD len = MAX_PATH;
    if (UrlCreateFromPathW(fn, uri, &len, 0) == S_OK)
        t->webview->lpVtbl->Navigate(t->webview, uri);
}

/* ------------------------------------------------------------------ */
/* WebView2 COM handlers                                               */
/* ------------------------------------------------------------------ */
typedef struct { ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler base; ULONG ref; TabWin *tab; } EnvHandler;
typedef struct { ICoreWebView2CreateCoreWebView2ControllerCompletedHandler  base; ULONG ref; TabWin *tab; } CtrlHandler;
typedef struct { ICoreWebView2NavigationStartingEventHandler                 base; ULONG ref; TabWin *tab; } NavHandler;

static const ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl env_vtbl;
static const ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl  ctrl_vtbl;
static const ICoreWebView2NavigationStartingEventHandlerVtbl                nav_vtbl;

static HRESULT STDMETHODCALLTYPE env_QI(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self,
                                        REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
        *ppv = self;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE env_AddRef(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self) { return ++((EnvHandler *)self)->ref; }
static ULONG STDMETHODCALLTYPE env_Release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self) {
    ULONG r = --((EnvHandler *)self)->ref;
    if (r == 0) free(self);
    return r;
}

static HRESULT STDMETHODCALLTYPE ctrl_QI(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self,
                                         REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
        *ppv = self;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ctrl_AddRef(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self) { return ++((CtrlHandler *)self)->ref; }
static ULONG STDMETHODCALLTYPE ctrl_Release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self) {
    ULONG r = --((CtrlHandler *)self)->ref;
    if (r == 0) free(self);
    return r;
}

static HRESULT STDMETHODCALLTYPE nav_QI(ICoreWebView2NavigationStartingEventHandler *self, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_ICoreWebView2NavigationStartingEventHandler)) {
        *ppv = self;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE nav_AddRef(ICoreWebView2NavigationStartingEventHandler *self) { return ++((NavHandler *)self)->ref; }
static ULONG STDMETHODCALLTYPE nav_Release(ICoreWebView2NavigationStartingEventHandler *self) {
    ULONG r = --((NavHandler *)self)->ref;
    if (r == 0) free(self);
    return r;
}

static HRESULT STDMETHODCALLTYPE nav_Invoke(ICoreWebView2NavigationStartingEventHandler *self,
                                            ICoreWebView2 *webview,
                                            ICoreWebView2NavigationStartingEventArgs *args) {
    LPWSTR uri = NULL;
    args->lpVtbl->get_Uri(args, &uri);
    if (uri) {
        if (wcsncmp(uri, L"http://", 7) == 0 || wcsncmp(uri, L"https://", 8) == 0 ||
            wcsncmp(uri, L"mailto:", 7) == 0) {
            ShellExecuteW(NULL, L"open", uri, NULL, NULL, SW_SHOWNORMAL);
            args->lpVtbl->put_Cancel(args, TRUE);
        } else if (wcsncmp(uri, L"file://", 7) == 0) {
            WCHAR path[MAX_PATH];
            DWORD plen = MAX_PATH;
            if (PathCreateFromUrlW(uri, path, &plen, 0) == S_OK && has_md_txt_ext(path))
                open_file(path);
            args->lpVtbl->put_Cancel(args, TRUE);
        }
        CoTaskMemFree(uri);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ctrl_Invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self,
                                             HRESULT errorCode, ICoreWebView2Controller *controller) {
    CtrlHandler *h = (CtrlHandler *)self;
    TabWin *t = h->tab;
    if (SUCCEEDED(errorCode) && controller) {
        t->controller = controller;
        controller->lpVtbl->get_CoreWebView2(controller, &t->webview);
        controller->lpVtbl->put_ZoomFactor(controller, g_zoom);
        RECT rc;
        GetClientRect(t->hWebHost, &rc);
        controller->lpVtbl->put_Bounds(controller, rc);
        controller->lpVtbl->put_IsVisible(controller, t->editing ? FALSE : TRUE);
        NavHandler *nav = g_malloc(sizeof(NavHandler));
        nav->base.lpVtbl = &nav_vtbl;
        nav->ref = 1;
        nav->tab = t;
        EventRegistrationToken tok;
        t->webview->lpVtbl->add_NavigationStarting(t->webview, &nav->base, &tok);
        if (!t->editing) show_preview(t);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE env_Invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self,
                                            HRESULT errorCode, ICoreWebView2Environment *env) {
    EnvHandler *h = (EnvHandler *)self;
    if (SUCCEEDED(errorCode) && env) {
        g_env = env;
        create_controller(h->tab);
    }
    return S_OK;
}

static const ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl env_vtbl = {
    env_QI, env_AddRef, env_Release, env_Invoke
};
static const ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl ctrl_vtbl = {
    ctrl_QI, ctrl_AddRef, ctrl_Release, ctrl_Invoke
};
static const ICoreWebView2NavigationStartingEventHandlerVtbl nav_vtbl = {
    nav_QI, nav_AddRef, nav_Release, nav_Invoke
};

static void create_controller(TabWin *t) {
    if (!g_env) {
        if (!g_createEnv) {
            HMODULE m = LoadLibraryW(L"WebView2Loader.dll");
            if (m)
                g_createEnv = (CreateCoreWebView2EnvironmentWithOptionsFn)
                    GetProcAddress(m, "CreateCoreWebView2EnvironmentWithOptions");
        }
        if (!g_createEnv) return;
        EnvHandler *h = g_malloc(sizeof(EnvHandler));
        h->base.lpVtbl = &env_vtbl;
        h->ref = 1;
        h->tab = t;
        g_createEnv(NULL, NULL, NULL, &h->base);
        return;
    }
    CtrlHandler *h = g_malloc(sizeof(CtrlHandler));
    h->base.lpVtbl = &ctrl_vtbl;
    h->ref = 1;
    h->tab = t;
    g_env->lpVtbl->CreateCoreWebView2Controller(g_env, t->hWebHost, &h->base);
}

/* ------------------------------------------------------------------ */
/* Zoom                                                                */
/* ------------------------------------------------------------------ */
static void zoom_refresh(void) {
    WCHAR buf[32];
    wsprintfW(buf, L"%d%%", (int)(g_zoom * 100 + 0.5));
    SetWindowTextW(g_hZoomLabel, buf);
    for (int i = 0; i < g_tab_count; i++)
        editor_set_theme(g_tabs[i]->hEdit, g_zoom);
    if (g_current >= 0 && g_tabs[g_current]->controller)
        g_tabs[g_current]->controller->lpVtbl->put_ZoomFactor(g_tabs[g_current]->controller, g_zoom);
}

static void zoom_by(double d) {
    g_zoom += d;
    if (g_zoom < 0.5) g_zoom = 0.5;
    if (g_zoom > 4.0) g_zoom = 4.0;
    zoom_refresh();
}

/* ------------------------------------------------------------------ */
/* Tabs                                                                */
/* ------------------------------------------------------------------ */
static void apply_tab_mode(TabWin *t) {
    ShowWindow(t->hEdit, t->editing ? SW_SHOW : SW_HIDE);
    ShowWindow(t->hWebHost, t->editing ? SW_HIDE : SW_SHOW);
    if (t->controller)
        t->controller->lpVtbl->put_IsVisible(t->controller, t->editing ? FALSE : TRUE);
    if (t->editing) SetFocus(t->hEdit);
}

static void layout(void) {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    int y = 4, toolbar_h = 30, tab_h = 24;
    MoveWindow(g_btnOpen,   4,    y, 52, 24, TRUE);
    MoveWindow(g_btnNew,    60,   y, 66, 24, TRUE);
    MoveWindow(g_btnClose,  130,  y, 60, 24, TRUE);
    MoveWindow(g_btnToggle, 194,  y, 78, 24, TRUE);
    MoveWindow(g_btnCopy,   276,  y, 66, 24, TRUE);
    MoveWindow(g_hZoomLabel, w - 88, y, 84, 24, TRUE);
    y += toolbar_h;
    MoveWindow(g_hTab, 4, y, w - 8, tab_h, TRUE);
    y += tab_h;
    int ch = h - y - 4;
    for (int i = 0; i < g_tab_count; i++) {
        MoveWindow(g_tabs[i]->hwnd, 4, y, w - 8, ch, TRUE);
        if (g_tabs[i]->controller) {
            RECT hr;
            GetClientRect(g_tabs[i]->hWebHost, &hr);
            g_tabs[i]->controller->lpVtbl->put_Bounds(g_tabs[i]->controller, hr);
        }
    }
}

static TabWin *new_tab(const WCHAR *path) {
    TabWin *t = g_malloc0(sizeof(TabWin));
    t->editing = TRUE;
    t->hwnd = CreateWindowExW(0, L"mdview_content", L"", WS_CHILD | WS_CLIPSIBLINGS,
                              0, 0, 0, 0, g_hwnd, NULL, g_hInst, NULL);
    t->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
                               WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_CLIPSIBLINGS,
                               0, 0, 0, 0, t->hwnd, NULL, g_hInst, NULL);
    editor_set_theme(t->hEdit, g_zoom);
    t->hWebHost = CreateWindowExW(0, L"mdview_host", L"", WS_CHILD | WS_CLIPSIBLINGS,
                                  0, 0, 0, 0, t->hwnd, NULL, g_hInst, NULL);

    if (path) {
        char *utf8 = read_file_utf8(path);
        if (utf8) {
            t->path = _wcsdup(path);
            t->source = utf8;
            WCHAR *w = utf8_to_wide(utf8);
            SetWindowTextW(t->hEdit, w ? w : L"");
            g_free(w);
        }
    }

    if (g_tab_count == g_tab_cap) {
        g_tab_cap = g_tab_cap ? g_tab_cap * 2 : 8;
        g_tabs = (TabWin **)g_realloc(g_tabs, (size_t)g_tab_cap * sizeof(TabWin *));
    }
    g_tabs[g_tab_count++] = t;

    TCITEMW ti;
    memset(&ti, 0, sizeof(ti));
    ti.mask = TCIF_TEXT;
    WCHAR title[MAX_PATH];
    if (t->path)
        wcscpy(title, PathFindFileNameW(t->path));
    else
        wcscpy(title, L"Untitled");
    ti.pszText = title;
    TabCtrl_InsertItem(g_hTab, g_tab_count - 1, &ti);

    TabCtrl_SetCurSel(g_hTab, g_tab_count - 1);
    switch_tab(g_tab_count - 1);
    create_controller(t);
    return t;
}

static void switch_tab(int idx) {
    if (idx < 0 || idx >= g_tab_count) return;
    if (g_current >= 0 && g_current < g_tab_count)
        ShowWindow(g_tabs[g_current]->hwnd, SW_HIDE);
    g_current = idx;
    ShowWindow(g_tabs[idx]->hwnd, SW_SHOW);
    apply_tab_mode(g_tabs[idx]);
    refresh_ui();
}

static void close_tab(int idx) {
    if (idx < 0 || idx >= g_tab_count) return;
    TabWin *t = g_tabs[idx];
    TabCtrl_DeleteItem(g_hTab, idx);
    if (t->controller) t->controller->lpVtbl->Release(t->controller);
    if (t->webview) t->webview->lpVtbl->Release(t->webview);
    DestroyWindow(t->hEdit);
    DestroyWindow(t->hWebHost);
    DestroyWindow(t->hwnd);
    g_free(t->path);
    g_free(t->source);
    g_free(t);
    for (int i = idx; i < g_tab_count - 1; i++) g_tabs[i] = g_tabs[i + 1];
    g_tab_count--;
    if (g_tab_count == 0) {
        g_current = -1;
        new_tab(NULL);
    } else {
        if (g_current >= g_tab_count) g_current = g_tab_count - 1;
        TabCtrl_SetCurSel(g_hTab, g_current);
        switch_tab(g_current);
    }
}

static TabWin *current_tab(void) {
    return (g_current >= 0 && g_current < g_tab_count) ? g_tabs[g_current] : NULL;
}

/* ------------------------------------------------------------------ */
/* File open / save                                                    */
/* ------------------------------------------------------------------ */
static void open_file(const WCHAR *path) {
    for (int i = 0; i < g_tab_count; i++)
        if (g_tabs[i]->path && _wcsicmp(g_tabs[i]->path, path) == 0) {
            switch_tab(i);
            return;
        }
    new_tab(path);
}

static int save_file_tab(TabWin *t) {
    if (!t->path) return 0;
    char *txt = editor_get_text(t);
    int ok = write_file_utf8(t->path, txt, -1);
    if (ok) {
        g_free(t->source);
        t->source = txt;
    } else {
        g_free(txt);
    }
    return ok;
}

static int save_file_as(TabWin *t) {
    WCHAR file[MAX_PATH] = L"untitled.md";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Markdown\0*.md\0Text\0*.txt\0All\0*.*\0";
    ofn.lpstrDefExt = L"md";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        g_free(t->path);
        t->path = _wcsdup(file);
        int ok = save_file_tab(t);
        if (ok) {
            TCITEMW ti;
            memset(&ti, 0, sizeof(ti));
            ti.mask = TCIF_TEXT;
            WCHAR *title = PathFindFileNameW(t->path);
            ti.pszText = title;
            TabCtrl_SetItem(g_hTab, g_current, &ti);
        }
        return ok;
    }
    return 0;
}

static void save_current(void) {
    TabWin *t = current_tab();
    if (!t) return;
    if (t->path)
        save_file_tab(t);
    else
        save_file_as(t);
}

static void copy_content(void) {
    TabWin *t = current_tab();
    if (!t) return;
    if (t->editing) {
        DWORD sel = (DWORD)SendMessageW(t->hEdit, EM_GETSEL, 0, 0);
        if (LOWORD(sel) != HIWORD(sel)) {
            SendMessageW(t->hEdit, WM_COPY, 0, 0);
            return;
        }
    }
    char *txt = editor_get_text(t);
    if (OpenClipboard(g_hwnd)) {
        EmptyClipboard();
        int n = MultiByteToWideChar(CP_UTF8, 0, txt, -1, NULL, 0);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n * sizeof(WCHAR));
        if (hg) {
            WCHAR *d = (WCHAR *)GlobalLock(hg);
            MultiByteToWideChar(CP_UTF8, 0, txt, -1, d, n);
            GlobalUnlock(hg);
            SetClipboardData(CF_UNICODETEXT, hg);
        }
        CloseClipboard();
    }
    g_free(txt);
}

static void toggle_preview(void) {
    TabWin *t = current_tab();
    if (!t) return;
    if (t->editing) {
        char *txt = editor_get_text(t);
        g_free(t->source);
        t->source = txt;
        t->editing = FALSE;
        show_preview(t);
    } else {
        t->editing = TRUE;
    }
    apply_tab_mode(t);
    refresh_ui();
}

static void refresh_ui(void) {
    TabWin *t = current_tab();
    SetWindowTextW(g_btnToggle, (t && t->editing) ? L"Preview" : L"Edit");
    WCHAR buf[32];
    wsprintfW(buf, L"%d%%", (int)(g_zoom * 100 + 0.5));
    SetWindowTextW(g_hZoomLabel, buf);
}

/* ------------------------------------------------------------------ */
/* Session cache                                                       */
/* ------------------------------------------------------------------ */
static void session_save(void) {
    WCHAR appdata[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata))) return;
    WCHAR dir[MAX_PATH];
    wsprintfW(dir, L"%s\\mdview", appdata);
    CreateDirectoryW(dir, NULL);
    WCHAR pat[MAX_PATH];
    wsprintfW(pat, L"%s\\untitled_*.md", dir);
    WIN32_FIND_DATAW fd;
    HANDLE ff = FindFirstFileW(pat, &fd);
    if (ff != INVALID_HANDLE_VALUE) {
        do {
            WCHAR fp[MAX_PATH];
            wsprintfW(fp, L"%s\\%s", dir, fd.cFileName);
            DeleteFileW(fp);
        } while (FindNextFileW(ff, &fd));
        FindClose(ff);
    }
    GString *out = g_string_new(NULL);
    for (int i = 0; i < g_tab_count; i++) {
        TabWin *t = g_tabs[i];
        char *txt = editor_get_text(t);
        if (t->path) {
            char *pu = wide_to_utf8(t->path);
            g_string_append_printf(out, "F\t%s\n", pu ? pu : "");
            g_free(pu);
        } else {
            gchar *fn = g_strdup_printf("untitled_%d.md", i);
            WCHAR *fnw = utf8_to_wide(fn);
            WCHAR wfn[MAX_PATH];
            wsprintfW(wfn, L"%s\\%s", dir, fnw ? fnw : L"");
            write_file_utf8(wfn, txt, -1);
            g_string_append_printf(out, "N\t%s\n", fn);
            g_free(fn);
            g_free(fnw);
        }
        g_free(txt);
    }
    WCHAR sess[MAX_PATH];
    wsprintfW(sess, L"%s\\session.txt", dir);
    char *u8 = g_string_free(out, FALSE);
    write_file_utf8(sess, u8, -1);
    g_free(u8);
}

static void session_restore(void) {
    WCHAR appdata[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata))) return;
    WCHAR sess[MAX_PATH];
    wsprintfW(sess, L"%s\\mdview\\session.txt", appdata);
    char *c = read_file_utf8(sess);
    if (!c) return;
    char **lines = g_strsplit(c, "\n", -1);
    WCHAR dir[MAX_PATH];
    wsprintfW(dir, L"%s\\mdview", appdata);
    for (int i = 0; lines[i]; i++) {
        const char *line = lines[i];
        if (line[0] == 'F' && line[1] == '\t') {
            WCHAR *p = utf8_to_wide(line + 2);
            if (p) { open_file(p); g_free(p); }
        } else if (line[0] == 'N' && line[1] == '\t') {
            WCHAR *cfnw = utf8_to_wide(line + 2);
            WCHAR cfn[MAX_PATH];
            wsprintfW(cfn, L"%s\\%s", dir, cfnw ? cfnw : L"");
            g_free(cfnw);
            char *content = read_file_utf8(cfn);
            TabWin *t = new_tab(NULL);
            if (t && content) {
                g_free(t->source);
                t->source = content;
                WCHAR *w = utf8_to_wide(content);
                SetWindowTextW(t->hEdit, w ? w : L"");
                g_free(w);
            }
        }
    }
    g_strfreev(lines);
    g_free(c);
}

/* ------------------------------------------------------------------ */
/* Window proc                                                         */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK ContentProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            g_btnOpen = CreateWindowW(L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      0, 0, 0, 0, h, (HMENU)ID_OPEN, g_hInst, NULL);
            g_btnNew = CreateWindowW(L"BUTTON", L"New", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     0, 0, 0, 0, h, (HMENU)ID_NEWTAB, g_hInst, NULL);
            g_btnClose = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       0, 0, 0, 0, h, (HMENU)ID_CLOSETAB, g_hInst, NULL);
            g_btnToggle = CreateWindowW(L"BUTTON", L"Preview", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                        0, 0, 0, 0, h, (HMENU)ID_TOGGLE, g_hInst, NULL);
            g_btnCopy = CreateWindowW(L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      0, 0, 0, 0, h, (HMENU)ID_COPY, g_hInst, NULL);
            g_hZoomLabel = CreateWindowW(L"STATIC", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                         0, 0, 0, 0, h, NULL, g_hInst, NULL);
            g_hTab = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   0, 0, 0, 0, h, NULL, g_hInst, NULL);
            return 0;
        }
        case WM_SIZE:
            layout();
            return 0;
        case WM_SETFOCUS:
            if (current_tab() && current_tab()->editing) SetFocus(current_tab()->hEdit);
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(w);
            switch (id) {
                case ID_OPEN: {
                    WCHAR file[MAX_PATH] = L"";
                    OPENFILENAMEW ofn;
                    memset(&ofn, 0, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = h;
                    ofn.lpstrFile = file;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = L"Markdown / Text\0*.md;*.markdown;*.txt;*.text\0All\0*.*\0";
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) open_file(file);
                    break;
                }
                case ID_NEWTAB: new_tab(NULL); break;
                case ID_CLOSETAB: close_tab(g_current); break;
                case ID_TOGGLE: toggle_preview(); break;
                case ID_COPY: copy_content(); break;
                case ID_SAVE: save_current(); break;
                case ID_ZOOMIN: zoom_by(0.1); break;
                case ID_ZOOMOUT: zoom_by(-0.1); break;
                case ID_ZOOMRESET: { g_zoom = 1.0; zoom_refresh(); break; }
            }
            return 0;
        }
        case WM_NOTIFY: {
            if (((LPNMHDR)l)->idFrom == 0 && ((LPNMHDR)l)->code == TCN_SELCHANGE)
                switch_tab(TabCtrl_GetCurSel(g_hTab));
            return 0;
        }
        case WM_DESTROY:
            session_save();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    g_hInst = hInstance;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    LoadLibraryW(L"Msftedit.dll");
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = L"mdview_main";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = ContentProc;
    wc.lpszClassName = L"mdview_content";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    wc.lpfnWndProc = HostProc;
    wc.lpszClassName = L"mdview_host";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, L"mdview_main", L"Markdown Viewer",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 960, 700,
                             NULL, NULL, hInstance, NULL);

    ACCEL acc[] = {
        { FCONTROL | FVIRTKEY, 'S', ID_SAVE },
        { FCONTROL | FVIRTKEY, 'O', ID_OPEN },
        { FCONTROL | FVIRTKEY, 'T', ID_NEWTAB },
        { FCONTROL | FVIRTKEY, 'W', ID_CLOSETAB },
        { FCONTROL | FVIRTKEY, 'E', ID_TOGGLE },
        { FCONTROL | FVIRTKEY, 'C', ID_COPY },
        { FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_ZOOMIN },
        { FCONTROL | FVIRTKEY, VK_ADD, ID_ZOOMIN },
        { FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_ZOOMOUT },
        { FCONTROL | FVIRTKEY, VK_SUBTRACT, ID_ZOOMOUT },
        { FCONTROL | FVIRTKEY, '0', ID_ZOOMRESET },
    };
    g_hAccel = CreateAcceleratorTableW(acc, sizeof(acc) / sizeof(acc[0]));

    session_restore();
    if (g_tab_count == 0) new_tab(NULL);

    int argc = 0;
    LPWSTR *args = CommandLineToArgvW(pCmdLine, &argc);
    for (int i = 0; i < argc; i++)
        if (args[i][0]) open_file(args[i]);
    if (args) LocalFree(args);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_MOUSEWHEEL && (LOWORD(msg.wParam) & MK_CONTROL)) {
            short delta = GET_WHEEL_DELTA_WPARAM(msg.wParam);
            zoom_by(delta > 0 ? 0.12 : -0.12);
            continue;
        }
        if (!TranslateAcceleratorW(g_hwnd, g_hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return (int)msg.wParam;
}
