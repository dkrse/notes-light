#include "actions.h"
#include <adwaita.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

/* --- Theme list --- */
static const char *theme_ids[] = {
    "system", "light", "dark",
    "solarized-light", "solarized-dark",
    "monokai",
    "gruvbox-light", "gruvbox-dark",
    "nord", "dracula", "tokyo-night",
    "catppuccin-latte", "catppuccin-mocha",
    NULL
};
static const char *theme_labels[] = {
    "System", "Light", "Dark",
    "Solarized Light", "Solarized Dark",
    "Monokai",
    "Gruvbox Light", "Gruvbox Dark",
    "Nord", "Dracula", "Tokyo Night",
    "Catppuccin Latte", "Catppuccin Mocha",
    NULL
};

static int theme_index_of(const char *id) {
    for (int i = 0; theme_ids[i]; i++)
        if (strcmp(theme_ids[i], id) == 0) return i;
    return 0;
}

/* --- Actions --- */

static void on_new_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    g_free(win->original_content);
    win->original_content = g_strdup("");
    gtk_text_buffer_set_text(win->buffer, "", -1);
    win->dirty = FALSE;
    win->is_binary = FALSE;
    win->is_truncated = FALSE;
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes Light");
    gtk_label_set_text(win->status_encoding, "UTF-8");
    win->current_file[0] = '\0';
    win->settings.last_file[0] = '\0';
    settings_save(&win->settings);
    gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
}

static void on_save(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (!win->dirty) return;
    /* Prevent overwriting original with truncated/binary content */
    if (win->is_truncated || win->is_binary) {
        g_action_group_activate_action(G_ACTION_GROUP(win->window), "save-as", NULL);
        return;
    }

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

    if (win->current_file[0] != '\0') {
        /* Atomic write: write to tmp, then rename */
        char tmp[2112];
        snprintf(tmp, sizeof(tmp), "%s.tmp", win->current_file);
        FILE *f = fopen(tmp, "w");
        if (f) {
            gboolean ok = (fputs(text, f) != EOF);
            ok = ok && (fflush(f) == 0);
            fclose(f);
            if (ok)
                g_rename(tmp, win->current_file);
            else
                g_remove(tmp);
        }
    } else {
        /* No file yet — trigger Save As */
        g_free(text);
        g_action_group_activate_action(G_ACTION_GROUP(win->window), "save-as", NULL);
        return;
    }
    g_free(win->original_content);
    win->original_content = text;
    win->dirty = FALSE;
    win->is_binary = FALSE;

    char *base = g_path_get_basename(win->current_file);
    gtk_window_set_title(GTK_WINDOW(win->window), base);
    g_free(base);
}

static void on_save_as_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *file = gtk_file_dialog_save_finish(dialog, result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(win->buffer, &start, &end);
            char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

            /* Atomic write */
            char tmp[2112];
            snprintf(tmp, sizeof(tmp), "%s.tmp", path);
            FILE *f = fopen(tmp, "w");
            if (f) {
                gboolean ok = (fputs(text, f) != EOF);
                ok = ok && (fflush(f) == 0);
                fclose(f);
                if (ok)
                    g_rename(tmp, path);
                else
                    g_remove(tmp);
            }

            snprintf(win->current_file, sizeof(win->current_file), "%s", path);
            snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", path);
            g_free(win->original_content);
            win->original_content = text;
            win->dirty = FALSE;
            settings_save(&win->settings);

            char *base = g_path_get_basename(path);
            gtk_window_set_title(GTK_WINDOW(win->window), base);
            g_free(base);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(dialog);
}

static void on_save_as(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save As");

    gtk_file_dialog_save(dialog, GTK_WINDOW(win->window), NULL, on_save_as_cb, win);
}

static void on_open_file_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            notes_window_load_file(win, path);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(dialog);
}

static void on_open_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open File");
    gtk_file_dialog_open(dialog, GTK_WINDOW(win->window), NULL, on_open_file_cb, win);
}

static void on_zoom_in(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (win->settings.font_size < 72) {
        win->settings.font_size += 2;
        notes_window_apply_settings(win);
        settings_save(&win->settings);
    }
}

static void on_zoom_out(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (win->settings.font_size > 6) {
        win->settings.font_size -= 2;
        notes_window_apply_settings(win);
        settings_save(&win->settings);
    }
}

/* --- Settings dialog --- */

static void on_settings_apply(GtkButton *button, gpointer data) {
    NotesWindow *win = data;
    settings_save(&win->settings);
    notes_window_apply_settings(win);
    GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    gtk_window_destroy(GTK_WINDOW(toplevel));
}

static void on_settings_cancel(GtkButton *button, gpointer data) {
    (void)data;
    GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    gtk_window_destroy(GTK_WINDOW(toplevel));
}

static void on_theme_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    guint idx = gtk_drop_down_get_selected(dropdown);
    if (theme_ids[idx])
        snprintf(win->settings.theme, sizeof(win->settings.theme), "%s", theme_ids[idx]);
}

static void on_spacing_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    guint idx = gtk_drop_down_get_selected(dropdown);
    double spacings[] = {1.0, 1.2, 1.5, 2.0};
    if (idx < 4)
        win->settings.line_spacing = spacings[idx];
}

static void on_line_numbers_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.show_line_numbers = gtk_check_button_get_active(btn);
}

static void on_highlight_line_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.highlight_current_line = gtk_check_button_get_active(btn);
}

static void on_wrap_lines_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.wrap_lines = gtk_check_button_get_active(btn);
}

static void on_intensity_changed(GtkRange *range, gpointer data) {
    NotesWindow *win = data;
    win->settings.font_intensity = gtk_range_get_value(range);
}

static void on_font_set(GtkFontDialogButton *button, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    PangoFontDescription *desc = gtk_font_dialog_button_get_font_desc(button);
    if (desc) {
        const char *family = pango_font_description_get_family(desc);
        if (family)
            snprintf(win->settings.font, sizeof(win->settings.font), "%s", family);
        int size = pango_font_description_get_size(desc);
        if (size > 0)
            win->settings.font_size = size / PANGO_SCALE;
    }
}

static void on_gui_font_set(GtkFontDialogButton *button, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    PangoFontDescription *desc = gtk_font_dialog_button_get_font_desc(button);
    if (desc) {
        const char *family = pango_font_description_get_family(desc);
        if (family)
            snprintf(win->settings.gui_font, sizeof(win->settings.gui_font), "%s", family);
        int size = pango_font_description_get_size(desc);
        if (size > 0)
            win->settings.gui_font_size = size / PANGO_SCALE;
    }
}

static void on_settings(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Settings");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, -1);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(vbox), grid);

    int row = 0;

    /* Theme */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Theme:"), 0, row, 1, 1);
    GtkWidget *theme_dd = gtk_drop_down_new_from_strings(theme_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(theme_dd), (guint)theme_index_of(win->settings.theme));
    g_signal_connect(theme_dd, "notify::selected", G_CALLBACK(on_theme_changed), win);
    gtk_grid_attach(GTK_GRID(grid), theme_dd, 1, row++, 1, 1);

    /* Font */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font:"), 0, row, 1, 1);
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, win->settings.font);
    pango_font_description_set_size(desc, win->settings.font_size * PANGO_SCALE);
    GtkFontDialog *font_dialog = gtk_font_dialog_new();
    GtkWidget *font_btn = gtk_font_dialog_button_new(font_dialog);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font_btn), desc);
    pango_font_description_free(desc);
    g_signal_connect(font_btn, "notify::font-desc", G_CALLBACK(on_font_set), win);
    gtk_grid_attach(GTK_GRID(grid), font_btn, 1, row++, 1, 1);

    /* GUI font */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("GUI Font:"), 0, row, 1, 1);
    PangoFontDescription *gui_desc = pango_font_description_new();
    pango_font_description_set_family(gui_desc, win->settings.gui_font);
    pango_font_description_set_size(gui_desc, win->settings.gui_font_size * PANGO_SCALE);
    GtkFontDialog *gui_font_dialog = gtk_font_dialog_new();
    GtkWidget *gui_font_btn = gtk_font_dialog_button_new(gui_font_dialog);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(gui_font_btn), gui_desc);
    pango_font_description_free(gui_desc);
    g_signal_connect(gui_font_btn, "notify::font-desc", G_CALLBACK(on_gui_font_set), win);
    gtk_grid_attach(GTK_GRID(grid), gui_font_btn, 1, row++, 1, 1);

    /* Font intensity */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font Intensity:"), 0, row, 1, 1);
    GtkWidget *intensity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.3, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(intensity_scale), win->settings.font_intensity);
    g_signal_connect(intensity_scale, "value-changed", G_CALLBACK(on_intensity_changed), win);
    gtk_grid_attach(GTK_GRID(grid), intensity_scale, 1, row++, 1, 1);

    /* Line spacing */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Line Spacing:"), 0, row, 1, 1);
    const char *spacings[] = {"1", "1.2", "1.5", "2", NULL};
    GtkWidget *sp_dd = gtk_drop_down_new_from_strings(spacings);
    guint sp_idx = 0;
    if (win->settings.line_spacing >= 1.9) sp_idx = 3;
    else if (win->settings.line_spacing >= 1.4) sp_idx = 2;
    else if (win->settings.line_spacing >= 1.1) sp_idx = 1;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(sp_dd), sp_idx);
    g_signal_connect(sp_dd, "notify::selected", G_CALLBACK(on_spacing_changed), win);
    gtk_grid_attach(GTK_GRID(grid), sp_dd, 1, row++, 1, 1);

    /* Line numbers */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Line Numbers:"), 0, row, 1, 1);
    GtkWidget *ln_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ln_check), win->settings.show_line_numbers);
    g_signal_connect(ln_check, "toggled", G_CALLBACK(on_line_numbers_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), ln_check, 1, row++, 1, 1);

    /* Highlight current line */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Highlight Line:"), 0, row, 1, 1);
    GtkWidget *hl_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(hl_check), win->settings.highlight_current_line);
    g_signal_connect(hl_check, "toggled", G_CALLBACK(on_highlight_line_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), hl_check, 1, row++, 1, 1);

    /* Wrap lines */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Wrap Lines:"), 0, row, 1, 1);
    GtkWidget *wrap_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(wrap_check), win->settings.wrap_lines);
    g_signal_connect(wrap_check, "toggled", G_CALLBACK(on_wrap_lines_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), wrap_check, 1, row++, 1, 1);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *apply_btn = gtk_button_new_with_label("Apply");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_settings_cancel), win);
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_settings_apply), win);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    gtk_box_append(GTK_BOX(btn_box), apply_btn);
    gtk_box_append(GTK_BOX(vbox), btn_box);

    gtk_window_present(GTK_WINDOW(dialog));
}

void actions_setup(NotesWindow *win, GtkApplication *app) {
    static const GActionEntry win_entries[] = {
        {"new-file",    on_new_file,   NULL, NULL, NULL, {0}},
        {"save",        on_save,       NULL, NULL, NULL, {0}},
        {"save-as",     on_save_as,    NULL, NULL, NULL, {0}},
        {"open-file",   on_open_file,  NULL, NULL, NULL, {0}},
        {"settings",    on_settings,   NULL, NULL, NULL, {0}},
        {"zoom-in",     on_zoom_in,    NULL, NULL, NULL, {0}},
        {"zoom-out",    on_zoom_out,   NULL, NULL, NULL, {0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(win->window),
                                   win_entries, G_N_ELEMENTS(win_entries), win);

    const char *zoom_in_accels[]  = {"<Control>plus", "<Control>equal", NULL};
    const char *zoom_out_accels[] = {"<Control>minus", NULL};
    const char *quit_accels[]     = {"<Control>q", NULL};
    const char *open_accels[]     = {"<Control>o", NULL};
    const char *save_accels[]     = {"<Control>s", NULL};
    const char *new_accels[]      = {"<Control>n", NULL};
    const char *save_as_accels[]  = {"<Control><Shift>s", NULL};

    gtk_application_set_accels_for_action(app, "win.zoom-in",   zoom_in_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-out",  zoom_out_accels);
    gtk_application_set_accels_for_action(app, "app.quit",      quit_accels);
    gtk_application_set_accels_for_action(app, "win.open-file", open_accels);
    gtk_application_set_accels_for_action(app, "win.save",      save_accels);
    gtk_application_set_accels_for_action(app, "win.new-file",  new_accels);
    gtk_application_set_accels_for_action(app, "win.save-as",   save_as_accels);
}
