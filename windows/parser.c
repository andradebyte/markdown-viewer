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
