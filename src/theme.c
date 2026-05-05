#define _GNU_SOURCE
#include "theme.h"
#include <adwaita.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *fg;
    const char *bg;
} ThemeDef;

static const ThemeDef custom_themes[] = {
    {"solarized-light",   "#657b83", "#fdf6e3"},
    {"solarized-dark",    "#839496", "#002b36"},
    {"monokai",           "#f8f8f2", "#272822"},
    {"gruvbox-light",     "#3c3836", "#fbf1c7"},
    {"gruvbox-dark",      "#ebdbb2", "#282828"},
    {"nord",              "#d8dee9", "#2e3440"},
    {"dracula",           "#f8f8f2", "#282a36"},
    {"tokyo-night",       "#a9b1d6", "#1a1b26"},
    {"catppuccin-latte",  "#4c4f69", "#eff1f5"},
    {"catppuccin-mocha",  "#cdd6f4", "#1e1e2e"},
    {NULL, NULL, NULL}
};

static const ThemeDef *theme_lookup(const char *name) {
    for (int i = 0; custom_themes[i].name; i++)
        if (strcmp(name, custom_themes[i].name) == 0)
            return &custom_themes[i];
    return NULL;
}

const char *notes_theme_fg(const char *theme) {
    const ThemeDef *t = theme_lookup(theme);
    return t ? t->fg : NULL;
}

const char *notes_theme_bg(const char *theme) {
    const ThemeDef *t = theme_lookup(theme);
    return t ? t->bg : NULL;
}

gboolean notes_is_dark_theme(const char *theme) {
    return strcmp(theme, "dark") == 0 ||
           strcmp(theme, "solarized-dark") == 0 ||
           strcmp(theme, "monokai") == 0 ||
           strcmp(theme, "gruvbox-dark") == 0 ||
           strcmp(theme, "nord") == 0 ||
           strcmp(theme, "dracula") == 0 ||
           strcmp(theme, "tokyo-night") == 0 ||
           strcmp(theme, "catppuccin-mocha") == 0;
}

static const char *scheme_for_theme(const char *theme) {
    if (strcmp(theme, "solarized-light") == 0) return "solarized-light";
    if (strcmp(theme, "solarized-dark") == 0)  return "solarized-dark";
    if (strcmp(theme, "monokai") == 0)         return "oblivion";
    if (strcmp(theme, "gruvbox-dark") == 0)    return "classic-dark";
    if (strcmp(theme, "gruvbox-light") == 0)   return "kate";
    if (strcmp(theme, "nord") == 0)            return "cobalt";
    if (strcmp(theme, "dracula") == 0)         return "oblivion";
    if (strcmp(theme, "tokyo-night") == 0)     return "Adwaita-dark";
    if (strcmp(theme, "catppuccin-mocha") == 0) return "Adwaita-dark";
    if (strcmp(theme, "catppuccin-latte") == 0) return "Adwaita";
    if (strcmp(theme, "dark") == 0)            return "Adwaita-dark";
    if (strcmp(theme, "light") == 0)           return "Adwaita";
    return "Adwaita";
}

void notes_apply_source_style(NotesWindow *win) {
    GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
    const char *scheme_id = scheme_for_theme(win->settings.theme);

    if (strcmp(win->settings.theme, "system") == 0) {
        AdwStyleManager *adw = adw_style_manager_get_default();
        scheme_id = adw_style_manager_get_dark(adw) ? "Adwaita-dark" : "Adwaita";
    }

    GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(sm, scheme_id);
    if (scheme)
        gtk_source_buffer_set_style_scheme(win->source_buffer, scheme);

    gtk_source_buffer_set_highlight_syntax(win->source_buffer, win->settings.highlight_syntax);
}

void notes_apply_source_language(NotesWindow *win, const char *path) {
    if (!win->settings.highlight_syntax) {
        gtk_source_buffer_set_language(win->source_buffer, NULL);
        return;
    }
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();

    static gboolean paths_set = FALSE;
    if (!paths_set) {
        const gchar * const *old = gtk_source_language_manager_get_search_path(lm);
        GPtrArray *dirs = g_ptr_array_new();

        char exe_dir[1024];
        ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
        if (n > 0) {
            exe_dir[n] = '\0';
            char *slash = strrchr(exe_dir, '/');
            if (slash) *slash = '\0';
            char custom[1088];
            snprintf(custom, sizeof(custom), "%s/../data/language-specs", exe_dir);
            g_ptr_array_add(dirs, g_strdup(custom));
            snprintf(custom, sizeof(custom), "%s/data/language-specs", exe_dir);
            g_ptr_array_add(dirs, g_strdup(custom));
        }

        for (int i = 0; old && old[i]; i++)
            g_ptr_array_add(dirs, g_strdup(old[i]));
        g_ptr_array_add(dirs, NULL);
        gtk_source_language_manager_set_search_path(lm, (const gchar * const *)dirs->pdata);
        g_ptr_array_unref(dirs);
        paths_set = TRUE;
    }

    GtkSourceLanguage *lang = gtk_source_language_manager_guess_language(lm, path, NULL);

    if (!lang) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (g_ascii_strncasecmp(base, "Makefile", 8) == 0 ||
            g_ascii_strncasecmp(base, "GNUmakefile", 11) == 0)
            lang = gtk_source_language_manager_get_language(lm, "makefile");
    }

    gtk_source_buffer_set_language(win->source_buffer, lang);
}

static void css_escape_font(char *out, size_t out_sz, const char *in) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < out_sz - 1; i++) {
        char c = in[i];
        if (c == '}' || c == '{' || c == ';' || c == '"' || c == '\'' || c == '\\')
            continue;
        out[j++] = c;
    }
    out[j] = '\0';
}

void notes_apply_css(NotesWindow *win) {
    char css[4096];

    char safe_font[256], safe_gui_font[256];
    css_escape_font(safe_font, sizeof(safe_font), win->settings.font);
    css_escape_font(safe_gui_font, sizeof(safe_gui_font), win->settings.gui_font);

    const ThemeDef *td = theme_lookup(win->settings.theme);

    if (td) {
        const char *bg = td->bg;
        const char *fg = td->fg;
        gboolean hl = win->settings.highlight_syntax;

        snprintf(css, sizeof(css),
            "textview { font-family: %s; font-size: %dpt; background-color: %s; }"
            "textview text { background-color: %s;%s%s%s }"
            ".line-numbers, .line-numbers text {"
            "  background-color: %s; color: alpha(%s, 0.3); }"
            ".titlebar, headerbar {"
            "  background: %s; color: %s; box-shadow: none; }"
            "headerbar button, headerbar menubutton button,"
            "headerbar menubutton { color: %s; background: transparent; }"
            "headerbar button:hover, headerbar menubutton button:hover {"
            "  background: alpha(%s, 0.1); }"
            ".statusbar { font-size: 10pt; padding: 2px 4px;"
            "  color: alpha(%s, 0.6); background-color: %s; }"
            "window, window.background { background-color: %s; color: %s; }"
            "popover, popover.menu {"
            "  background: transparent; box-shadow: none; border: none; }"
            "popover > contents, popover.menu > contents {"
            "  background-color: %s; color: %s;"
            "  border-radius: 12px; border: none; box-shadow: 0 2px 8px rgba(0,0,0,0.3); }"
            "popover > arrow, popover.menu > arrow { background: transparent; border: none; }"
            "popover modelbutton { color: %s; }"
            "popover modelbutton:hover { background-color: alpha(%s, 0.15); }"
            "windowcontrols button { color: %s; }"
            "label { color: %s; }"
            "entry { background-color: alpha(%s, 0.08); color: %s;"
            "  border: 1px solid alpha(%s, 0.2); }"
            "button { color: %s; }"
            "checkbutton { color: %s; }"
            "scale { color: %s; }"
            "list, listview, row { background-color: %s; color: %s; }"
            "row:hover { background-color: alpha(%s, 0.08); }"
            "row:selected { background-color: alpha(%s, 0.15); }"
            "scrolledwindow { background-color: %s; }"
            "separator { background-color: alpha(%s, 0.15); }",
            safe_font, win->settings.font_size, bg,
            bg, hl ? "" : " color: ", hl ? "" : fg, hl ? "" : ";",
            bg, fg,
            bg, fg,
            fg,
            fg,
            fg, bg,
            bg, fg,
            bg, fg,
            fg,
            fg,
            fg,
            fg,
            fg, fg,
            fg,
            fg,
            fg,
            fg,
            bg, fg,
            fg,
            fg,
            bg,
            fg);
    } else {
        const char *bg, *fg;
        if (notes_is_dark_theme(win->settings.theme)) {
            bg = "#1e1e1e"; fg = "#d4d4d4";
        } else {
            bg = "#ffffff"; fg = "#1e1e1e";
        }
        gboolean hl = win->settings.highlight_syntax;

        snprintf(css, sizeof(css),
            "textview { font-family: %s; font-size: %dpt; background-color: %s; }"
            "textview text { background-color: %s;%s%s%s }"
            ".line-numbers, .line-numbers text {"
            "  background-color: %s; color: alpha(%s, 0.3); }"
            ".statusbar { font-size: 10pt; padding: 2px 4px; opacity: 0.7; }",
            safe_font, win->settings.font_size, bg,
            bg, hl ? "" : " color: ", hl ? "" : fg, hl ? "" : ";",
            bg, fg);
    }

    char gui_css[512];
    snprintf(gui_css, sizeof(gui_css),
        "headerbar, headerbar button, headerbar label,"
        "popover, popover.menu, popover label, popover button,"
        ".statusbar, .statusbar label {"
        "  font-family: %s; font-size: %dpt; }",
        safe_gui_font, win->settings.gui_font_size);
    strncat(css, gui_css, sizeof(css) - strlen(css) - 1);

    gtk_css_provider_load_from_string(win->css_provider, css);
}

void notes_apply_theme(NotesWindow *win) {
    AdwStyleManager *sm = adw_style_manager_get_default();
    gboolean dark = notes_is_dark_theme(win->settings.theme);

    const ThemeDef *td = theme_lookup(win->settings.theme);
    if (td) {
        GdkRGBA c;
        gdk_rgba_parse(&c, td->bg);
        dark = (0.299 * c.red + 0.587 * c.green + 0.114 * c.blue) < 0.5;
    }

    if (strcmp(win->settings.theme, "system") == 0)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);
    else if (dark)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_DARK);
    else
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_LIGHT);
}
