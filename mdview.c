#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ===================== markdown -> HTML ===================== */

static void append_escaped(GString *out, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        switch (c) {
            case '&': g_string_append(out, "&amp;"); break;
            case '<': g_string_append(out, "&lt;"); break;
            case '>': g_string_append(out, "&gt;"); break;
            case '"': g_string_append(out, "&quot;"); break;
            default:  g_string_append_c(out, c);
        }
    }
}

static size_t find_char(const char *s, size_t start, size_t n, char c) {
    for (size_t i = start; i < n; i++)
        if (s[i] == c) return i;
    return n;
}

static size_t find_str(const char *s, size_t start, size_t n, const char *pat) {
    size_t pl = strlen(pat);
    if (n - start < pl) return n;
    for (size_t i = start; i + pl <= n; i++)
        if (strncmp(s + i, pat, pl) == 0) return i;
    return n;
}

static void render_inline(GString *out, const char *s) {
    size_t n = strlen(s);
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        if (c == '`') {
            size_t close = find_char(s, i + 1, n, '`');
            if (close < n) {
                g_string_append(out, "<code>");
                append_escaped(out, s + i + 1, close - i - 1);
                g_string_append(out, "</code>");
                i = close + 1;
            } else { append_escaped(out, s + i, 1); i++; }
        } else if (c == '!' && i + 1 < n && s[i + 1] == '[') {
            size_t close = find_char(s, i + 2, n, ']');
            if (close < n && close + 1 < n && s[close + 1] == '(') {
                size_t paren = find_char(s, close + 2, n, ')');
                if (paren < n) {
                    char *alt = g_strndup(s + i + 2, close - i - 2);
                    char *url = g_strndup(s + close + 2, paren - close - 2);
                    g_string_append(out, "<img src=\"");
                    append_escaped(out, url, strlen(url));
                    g_string_append(out, "\" alt=\"");
                    append_escaped(out, alt, strlen(alt));
                    g_string_append(out, "\">");
                    g_free(alt); g_free(url);
                    i = paren + 1;
                    continue;
                }
            }
            append_escaped(out, s + i, 1); i++;
        } else if (c == '[') {
            size_t close = find_char(s, i + 1, n, ']');
            if (close < n && close + 1 < n && s[close + 1] == '(') {
                size_t paren = find_char(s, close + 2, n, ')');
                if (paren < n) {
                    char *txt = g_strndup(s + i + 1, close - i - 1);
                    char *url = g_strndup(s + close + 2, paren - close - 2);
                    g_string_append(out, "<a href=\"");
                    append_escaped(out, url, strlen(url));
                    g_string_append(out, "\">");
                    render_inline(out, txt);
                    g_string_append(out, "</a>");
                    g_free(txt); g_free(url);
                    i = paren + 1;
                    continue;
                }
            }
            append_escaped(out, s + i, 1); i++;
        } else if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c) {
            char mk[3] = {c, c, 0};
            size_t close = find_str(s, i + 2, n, mk);
            if (close < n) {
                g_string_append(out, "<strong>");
                char *sub = g_strndup(s + i + 2, close - (i + 2));
                render_inline(out, sub);
                g_free(sub);
                g_string_append(out, "</strong>");
                i = close + 2;
            } else { append_escaped(out, s + i, 1); i++; }
        } else if (c == '*' || c == '_') {
            size_t close = find_char(s, i + 1, n, c);
            if (close < n) {
                g_string_append(out, "<em>");
                char *sub = g_strndup(s + i + 1, close - i - 1);
                render_inline(out, sub);
                g_free(sub);
                g_string_append(out, "</em>");
                i = close + 1;
            } else { append_escaped(out, s + i, 1); i++; }
        } else if (c == '~' && i + 1 < n && s[i + 1] == '~') {
            size_t close = find_str(s, i + 2, n, "~~");
            if (close < n) {
                g_string_append(out, "<del>");
                char *sub = g_strndup(s + i + 2, close - (i + 2));
                render_inline(out, sub);
                g_free(sub);
                g_string_append(out, "</del>");
                i = close + 2;
            } else { append_escaped(out, s + i, 1); i++; }
        } else {
            append_escaped(out, s + i, 1);
            i++;
        }
    }
}

static int is_blank(const char *l) {
    while (*l == ' ' || *l == '\t') l++;
    return *l == '\0';
}

static int atx_level(const char *l) {
    int cnt = 0;
    while (*l == '#') { cnt++; l++; }
    if (cnt == 0 || cnt > 6) return 0;
    if (*l == ' ' || *l == '\t' || *l == '\0') return cnt;
    return 0;
}

static int fence_char(const char *l) {
    if (l[0] == '`') return '`';
    if (l[0] == '~') return '~';
    return 0;
}

static int fence_len(const char *l, char c) {
    int cnt = 0;
    while (l[cnt] == c) cnt++;
    return cnt;
}

static int is_hr(const char *l) {
    const char *p = l;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '-' && *p != '*' && *p != '_') return 0;
    char c = *p;
    int cnt = 0;
    while (*p == c) { cnt++; p++; }
    while (*p == ' ' || *p == '\t') p++;
    return *p == '\0' && cnt >= 3;
}

static int is_quote_line(const char *l) {
    while (*l == ' ' || *l == '\t') l++;
    return *l == '>';
}

static int ul_marker(const char *l, const char **content) {
    int ind = 0;
    while (*l == ' ') { l++; ind++; }
    if (ind > 3) return 0;
    if ((*l == '-' || *l == '*' || *l == '+') && (l[1] == ' ' || l[1] == '\t')) {
        const char *c = l + 1;
        while (*c == ' ' || *c == '\t') c++;
        *content = c;
        return 1;
    }
    return 0;
}

static int ol_marker(const char *l, const char **content, int *start) {
    int ind = 0;
    while (*l == ' ') { l++; ind++; }
    if (ind > 3) return 0;
    const char *p = l;
    int num = 0;
    while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; }
    if (num == 0) return 0;
    if ((*p == '.' || *p == ')') && (p[1] == ' ' || p[1] == '\t')) {
        const char *c = p + 1;
        while (*c == ' ' || *c == '\t') c++;
        *content = c;
        *start = num;
        return 1;
    }
    return 0;
}

static int is_indented_code(const char *l) {
    if (l[0] == '\t') return 1;
    return l[0] == ' ' && l[1] == ' ' && l[2] == ' ' && l[3] == ' ';
}

static GPtrArray *split_row(const char *l) {
    GPtrArray *res = g_ptr_array_new_with_free_func(g_free);
    char *t = g_strdup(l);
    char *s = g_strstrip(t);
    size_t sl = strlen(s);
    if (sl > 0 && s[0] == '|') s++;
    sl = strlen(s);
    if (sl > 0 && s[sl - 1] == '|') s[sl - 1] = '\0';
    if (strlen(s) == 0) { g_free(t); return res; }
    char **parts = g_strsplit(s, "|", -1);
    for (int k = 0; parts[k]; k++) {
        char *cell = g_strstrip(g_strdup(parts[k]));
        if (strlen(cell) == 0) { g_free(cell); continue; }
        g_ptr_array_add(res, cell);
    }
    g_strfreev(parts);
    g_free(t);
    return res;
}

static int is_table_sep(const char *l) {
    GPtrArray *cells = split_row(l);
    int ok = cells->len > 0;
    for (guint k = 0; ok && k < cells->len; k++) {
        const char *cell = g_ptr_array_index(cells, k);
        const char *p = cell;
        if (*p == ':') p++;
        int dashes = 0;
        while (*p == '-') { dashes++; p++; }
        if (*p == ':') p++;
        if (dashes == 0 || *p != '\0') ok = 0;
    }
    g_ptr_array_unref(cells);
    return ok;
}

static GPtrArray *parse_sep(const char *l) {
    GPtrArray *res = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *cells = split_row(l);
    for (guint k = 0; k < cells->len; k++) {
        const char *c = g_ptr_array_index(cells, k);
        const char *p = c;
        int left = 0, right = 0;
        if (*p == ':') { left = 1; p++; }
        while (*p == '-') p++;
        if (*p == ':') right = 1;
        if (left && right)      g_ptr_array_add(res, g_strdup("center"));
        else if (right)         g_ptr_array_add(res, g_strdup("right"));
        else                    g_ptr_array_add(res, g_strdup("left"));
    }
    g_ptr_array_unref(cells);
    return res;
}

static GString *markdown_to_html(const char *md) {
    GString *out = g_string_new(NULL);
    char **lines = g_strsplit(md, "\n", -1);
    long n = 0;
    while (lines[n]) n++;
    long i = 0;

    while (i < n) {
        const char *line = lines[i];

        if (is_blank(line)) { i++; continue; }

        /* fenced code */
        int fc = fence_char(line);
        if (fc && fence_len(line, fc) >= 3) {
            const char *info = line + fence_len(line, fc);
            while (*info == ' ') info++;
            g_string_append(out, "<pre><code");
            if (*info) {
                const char *e = info;
                while (*e && *e != ' ') e++;
                g_string_append(out, " class=\"language-");
                append_escaped(out, info, (size_t)(e - info));
                g_string_append(out, "\"");
            }
            g_string_append(out, ">\n");
            i++;
            GString *code = g_string_new(NULL);
            while (i < n) {
                const char *cl = lines[i];
                if (fence_char(cl) == fc && fence_len(cl, fc) >= fence_len(line, fc)) { i++; break; }
                g_string_append(code, cl);
                g_string_append_c(code, '\n');
                i++;
            }
            append_escaped(out, code->str, code->len);
            g_string_free(code, TRUE);
            g_string_append(out, "</code></pre>\n");
            continue;
        }

        /* indented code */
        if (is_indented_code(line)) {
            g_string_append(out, "<pre><code>\n");
            while (i < n && (is_indented_code(lines[i]) || is_blank(lines[i]))) {
                const char *cl = lines[i];
                if (is_blank(cl)) {
                    g_string_append_c(out, '\n');
                } else {
                    const char *p = cl;
                    int rem = 0;
                    while (rem < 4 && *p == ' ') { p++; rem++; }
                    if (*p == '\t') p++;
                    g_string_append(out, p);
                    g_string_append_c(out, '\n');
                }
                i++;
            }
            g_string_append(out, "</code></pre>\n");
            continue;
        }

        /* ATX heading */
        int lvl = atx_level(line);
        if (lvl > 0) {
            const char *txt = line;
            while (*txt == '#') txt++;
            while (*txt == ' ' || *txt == '\t') txt++;
            size_t tl = strlen(txt);
            while (tl > 0 && txt[tl - 1] == '#') tl--;
            while (tl > 0 && (txt[tl - 1] == ' ' || txt[tl - 1] == '\t')) tl--;
            g_string_append_printf(out, "<h%d>", lvl);
            GString *t = g_string_new_len(txt, tl);
            render_inline(out, t->str);
            g_string_free(t, TRUE);
            g_string_append_printf(out, "</h%d>\n", lvl);
            i++;
            continue;
        }

        /* horizontal rule */
        if (is_hr(line)) { g_string_append(out, "<hr>\n"); i++; continue; }

        /* blockquote */
        if (is_quote_line(line)) {
            GString *content = g_string_new(NULL);
            while (i < n && is_quote_line(lines[i])) {
                const char *p = lines[i];
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '>') p++;
                while (*p == ' ') p++;
                g_string_append(content, p);
                g_string_append_c(content, '\n');
                i++;
            }
            g_string_append(out, "<blockquote>\n");
            GString *inner = markdown_to_html(content->str);
            g_string_append_len(out, inner->str, inner->len);
            g_string_free(inner, TRUE);
            g_string_free(content, TRUE);
            g_string_append(out, "</blockquote>\n");
            continue;
        }

        /* table */
        if (strchr(line, '|') && i + 1 < n && is_table_sep(lines[i + 1])) {
            GPtrArray *hdr = split_row(line);
            GPtrArray *align = parse_sep(lines[i + 1]);
            g_string_append(out, "<table>\n<thead>\n<tr>\n");
            for (guint k = 0; k < hdr->len; k++) {
                g_string_append(out, "<th");
                if (k < align->len) {
                    const char *a = g_ptr_array_index(align, k);
                    if (strcmp(a, "center") == 0) g_string_append(out, " style=\"text-align:center\"");
                    else if (strcmp(a, "right") == 0) g_string_append(out, " style=\"text-align:right\"");
                }
                g_string_append(out, ">");
                render_inline(out, g_ptr_array_index(hdr, k));
                g_string_append(out, "</th>\n");
            }
            g_string_append(out, "</tr>\n</thead>\n<tbody>\n");
            i += 2;
            while (i < n && !is_blank(lines[i]) && strchr(lines[i], '|')) {
                GPtrArray *row = split_row(lines[i]);
                g_string_append(out, "<tr>\n");
                for (guint k = 0; k < row->len; k++) {
                    g_string_append(out, "<td>");
                    render_inline(out, g_ptr_array_index(row, k));
                    g_string_append(out, "</td>\n");
                }
                g_string_append(out, "</tr>\n");
                g_ptr_array_unref(row);
                i++;
            }
            g_string_append(out, "</tbody>\n</table>\n");
            g_ptr_array_unref(hdr);
            g_ptr_array_unref(align);
            continue;
        }

        /* list */
        const char *item_content;
        int ol_num;
        int is_ol = ol_marker(line, &item_content, &ol_num);
        if (!is_ol && ul_marker(line, &item_content)) {
            GPtrArray *items = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(items, g_strstrip(g_strdup(item_content)));
            long j = i + 1;
            while (j < n) {
                const char *cl = lines[j];
                const char *c2;
                if (ul_marker(cl, &c2)) {
                    g_ptr_array_add(items, g_strstrip(g_strdup(c2)));
                    j++;
                } else if (is_blank(cl)) {
                    break;
                } else if (cl[0] == ' ' || cl[0] == '\t') {
                    const char *pc = cl;
                    while (*pc == ' ' || *pc == '\t') pc++;
                    char *last = g_ptr_array_index(items, items->len - 1);
                    char *trimmed = g_strstrip(g_strdup(pc));
                    char *joined = g_strdup_printf("%s %s", last, trimmed);
                    g_free(last); g_free(trimmed);
                    g_ptr_array_index(items, items->len - 1) = joined;
                    j++;
                } else break;
            }
            g_string_append(out, "<ul>\n");
            for (guint k = 0; k < items->len; k++) {
                g_string_append(out, "<li>");
                render_inline(out, g_ptr_array_index(items, k));
                g_string_append(out, "</li>\n");
            }
            g_string_append(out, "</ul>\n");
            g_ptr_array_unref(items);
            i = j;
            continue;
        }
        if (is_ol) {
            GPtrArray *items = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(items, g_strstrip(g_strdup(item_content)));
            int start = ol_num;
            long j = i + 1;
            while (j < n) {
                const char *cl = lines[j];
                const char *c2;
                int n2;
                if (ol_marker(cl, &c2, &n2)) {
                    g_ptr_array_add(items, g_strstrip(g_strdup(c2)));
                    j++;
                } else if (is_blank(cl)) {
                    break;
                } else if (cl[0] == ' ' || cl[0] == '\t') {
                    const char *pc = cl;
                    while (*pc == ' ' || *pc == '\t') pc++;
                    char *last = g_ptr_array_index(items, items->len - 1);
                    char *trimmed = g_strstrip(g_strdup(pc));
                    char *joined = g_strdup_printf("%s %s", last, trimmed);
                    g_free(last); g_free(trimmed);
                    g_ptr_array_index(items, items->len - 1) = joined;
                    j++;
                } else break;
            }
            if (start == 1) g_string_append(out, "<ol>\n");
            else g_string_append_printf(out, "<ol start=\"%d\">\n", start);
            for (guint k = 0; k < items->len; k++) {
                g_string_append(out, "<li>");
                render_inline(out, g_ptr_array_index(items, k));
                g_string_append(out, "</li>\n");
            }
            g_string_append(out, "</ol>\n");
            g_ptr_array_unref(items);
            i = j;
            continue;
        }

        /* paragraph */
        GString *para = g_string_new(NULL);
        while (i < n) {
            const char *cl = lines[i];
            if (is_blank(cl)) break;
            if (atx_level(cl) > 0) break;
            int tfc = fence_char(cl);
            if (tfc && fence_len(cl, tfc) >= 3) break;
            if (is_hr(cl)) break;
            if (is_quote_line(cl)) break;
            const char *tc;
            int tn;
            if (ul_marker(cl, &tc) || ol_marker(cl, &tc, &tn)) break;
            if (is_indented_code(cl)) break;
            if (strchr(cl, '|') && i + 1 < n && is_table_sep(lines[i + 1])) break;
            char *tmp = g_strstrip(g_strdup(cl));
            if (para->len > 0) g_string_append_c(para, ' ');
            g_string_append(para, tmp);
            g_free(tmp);
            i++;
        }
        g_string_append(out, "<p>");
        render_inline(out, para->str);
        g_string_append(out, "</p>\n");
        g_string_free(para, TRUE);
    }

    g_strfreev(lines);
    return out;
}

/* ===================== app ===================== */

typedef struct App App;
typedef struct Tab Tab;

struct Tab {
    App *app;
    WebKitWebView *webview;
    GtkWidget *stack;
    GtkWidget *editor_scroll;
    GtkTextView *editor;
    gchar *file_path;
    GString *source;
    gboolean editing;
    GFileMonitor *monitor;
};

struct App {
    GtkWidget *window;
    GtkWidget *edit_btn;
    GtkWidget *zoom_label;
    GtkNotebook *nb;
    gboolean dark;
    gdouble zoom;
    Tab *current;
    GtkCssProvider *chrome_css;
};

static void on_open(GtkWidget *btn, App *app);
static Tab *open_new_tab(App *app, const char *path);
static void on_file_changed(GFileMonitor *mon, GFile *file, GFile *other,
                            GFileMonitorEvent ev, Tab *t);

static const char *CSS_LIGHT =
    "body{font-family:ui-sans-serif,system-ui,-apple-system,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;"
    "max-width:840px;margin:0 auto;padding:40px 28px 80px;line-height:1.7;color:#24292f;background:#ffffff;"
    "word-wrap:break-word;-webkit-font-smoothing:antialiased;}"
    "h1,h2,h3,h4{line-height:1.3;margin-top:1.6em;margin-bottom:.6em;font-weight:600;letter-spacing:-.01em}"
    "h1{font-size:1.9em;padding-bottom:.35em;border-bottom:1px solid #eaeef2}"
    "h2{font-size:1.5em;padding-bottom:.3em;border-bottom:1px solid #eaeef2}"
    "h3{font-size:1.22em}h4{font-size:1.05em}"
    "p{margin:.6em 0 1.2em}"
    "a{color:#0969da;text-decoration:none}a:hover{text-decoration:underline}"
    "code{background:#f1f3f5;border-radius:5px;padding:.2em .45em;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,'Liberation Mono',monospace;font-size:.88em}"
    "pre{background:#f6f8fa;border:1px solid #eaeef2;border-radius:8px;padding:16px 18px;overflow-x:auto;line-height:1.55}"
    "pre code{background:transparent;padding:0;font-size:.86em}"
    "blockquote{border-left:4px solid #d0d7de;margin:1.2em 0;padding:.1em 1.1em;color:#57606a}"
    "ul,ol{padding-left:1.5em;margin:.6em 0 1.2em}li{margin:.3em 0}"
    "table{border-collapse:collapse;margin:1.2em 0;display:block;overflow-x:auto;font-size:.95em}"
    "th,td{border:1px solid #d8dee4;padding:8px 14px}th{background:#f6f8fa;font-weight:600}"
    "hr{border:0;border-top:1px solid #d8dee4;margin:2em 0}img{max-width:100%;border-radius:6px}del{color:#6e7781}"
    "::selection{background:#b3d7ff}"
    "::-webkit-scrollbar{width:10px;height:10px}"
    "::-webkit-scrollbar-thumb{background:#c1c7cd;border-radius:5px;border:2px solid #ffffff}"
    "::-webkit-scrollbar-thumb:hover{background:#a8aeb5}";

static const char *CSS_DARK =
    "body{font-family:ui-sans-serif,system-ui,-apple-system,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;"
    "max-width:840px;margin:0 auto;padding:40px 28px 80px;line-height:1.7;color:#e6edf3;background:#0d1117;"
    "word-wrap:break-word;-webkit-font-smoothing:antialiased;}"
    "h1,h2,h3,h4{line-height:1.3;margin-top:1.6em;margin-bottom:.6em;font-weight:600;letter-spacing:-.01em}"
    "h1{font-size:1.9em;padding-bottom:.35em;border-bottom:1px solid #21262d}"
    "h2{font-size:1.5em;padding-bottom:.3em;border-bottom:1px solid #21262d}"
    "h3{font-size:1.22em}h4{font-size:1.05em}"
    "p{margin:.6em 0 1.2em}"
    "a{color:#58a6ff;text-decoration:none}a:hover{text-decoration:underline}"
    "code{background:#21262d;border-radius:5px;padding:.2em .45em;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,'Liberation Mono',monospace;font-size:.88em}"
    "pre{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px 18px;overflow-x:auto;line-height:1.55}"
    "pre code{background:transparent;padding:0;font-size:.86em}"
    "blockquote{border-left:4px solid #3d444d;margin:1.2em 0;padding:.1em 1.1em;color:#9198a1}"
    "ul,ol{padding-left:1.5em;margin:.6em 0 1.2em}li{margin:.3em 0}"
    "table{border-collapse:collapse;margin:1.2em 0;display:block;overflow-x:auto;font-size:.95em}"
    "th,td{border:1px solid #30363d;padding:8px 14px}th{background:#161b22;font-weight:600}"
    "hr{border:0;border-top:1px solid #30363d;margin:2em 0}img{max-width:100%;border-radius:6px}del{color:#8b949e}"
    "::selection{background:#1f3a52}"
    "::-webkit-scrollbar{width:10px;height:10px}"
    "::-webkit-scrollbar-thumb{background:#3d444d;border-radius:5px;border:2px solid #0d1117}"
    "::-webkit-scrollbar-thumb:hover{background:#4a5158}";

static gchar *build_html(App *app) {
    GString *html = g_string_new(NULL);
    g_string_append(html, "<!DOCTYPE html><html><head><meta charset='utf-8'><style>");
    g_string_append(html, app->dark ? CSS_DARK : CSS_LIGHT);
    g_string_append(html, "</style></head><body>");
    Tab *t = app->current;
    if (t && t->source && t->source->len) {
        GString *body = markdown_to_html(t->source->str);
        g_string_append_len(html, body->str, body->len);
        g_string_free(body, TRUE);
    } else {
        g_string_append(html, "<p style='color:#888'>Open a file with <b>Ctrl+O</b> or pass it as an argument: "
                              "<code>mdview file.md</code></p>");
    }
    g_string_append(html, "</body></html>");
    return g_string_free(html, FALSE);
}

static void render_page(App *app) {
    Tab *t = app->current;
    if (!t) return;
    gchar *html = build_html(app);
    gchar *dir = g_path_get_dirname(t->file_path ? t->file_path : ".");
    gchar *base = g_filename_to_uri(dir, NULL, NULL);
    webkit_web_view_load_html(t->webview, html, base);
    webkit_web_view_set_zoom_level(t->webview, app->zoom);
    g_free(dir); g_free(base); g_free(html);
}

static void set_title(App *app) {
    Tab *t = app->current;
    gchar *base = (t && t->file_path) ? g_path_get_basename(t->file_path) : NULL;
    gchar *title = g_strdup_printf("mdview - %s", base ? base : "no file");
    gtk_header_bar_set_title(GTK_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(app->window))), title);
    if (t && t->file_path)
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(app->window))), t->file_path);
    else
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(app->window))), "");
    g_free(base);
    g_free(title);
}

static gchar *editor_get_text(Tab *t) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(t->editor);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    return gtk_text_buffer_get_text(buf, &start, &end, FALSE);
}

static gboolean save_tab(App *app, Tab *t) {
    if (!t->file_path) return FALSE;
    gchar *text = editor_get_text(t);
    GError *err = NULL;
    gboolean ok = g_file_set_contents(t->file_path, text, -1, &err);
    if (ok) {
        if (t->source) g_string_assign(t->source, text);
    } else {
        g_printerr("Error saving %s: %s\n", t->file_path, err->message);
        g_error_free(err);
    }
    g_free(text);
    return ok;
}

static void on_new_tab(GtkWidget *btn, App *app) {
    open_new_tab(app, NULL);
}

static gboolean save_tab_as(App *app, Tab *t) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Save As", GTK_WINDOW(app->window),
                                                 GTK_FILE_CHOOSER_ACTION_SAVE,
                                                 "_Cancel", GTK_RESPONSE_CANCEL,
                                                 "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "untitled.md");
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        g_free(t->file_path);
        t->file_path = g_strdup(path);
        if (t->monitor) g_object_unref(t->monitor);
        GFile *gf = g_file_new_for_path(path);
        t->monitor = g_file_monitor_file(gf, G_FILE_MONITOR_NONE, NULL, NULL);
        if (t->monitor) g_signal_connect(t->monitor, "changed", G_CALLBACK(on_file_changed), t);
        g_object_unref(gf);
        GtkWidget *label = g_object_get_data(G_OBJECT(t->stack), "label");
        if (label) gtk_label_set_text(GTK_LABEL(label), g_path_get_basename(path));
        gboolean ok = save_tab(app, t);
        g_free(path);
        gtk_widget_destroy(dlg);
        return ok;
    }
    gtk_widget_destroy(dlg);
    return FALSE;
}

static gboolean reload_tab_content(Tab *t) {
    if (!t->file_path) return FALSE;
    gchar *contents = NULL;
    GError *err = NULL;
    if (!g_file_get_contents(t->file_path, &contents, NULL, &err)) {
        g_printerr("Error reloading %s: %s\n", t->file_path, err->message);
        g_error_free(err);
        return FALSE;
    }
    if (t->source) g_string_free(t->source, TRUE);
    t->source = g_string_new(contents);
    g_free(contents);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(t->editor), t->source->str, -1);
    return TRUE;
}

static void apply_gtk_theme(App *app) {
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", app->dark, NULL);
    if (app->chrome_css) {
        gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),
                                                     GTK_STYLE_PROVIDER(app->chrome_css));
        g_object_unref(app->chrome_css);
        app->chrome_css = NULL;
    }
    GtkCssProvider *p = gtk_css_provider_new();
    const char *css = app->dark
        ? "headerbar,headerbar label,headerbar button{color:#ffffff}"
          "headerbar button image,headerbar button image:hover{color:#ffffff}"
          "headerbar button:disabled image{color:rgba(255,255,255,0.4)}"
        : "headerbar,headerbar label,headerbar button{color:#24292f}"
          "headerbar button image,headerbar button image:hover{color:#24292f}";
    gtk_css_provider_load_from_data(p, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(p),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    app->chrome_css = p;
}

static void apply_editor_theme(App *app, Tab *t) {
    if (!t->editor) return;
    gint fsize = (gint)(13 * app->zoom + 0.5);
    if (fsize < 8) fsize = 8;
    GtkCssProvider *p = gtk_css_provider_new();
    gchar *css = app->dark
        ? g_strdup_printf("textview,textview text{background-color:#0d1117;color:#e6edf3;caret-color:#e6edf3;"
                          "font-family:'DejaVu Sans Mono',monospace;font-size:%dpx}", fsize)
        : g_strdup_printf("textview,textview text{background-color:#ffffff;color:#24292f;caret-color:#24292f;"
                          "font-family:'DejaVu Sans Mono',monospace;font-size:%dpx}", fsize);
    gtk_css_provider_load_from_data(p, css, -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(GTK_WIDGET(t->editor)),
                                   GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
    g_free(css);
}

typedef struct { GtkWidget *btn; gchar *label; } FlashData;

static gboolean flash_revert(gpointer data) {
    FlashData *fd = data;
    gtk_button_set_label(GTK_BUTTON(fd->btn), fd->label);
    g_free(fd->label);
    g_free(fd);
    return G_SOURCE_REMOVE;
}

static void flash_label(GtkWidget *btn, const char *msg, const char *revert_to) {
    gtk_button_set_label(GTK_BUTTON(btn), msg);
    FlashData *fd = g_malloc(sizeof(*fd));
    fd->btn = btn;
    fd->label = g_strdup(revert_to);
    g_timeout_add(1200, flash_revert, fd);
}

static void update_edit_button(App *app) {
    if (!app->edit_btn) return;
    gboolean editing = app->current && app->current->editing;
    gtk_button_set_label(GTK_BUTTON(app->edit_btn), editing ? "Preview" : "Edit");
    gtk_widget_set_tooltip_text(app->edit_btn, editing ? "Show rendered preview" : "Edit source");
}

static void on_switch_page(GtkNotebook *nb, GtkWidget *child, guint page, App *app) {
    app->current = g_object_get_data(G_OBJECT(child), "tab");
    set_title(app);
    update_edit_button(app);
}

static void on_close_tab(GtkWidget *btn, App *app) {
    Tab *t = btn ? g_object_get_data(G_OBJECT(btn), "tab") : app->current;
    if (!t) return;
    if (t->monitor) g_object_unref(t->monitor);
    gtk_notebook_remove_page(app->nb, gtk_notebook_page_num(app->nb, t->stack));
    g_free(t->file_path);
    if (t->source) g_string_free(t->source, TRUE);
    g_free(t);
    if (gtk_notebook_get_n_pages(app->nb) == 0) {
        app->current = NULL;
        open_new_tab(app, NULL);
    }
}

static void on_toggle_edit(GtkWidget *btn, App *app) {
    Tab *t = app->current;
    if (!t) return;
    if (!t->editing) {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(t->editor), t->source ? t->source->str : "", -1);
        t->editing = TRUE;
        gtk_stack_set_visible_child(GTK_STACK(t->stack), t->editor_scroll);
        gtk_widget_grab_focus(GTK_WIDGET(t->editor));
        update_edit_button(app);
    } else {
        gchar *text = editor_get_text(t);
        if (t->source) g_string_assign(t->source, text);
        g_free(text);
        t->editing = FALSE;
        gtk_stack_set_visible_child(GTK_STACK(t->stack), GTK_WIDGET(t->webview));
        render_page(app);
        update_edit_button(app);
    }
}

static gboolean revert_saved(gpointer data) {
    GtkWidget *btn = data;
    App *app = g_object_get_data(G_OBJECT(btn), "app");
    if (app) update_edit_button(app);
    g_object_unref(btn);
    return G_SOURCE_REMOVE;
}

static void on_save_only(GtkWidget *btn, App *app) {
    Tab *t = app->current;
    if (!t) return;
    gboolean ok = t->file_path ? save_tab(app, t) : save_tab_as(app, t);
    if (ok) {
        gtk_button_set_label(GTK_BUTTON(app->edit_btn), "Saved!");
        g_timeout_add(1000, revert_saved, g_object_ref(app->edit_btn));
    }
}

static void zoom_change(App *app, gdouble delta) {
    app->zoom += delta;
    if (app->zoom < 0.5) app->zoom = 0.5;
    if (app->zoom > 4.0) app->zoom = 4.0;
    int n = gtk_notebook_get_n_pages(app->nb);
    for (int i = 0; i < n; i++) {
        GtkWidget *st = gtk_notebook_get_nth_page(app->nb, i);
        Tab *t = g_object_get_data(G_OBJECT(st), "tab");
        if (t) apply_editor_theme(app, t);
    }
    if (app->current)
        webkit_web_view_set_zoom_level(app->current->webview, app->zoom);
    if (app->zoom_label) {
        gchar *s = g_strdup_printf("%d%%", (int)(app->zoom * 100 + 0.5));
        gtk_label_set_text(GTK_LABEL(app->zoom_label), s);
        g_free(s);
    }
}

static gboolean on_scroll_zoom(GtkWidget *w, GdkEventScroll *ev, App *app) {
    if (!(ev->state & GDK_CONTROL_MASK)) return FALSE;
    gdouble step = ev->direction == GDK_SCROLL_SMOOTH ? fabs(ev->delta_y) * 0.08 : 0.12;
    if (step < 0.04) step = 0.12;
    if ((ev->direction == GDK_SCROLL_UP) ||
        (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y < 0))
        zoom_change(app, step);
    else if ((ev->direction == GDK_SCROLL_DOWN) ||
             (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y > 0))
        zoom_change(app, -step);
    return TRUE;
}

static gboolean on_key_press(GtkWidget *w, GdkEventKey *ev, App *app) {
    if (!(ev->state & GDK_CONTROL_MASK)) return FALSE;
    switch (ev->keyval) {
        case GDK_KEY_s: on_save_only(NULL, app); return TRUE;
        case GDK_KEY_o: on_open(NULL, app); return TRUE;
        case GDK_KEY_t: open_new_tab(app, NULL); return TRUE;
        case GDK_KEY_w: on_close_tab(NULL, app); return TRUE;
        case GDK_KEY_plus:
        case GDK_KEY_equal: zoom_change(app, 0.1); return TRUE;
        case GDK_KEY_minus: zoom_change(app, -0.1); return TRUE;
        case GDK_KEY_0: zoom_change(app, 1.0 - app->zoom); return TRUE;
        default: return FALSE;
    }
}

static void on_open(GtkWidget *btn, App *app) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Open file", GTK_WINDOW(app->window),
                                                 GTK_FILE_CHOOSER_ACTION_OPEN,
                                                 "_Cancel", GTK_RESPONSE_CANCEL,
                                                 "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "Markdown / Text");
    gtk_file_filter_add_pattern(f, "*.md");
    gtk_file_filter_add_pattern(f, "*.markdown");
    gtk_file_filter_add_pattern(f, "*.txt");
    gtk_file_filter_add_pattern(f, "*.text");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);
    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), all);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        open_new_tab(app, path);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

static gboolean on_decide_policy(WebKitWebView *wv, WebKitPolicyDecision *decision,
                                 WebKitPolicyDecisionType type, App *app) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        webkit_policy_decision_use(decision);
        return FALSE;
    }
    WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(
        WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    if (webkit_navigation_action_get_navigation_type(action) == WEBKIT_NAVIGATION_TYPE_LINK_CLICKED) {
        const gchar *uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
        if (g_str_has_prefix(uri, "file://")) {
            gchar *path = g_filename_from_uri(uri, NULL, NULL);
            if (path) {
                gchar *lower = g_ascii_strdown(path, -1);
                if (g_str_has_suffix(lower, ".md") || g_str_has_suffix(lower, ".markdown") ||
                    g_str_has_suffix(lower, ".txt") || g_str_has_suffix(lower, ".text")) {
                    open_new_tab(app, path);
                    g_free(lower); g_free(path);
                    webkit_policy_decision_ignore(decision);
                    return TRUE;
                }
                g_free(lower); g_free(path);
            }
        } else if (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://") ||
                   g_str_has_prefix(uri, "mailto:")) {
            GError *e = NULL;
            g_app_info_launch_default_for_uri(uri, NULL, &e);
            if (e) { g_printerr("Error opening link: %s\n", e->message); g_error_free(e); }
            webkit_policy_decision_ignore(decision);
            return TRUE;
        }
    }
    webkit_policy_decision_use(decision);
    return FALSE;
}

static void on_copy_md(GtkWidget *btn, App *app) {
    Tab *t = app->current;
    gchar *text = NULL;
    if (t && t->editing) text = editor_get_text(t);
    else if (t && t->source) text = g_strdup(t->source->str);
    if (!text) return;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    g_free(text);
    flash_label(btn, "Copied!", "Copy");
}

static void on_toggle_theme(GtkWidget *btn, App *app) {
    app->dark = !app->dark;
    apply_gtk_theme(app);
    int n = gtk_notebook_get_n_pages(app->nb);
    for (int i = 0; i < n; i++) {
        GtkWidget *st = gtk_notebook_get_nth_page(app->nb, i);
        Tab *t = g_object_get_data(G_OBJECT(st), "tab");
        if (t) apply_editor_theme(app, t);
    }
    render_page(app);
}

static void on_reload(GtkWidget *btn, App *app) {
    Tab *t = app->current;
    if (t && t->file_path && reload_tab_content(t)) render_page(app);
    else render_page(app);
}

static void on_file_changed(GFileMonitor *mon, GFile *file, GFile *other,
                            GFileMonitorEvent ev, Tab *t) {
    if (t->editing || !t->file_path) return;
    if (g_file_test(t->file_path, G_FILE_TEST_EXISTS) && reload_tab_content(t)) {
        if (t->app->current == t) render_page(t->app);
    }
}

static void save_session(App *app) {
    const gchar *dir = g_get_user_cache_dir();
    gchar *mdir = g_build_filename(dir, "mdview", NULL);
    g_mkdir_with_parents(mdir, 0700);
    GDir *d = g_dir_open(mdir, 0, NULL);
    if (d) {
        const char *name;
        while ((name = g_dir_read_name(d)))
            if (g_str_has_prefix(name, "untitled_")) {
                gchar *fp = g_build_filename(mdir, name, NULL);
                g_remove(fp);
                g_free(fp);
            }
        g_dir_close(d);
    }
    gchar *sess = g_build_filename(mdir, "session", NULL);
    GString *out = g_string_new(NULL);
    int n = gtk_notebook_get_n_pages(app->nb);
    int uid = 0;
    for (int i = 0; i < n; i++) {
        GtkWidget *st = gtk_notebook_get_nth_page(app->nb, i);
        Tab *t = g_object_get_data(G_OBJECT(st), "tab");
        if (!t) continue;
        gchar *content = NULL;
        if (t->editing) content = editor_get_text(t);
        else if (t->source) content = g_strdup(t->source->str);
        if (!content) content = g_strdup("");
        gchar *fname = g_strdup_printf("untitled_%d.md", uid++);
        gchar *cpath = g_build_filename(mdir, fname, NULL);
        g_file_set_contents(cpath, content, -1, NULL);
        if (t->file_path)
            g_string_append_printf(out, "F\t%s\t%s\n", t->file_path, fname);
        else
            g_string_append_printf(out, "N\t%s\n", fname);
        g_free(fname); g_free(cpath); g_free(content);
    }
    g_file_set_contents(sess, out->str, out->len, NULL);
    g_string_free(out, TRUE);
    g_free(sess);
    g_free(mdir);
}

static void restore_session(App *app) {
    const gchar *dir = g_get_user_cache_dir();
    gchar *mdir = g_build_filename(dir, "mdview", NULL);
    gchar *sess = g_build_filename(mdir, "session", NULL);
    gchar *contents = NULL;
    if (g_file_get_contents(sess, &contents, NULL, NULL) && contents) {
        char **lines = g_strsplit(contents, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            const char *line = lines[i];
            if (line[0] == 'F' && line[1] == '\t') {
                char **parts = g_strsplit(line + 2, "\t", -1);
                Tab *t = parts[0] ? open_new_tab(app, parts[0]) : NULL;
                if (t && parts[1]) {
                    gchar *cpath = g_build_filename(mdir, parts[1], NULL);
                    gchar *c = NULL;
                    if (g_file_get_contents(cpath, &c, NULL, NULL)) {
                        if (t->source) g_string_free(t->source, TRUE);
                        t->source = g_string_new(c);
                        gtk_text_buffer_set_text(gtk_text_view_get_buffer(t->editor), t->source->str, -1);
                        render_page(app);
                        g_free(c);
                    }
                    g_free(cpath);
                }
                g_strfreev(parts);
            } else if (line[0] == 'N' && line[1] == '\t') {
                gchar *cpath = g_build_filename(mdir, line + 2, NULL);
                gchar *c = NULL;
                if (g_file_get_contents(cpath, &c, NULL, NULL)) {
                    Tab *t = open_new_tab(app, NULL);
                    if (t && c) {
                        if (t->source) g_string_free(t->source, TRUE);
                        t->source = g_string_new(c);
                        gtk_text_buffer_set_text(gtk_text_view_get_buffer(t->editor), t->source->str, -1);
                        render_page(app);
                    }
                    g_free(c);
                }
                g_free(cpath);
            }
        }
        g_strfreev(lines);
    }
    g_free(contents);
    g_free(sess);
    g_free(mdir);
}

static void on_destroy(GtkWidget *w, App *app) {
    save_session(app);
    gtk_main_quit();
}

static gboolean focus_editor_later(gpointer data) {
    GtkWidget *ed = data;
    if (GTK_IS_WIDGET(ed) && gtk_widget_get_realized(ed))
        gtk_widget_grab_focus(ed);
    g_object_unref(ed);
    return G_SOURCE_REMOVE;
}

static Tab *open_new_tab(App *app, const char *path) {
    if (path) {
        int n = gtk_notebook_get_n_pages(app->nb);
        for (int i = 0; i < n; i++) {
            GtkWidget *st = gtk_notebook_get_nth_page(app->nb, i);
            Tab *ot = g_object_get_data(G_OBJECT(st), "tab");
            if (ot && ot->file_path && g_strcmp0(ot->file_path, path) == 0) {
                gtk_notebook_set_current_page(app->nb, i);
                app->current = ot;
                set_title(app);
                update_edit_button(app);
                return ot;
            }
        }
    }

    Tab *t = g_malloc0(sizeof(Tab));
    t->app = app;

    t->webview = WEBKIT_WEB_VIEW(webkit_web_view_new());
    g_signal_connect(t->webview, "decide-policy", G_CALLBACK(on_decide_policy), app);
    g_signal_connect(t->webview, "scroll-event", G_CALLBACK(on_scroll_zoom), app);

    t->editor_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(t->editor_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_signal_connect(t->editor_scroll, "scroll-event", G_CALLBACK(on_scroll_zoom), app);
    t->editor = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_wrap_mode(t->editor, GTK_WRAP_WORD);
    gtk_text_view_set_accepts_tab(t->editor, TRUE);
    gtk_container_add(GTK_CONTAINER(t->editor_scroll), GTK_WIDGET(t->editor));
    apply_editor_theme(app, t);

    t->stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(t->stack), GTK_WIDGET(t->webview), "view");
    gtk_stack_add_named(GTK_STACK(t->stack), t->editor_scroll, "edit");
    gtk_stack_set_visible_child_name(GTK_STACK(t->stack), "view");
    g_object_set_data(G_OBJECT(t->stack), "tab", t);

    const char *name = path ? g_path_get_basename(path) : "Untitled";
    GtkWidget *label = gtk_label_new(name);
    g_object_set_data(G_OBJECT(t->stack), "label", label);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(close, "Close tab (Ctrl+W)");
    g_object_set_data(G_OBJECT(close), "tab", t);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_tab), app);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);
    gtk_widget_show_all(box);

    gtk_notebook_append_page(app->nb, t->stack, box);
    gtk_widget_show_all(t->stack);
    gtk_notebook_set_current_page(app->nb, gtk_notebook_get_n_pages(app->nb) - 1);

    if (path) {
        gchar *contents = NULL;
        GError *err = NULL;
        if (g_file_get_contents(path, &contents, NULL, &err)) {
            t->file_path = g_strdup(path);
            t->source = g_string_new(contents);
            g_free(contents);
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(t->editor), t->source->str, -1);
            GFile *gf = g_file_new_for_path(path);
            t->monitor = g_file_monitor_file(gf, G_FILE_MONITOR_NONE, NULL, NULL);
            if (t->monitor) g_signal_connect(t->monitor, "changed", G_CALLBACK(on_file_changed), t);
            g_object_unref(gf);
        } else {
            g_printerr("Error opening %s: %s\n", path, err->message);
            g_error_free(err);
        }
    }

    app->current = t;
    set_title(app);
    render_page(app);
    t->editing = TRUE;
    gtk_stack_set_visible_child(GTK_STACK(t->stack), t->editor_scroll);
    g_timeout_add(100, focus_editor_later, g_object_ref(GTK_WIDGET(t->editor)));
    update_edit_button(app);
    return t;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    App app = {0};
    app.dark = TRUE;
    app.zoom = 1.0;
    apply_gtk_theme(&app);

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(app.window), 960, 700);
    gtk_window_set_title(GTK_WINDOW(app.window), "Markdown Viewer");

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(app.window), header);

    GtkWidget *open_btn = gtk_button_new_from_icon_name("document-open-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(open_btn, "Open file (Ctrl+O)");
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open), &app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), open_btn);

    GtkWidget *newtab_btn = gtk_button_new_from_icon_name("tab-new-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(newtab_btn, "New tab (Ctrl+T)");
    g_signal_connect(newtab_btn, "clicked", G_CALLBACK(on_new_tab), &app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), newtab_btn);

    GtkWidget *reload_btn = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(reload_btn, "Reload");
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload), &app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), reload_btn);

    GtkWidget *theme_btn = gtk_button_new_from_icon_name("weather-clear-night-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(theme_btn, "Toggle light/dark theme");
    g_signal_connect(theme_btn, "clicked", G_CALLBACK(on_toggle_theme), &app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), theme_btn);

    app.edit_btn = gtk_button_new_with_label("Preview");
    gtk_widget_set_tooltip_text(app.edit_btn, "Show rendered preview (Ctrl+E)");
    g_object_set_data(G_OBJECT(app.edit_btn), "app", &app);
    g_signal_connect(app.edit_btn, "clicked", G_CALLBACK(on_toggle_edit), &app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app.edit_btn);

    GtkWidget *copy_btn = gtk_button_new_with_label("Copy");
    gtk_widget_set_tooltip_text(copy_btn, "Copy content (Ctrl+C)");
    gtk_button_set_image(GTK_BUTTON(copy_btn), gtk_image_new_from_icon_name("edit-copy-symbolic", GTK_ICON_SIZE_BUTTON));
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_md), &app);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), copy_btn);

    app.zoom_label = gtk_label_new("100%");
    gtk_widget_set_tooltip_text(app.zoom_label, "Zoom (Ctrl+scroll)");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app.zoom_label);

    app.nb = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_set_scrollable(app.nb, TRUE);
    g_signal_connect(app.nb, "switch-page", G_CALLBACK(on_switch_page), &app);
    gtk_container_add(GTK_CONTAINER(app.window), GTK_WIDGET(app.nb));

    g_signal_connect(app.window, "destroy", G_CALLBACK(on_destroy), &app);

    /* Ctrl+C -> copiar markdown, Ctrl+E -> editar */
    GtkAccelGroup *accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(app.window), accel);
    gtk_widget_add_accelerator(copy_btn, "clicked", accel, GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(app.edit_btn, "clicked", accel, GDK_KEY_e, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), &app);

    restore_session(&app);
    if (argc > 1) {
        for (int a = 1; a < argc; a++)
            if (argv[a][0] != '\0') open_new_tab(&app, argv[a]);
    } else if (gtk_notebook_get_n_pages(app.nb) == 0) {
        open_new_tab(&app, NULL);
    }

    gtk_widget_show_all(app.window);
    gtk_main();
    return 0;
}

