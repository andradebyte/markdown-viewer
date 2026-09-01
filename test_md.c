#include <stdio.h>
#define main mdview_main
#include "mdview.c"
#undef main

int main(void) {
    const char *md =
        "# Titulo\n\n"
        "Paragrafo com **negrito**, *italico*, `codigo`, ~~riscado~~ e [link](https://exemplo.com).\n\n"
        "![img](imagem.png)\n\n"
        "## Lista\n\n"
        "- item 1\n"
        "- item 2 com *enfase*\n\n"
        "1. primeiro\n"
        "2. segundo\n\n"
        "> citacao\n"
        "> continua\n\n"
        "```c\n"
        "int main() { return 0; }\n"
        "```\n\n"
        "    indented code\n\n"
        "| Col A | Col B |\n"
        "|:------|------:|\n"
        "| x     | y     |\n\n"
        "---\n";
    GString *html = markdown_to_html(md);
    printf("%s\n", html->str);
    g_string_free(html, TRUE);
    return 0;
}
