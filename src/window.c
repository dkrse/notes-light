#define _GNU_SOURCE
#include <adwaita.h>
#include "window.h"
#include "actions.h"
#include "ssh.h"
#include "ssh_window.h"
#include "editor_view.h"
#include "search.h"
#include "theme.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void on_file_monitor_changed(GFileMonitor *mon, GFile *file, GFile *other,
                                     GFileMonitorEvent event, gpointer data);

void notes_window_recent_menu_rebuild(NotesWindow *win) {
    if (!win->recent_menu) return;
    g_menu_remove_all(win->recent_menu);
    for (int i = 0; i < win->settings.recent_count; i++) {
        const char *p = win->settings.recent_files[i];
        if (!p[0]) continue;
        char *base = g_path_get_basename(p);
        GMenuItem *item = g_menu_item_new(base, NULL);
        g_menu_item_set_action_and_target_value(item, "win.open-recent",
            g_variant_new_string(p));
        g_menu_append_item(win->recent_menu, item);
        g_object_unref(item);
        g_free(base);
    }
}

guint32 fnv1a_hash(const char *data, gsize len) {
    guint32 h = 2166136261u;
    for (gsize i = 0; i < len; i++) {
        h ^= (guint8)data[i];
        h *= 16777619u;
    }
    return h;
}

void notes_window_apply_settings(NotesWindow *win) {
    notes_apply_theme(win);
    notes_apply_css(win);
    editor_view_apply_settings(win);
    notes_apply_source_style(win);
}

#define MAX_DISPLAY_BYTES (5 * 1024 * 1024)

static const char *human_size(gsize bytes) {
    static char buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%lu B", (unsigned long)bytes);
    return buf;
}

void notes_window_load_file(NotesWindow *win, const char *path) {
    if (!path || path[0] == '\0') return;

    struct stat st;
    if (g_stat(path, &st) != 0) return;
    gsize file_size = (gsize)st.st_size;

    gboolean truncated = FALSE;
    gsize read_size = file_size;
    if (read_size > MAX_DISPLAY_BYTES) {
        read_size = MAX_DISPLAY_BYTES;
        truncated = TRUE;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    char *contents = g_malloc(read_size + 1);
    gsize len = fread(contents, 1, read_size, fp);
    fclose(fp);
    contents[len] = '\0';

    gboolean is_binary = FALSE;
    gsize check_len = len < 8192 ? len : 8192;
    for (gsize i = 0; i < check_len; i++) {
        if (contents[i] == '\0') { is_binary = TRUE; break; }
    }

    if (is_binary) {
        for (gsize i = 0; i < len; i++) {
            if (contents[i] == '\0') contents[i] = '.';
        }
    }

    if (!g_utf8_validate(contents, (gssize)len, NULL)) {
        is_binary = TRUE;
        gsize bytes_written = 0;
        char *utf8 = g_convert_with_fallback(contents, (gssize)len,
                         "UTF-8", "ISO-8859-1", ".", NULL, &bytes_written, NULL);
        if (utf8) {
            g_free(contents);
            contents = utf8;
            len = bytes_written;
        }
    }

    g_free(win->original_content);
    if (is_binary || truncated) {
        win->original_content = NULL;
        win->original_hash = 0;
    } else {
        win->original_hash = fnv1a_hash(contents, len);
        win->original_content = g_strndup(contents, len);
    }

    win->is_binary = is_binary;
    win->is_truncated = truncated;

    notes_apply_source_language(win, path);
    notes_apply_source_style(win);

    /* (re)install file monitor for auto-reload */
    if (win->file_monitor) {
        g_signal_handlers_disconnect_by_func(win->file_monitor,
            on_file_monitor_changed, win);
        g_object_unref(win->file_monitor);
        win->file_monitor = NULL;
    }
    GFile *gf = g_file_new_for_path(path);
    win->file_monitor = g_file_monitor_file(gf, G_FILE_MONITOR_NONE, NULL, NULL);
    g_object_unref(gf);
    if (win->file_monitor)
        g_signal_connect(win->file_monitor, "changed",
                         G_CALLBACK(on_file_monitor_changed), win);

    editor_view_block_signals(win);

    gtk_text_buffer_set_text(win->buffer, contents, (int)len);
    g_free(contents);

    GtkTextIter start;
    gtk_text_buffer_get_start_iter(win->buffer, &start);
    gtk_text_buffer_place_cursor(win->buffer, &start);

    win->dirty = FALSE;
    snprintf(win->current_file, sizeof(win->current_file), "%s", path);
    snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", path);

    char *base = g_path_get_basename(path);
    gtk_window_set_title(GTK_WINDOW(win->window), base);
    g_free(base);

    editor_view_unblock_signals(win);

    char status[128];
    if (truncated)
        snprintf(status, sizeof(status), "UTF-8 | %s | showing first 5 MB of %s",
                 is_binary ? "BIN" : "TEXT", human_size(file_size));
    else
        snprintf(status, sizeof(status), "UTF-8 | %s | %s",
                 is_binary ? "BIN" : "TEXT", human_size(file_size));
    gtk_label_set_text(win->status_encoding, status);

    editor_view_update_cursor_position(win);
    if (win->settings.show_line_numbers)
        editor_view_update_line_numbers(win);
    editor_view_update_line_highlights(win);
    editor_view_apply_font_intensity(win);

    settings_push_recent(&win->settings, path);
    notes_window_recent_menu_rebuild(win);

    settings_save(&win->settings);
}

/* ── Auto-reload (file monitor) ── */

static void reload_keep_cursor(NotesWindow *win) {
    GtkTextMark *insert = gtk_text_buffer_get_insert(win->buffer);
    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(win->buffer, &cur, insert);
    int line = gtk_text_iter_get_line(&cur);
    int col  = gtk_text_iter_get_line_offset(&cur);

    char path[2048];
    g_strlcpy(path, win->current_file, sizeof(path));
    notes_window_load_file(win, path);

    int total = gtk_text_buffer_get_line_count(win->buffer);
    if (line >= total) line = total - 1;
    if (line < 0) line = 0;
    GtkTextIter restore;
    gtk_text_buffer_get_iter_at_line(win->buffer, &restore, line);
    int line_chars = gtk_text_iter_get_chars_in_line(&restore);
    if (col > line_chars) col = line_chars;
    gtk_text_iter_set_line_offset(&restore, col);
    gtk_text_buffer_place_cursor(win->buffer, &restore);
    gtk_text_view_scroll_to_iter(win->text_view, &restore, 0.2, FALSE, 0, 0);
}

static void on_reload_dirty_response(GObject *src, GAsyncResult *res, gpointer data) {
    NotesWindow *win = data;
    int btn = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, NULL);
    if (btn == 0) {
        win->dirty = FALSE;
        reload_keep_cursor(win);
    }
}

static void on_file_monitor_changed(GFileMonitor *mon, GFile *file, GFile *other,
                                     GFileMonitorEvent event, gpointer data) {
    (void)mon; (void)file; (void)other;
    NotesWindow *win = data;

    if (event != G_FILE_MONITOR_EVENT_CHANGED &&
        event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event != G_FILE_MONITOR_EVENT_CREATED)
        return;
    if (win->current_file[0] == '\0') return;
    if (notes_window_is_remote(win)) return;
    if (ssh_path_is_remote(win->current_file)) return;

    /* Skip events caused by our own writes — compare hash with original_hash */
    char *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(win->current_file, &contents, &len, NULL)) return;
    guint32 h = fnv1a_hash(contents, len);
    g_free(contents);
    if (h == win->original_hash) return;

    if (win->dirty) {
        GtkAlertDialog *dlg = gtk_alert_dialog_new("File changed on disk");
        gtk_alert_dialog_set_detail(dlg,
            "The file has been modified outside the editor while you have unsaved changes.");
        const char *buttons[] = {"Reload (discard my changes)", "Keep my version", NULL};
        gtk_alert_dialog_set_buttons(dlg, buttons);
        gtk_alert_dialog_set_default_button(dlg, 1);
        gtk_alert_dialog_set_cancel_button(dlg, 1);
        gtk_alert_dialog_choose(dlg, GTK_WINDOW(win->window), NULL,
                                on_reload_dirty_response, win);
        g_object_unref(dlg);
        return;
    }

    reload_keep_cursor(win);
}

/* ── Print ── */

typedef struct {
    char        *text;
    PangoLayout *layout;
    GArray      *page_breaks;  /* line index where each page ends */
} PrintData;

static void print_data_free(gpointer p) {
    PrintData *pd = p;
    g_free(pd->text);
    if (pd->layout) g_object_unref(pd->layout);
    if (pd->page_breaks) g_array_unref(pd->page_breaks);
    g_free(pd);
}

static void on_begin_print(GtkPrintOperation *op, GtkPrintContext *ctx, gpointer data) {
    PrintData *pd = data;
    NotesWindow *win = g_object_get_data(G_OBJECT(op), "notes-win");

    pd->layout = gtk_print_context_create_pango_layout(ctx);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, win->settings.font);
    pango_font_description_set_size(fd, win->settings.font_size * PANGO_SCALE);
    pango_layout_set_font_description(pd->layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(pd->layout, pd->text, -1);
    pango_layout_set_width(pd->layout,
        (int)(gtk_print_context_get_width(ctx) * PANGO_SCALE));
    pango_layout_set_wrap(pd->layout, PANGO_WRAP_WORD_CHAR);

    pd->page_breaks = g_array_new(FALSE, FALSE, sizeof(int));
    double page_h = gtk_print_context_get_height(ctx);
    int n_lines = pango_layout_get_line_count(pd->layout);
    double y = 0;
    for (int i = 0; i < n_lines; i++) {
        PangoLayoutLine *line = pango_layout_get_line_readonly(pd->layout, i);
        PangoRectangle logical;
        pango_layout_line_get_extents(line, NULL, &logical);
        double line_h = (double)logical.height / PANGO_SCALE;
        if (y + line_h > page_h && i > 0) {
            int idx = i;
            g_array_append_val(pd->page_breaks, idx);
            y = 0;
        }
        y += line_h;
    }
    int sentinel = n_lines;
    g_array_append_val(pd->page_breaks, sentinel);

    gtk_print_operation_set_n_pages(op, (int)pd->page_breaks->len);
}

static void on_draw_page(GtkPrintOperation *op, GtkPrintContext *ctx,
                          int page_nr, gpointer data) {
    (void)op;
    PrintData *pd = data;
    cairo_t *cr = gtk_print_context_get_cairo_context(ctx);

    int start = (page_nr == 0) ? 0 : g_array_index(pd->page_breaks, int, page_nr - 1);
    int end   = g_array_index(pd->page_breaks, int, page_nr);

    cairo_set_source_rgb(cr, 0, 0, 0);
    double y = 0;
    for (int i = start; i < end; i++) {
        PangoLayoutLine *line = pango_layout_get_line_readonly(pd->layout, i);
        PangoRectangle logical;
        pango_layout_line_get_extents(line, NULL, &logical);
        double baseline_off = -(double)logical.y / PANGO_SCALE;
        cairo_move_to(cr, 0, y + baseline_off);
        pango_cairo_show_layout_line(cr, line);
        y += (double)logical.height / PANGO_SCALE;
    }
}

void notes_window_print(NotesWindow *win) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);

    PrintData *pd = g_new0(PrintData, 1);
    pd->text = gtk_text_buffer_get_text(win->buffer, &s, &e, FALSE);

    GtkPrintOperation *op = gtk_print_operation_new();
    g_object_set_data(G_OBJECT(op), "notes-win", win);
    g_signal_connect(op, "begin-print", G_CALLBACK(on_begin_print), pd);
    g_signal_connect(op, "draw-page", G_CALLBACK(on_draw_page), pd);
    g_object_set_data_full(G_OBJECT(op), "print-data", pd, print_data_free);

    if (win->current_file[0]) {
        char *base = g_path_get_basename(win->current_file);
        gtk_print_operation_set_job_name(op, base);
        g_free(base);
    }

    gtk_print_operation_run(op, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                            GTK_WINDOW(win->window), NULL);
    g_object_unref(op);
}

static void auto_save_current(NotesWindow *win) {
    if (!win->dirty) return;
    if (win->is_binary || !win->original_content) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);
    if (!text || text[0] == '\0') {
        g_free(text);
        return;
    }

    if (win->current_file[0] != '\0') {
        char tmp[2112];
        snprintf(tmp, sizeof(tmp), "%s.XXXXXX", win->current_file);
        int fd = g_mkstemp(tmp);
        if (fd >= 0) {
            FILE *f = fdopen(fd, "w");
            if (f) {
                gboolean ok = (fputs(text, f) != EOF);
                ok = ok && (fflush(f) == 0);
                fclose(f);
                if (ok)
                    g_rename(tmp, win->current_file);
                else
                    g_remove(tmp);
            } else {
                close(fd);
                g_remove(tmp);
            }
        }
        snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", win->current_file);
    }
    g_free(win->original_content);
    win->original_content = text;
    win->original_hash = fnv1a_hash(text, strlen(text));
    win->dirty = FALSE;
}

static void close_and_cleanup(NotesWindow *win) {
    if (notes_window_is_remote(win))
        notes_window_ssh_disconnect(win);

    if (win->current_file[0] != '\0' && !ssh_path_is_remote(win->current_file))
        snprintf(win->settings.last_file, sizeof(win->settings.last_file),
                 "%s", win->current_file);

    win->settings.window_width = gtk_widget_get_width(GTK_WIDGET(win->window));
    win->settings.window_height = gtk_widget_get_height(GTK_WIDGET(win->window));
    settings_save(&win->settings);
    gtk_window_destroy(GTK_WINDOW(win->window));
}

static void on_close_save_response(GObject *src, GAsyncResult *res, gpointer data) {
    NotesWindow *win = data;
    GtkAlertDialog *dlg = GTK_ALERT_DIALOG(src);
    int btn = gtk_alert_dialog_choose_finish(dlg, res, NULL);

    if (btn == 0) {
        auto_save_current(win);
        close_and_cleanup(win);
    } else if (btn == 1) {
        win->dirty = FALSE;
        close_and_cleanup(win);
    }
}

static gboolean on_close_request(GtkWindow *window, gpointer data) {
    (void)window;
    NotesWindow *win = data;

    if (!win->dirty) {
        close_and_cleanup(win);
        return TRUE;
    }

    GtkAlertDialog *dlg = gtk_alert_dialog_new("Save changes before closing?");
    gtk_alert_dialog_set_detail(dlg, "If you don't save, your changes will be lost.");
    const char *buttons[] = {"Save", "Don't Save", "Cancel", NULL};
    gtk_alert_dialog_set_buttons(dlg, buttons);
    gtk_alert_dialog_set_default_button(dlg, 0);
    gtk_alert_dialog_set_cancel_button(dlg, 2);
    gtk_alert_dialog_choose(dlg, GTK_WINDOW(win->window), NULL, on_close_save_response, win);
    g_object_unref(dlg);

    return TRUE;
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;

    if (win->intensity_idle_id) {
        g_source_remove(win->intensity_idle_id);
        win->intensity_idle_id = 0;
    }
    if (win->scroll_idle_id) {
        g_source_remove(win->scroll_idle_id);
        win->scroll_idle_id = 0;
    }
    if (win->line_numbers_idle_id) {
        g_source_remove(win->line_numbers_idle_id);
        win->line_numbers_idle_id = 0;
    }
    if (win->title_idle_id) {
        g_source_remove(win->title_idle_id);
        win->title_idle_id = 0;
    }
    gtk_style_context_remove_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(win->css_provider));
    g_object_unref(win->css_provider);

    if (win->file_monitor) {
        g_signal_handlers_disconnect_by_func(win->file_monitor,
            on_file_monitor_changed, win);
        g_object_unref(win->file_monitor);
        win->file_monitor = NULL;
    }
    if (win->recent_menu) {
        g_object_unref(win->recent_menu);
        win->recent_menu = NULL;
    }

    g_free(win->match_lines);
    g_free(win->match_offsets);
    g_free(win->original_content);
    g_free(win);
}

static GtkWidget *build_menu_button(NotesWindow *win) {
    GMenu *menu = g_menu_new();

    GMenu *file_section = g_menu_new();
    g_menu_append(file_section, "New", "win.new-file");
    g_menu_append(file_section, "Open File", "win.open-file");

    win->recent_menu = g_menu_new();
    g_menu_append_submenu(file_section, "Open Recent",
                          G_MENU_MODEL(win->recent_menu));

    g_menu_append(file_section, "Save", "win.save");
    g_menu_append(file_section, "Save As...", "win.save-as");
    g_menu_append(file_section, "Print...", "win.print");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(file_section));
    g_object_unref(file_section);

    GMenu *edit_section = g_menu_new();
    g_menu_append(edit_section, "Undo", "win.undo");
    g_menu_append(edit_section, "Redo", "win.redo");
    g_menu_append(edit_section, "Find", "win.find");
    g_menu_append(edit_section, "Find & Replace", "win.find-replace");
    g_menu_append(edit_section, "Go to Line", "win.goto-line");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(edit_section));
    g_object_unref(edit_section);

    GMenu *view_section = g_menu_new();
    g_menu_append(view_section, "Show Whitespace", "win.toggle-whitespace");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(view_section));
    g_object_unref(view_section);

    GMenu *ssh_section = g_menu_new();
    g_menu_append(ssh_section, "SFTP Connect...", "win.sftp-connect");
    g_menu_append(ssh_section, "Open Remote File", "win.open-remote");
    g_menu_append(ssh_section, "SFTP Disconnect", "win.sftp-disconnect");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(ssh_section));
    g_object_unref(ssh_section);

    g_menu_append(menu, "Settings", "win.settings");

    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    g_object_unref(menu);
    return button;
}

NotesWindow *notes_window_new(GtkApplication *app) {
    NotesWindow *win = g_new0(NotesWindow, 1);
    settings_load(&win->settings);

    win->window = GTK_APPLICATION_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes Light");
    gtk_window_set_default_size(GTK_WINDOW(win->window),
                                win->settings.window_width,
                                win->settings.window_height);

    g_signal_connect(win->window, "close-request", G_CALLBACK(on_close_request), win);
    g_signal_connect(win->window, "destroy", G_CALLBACK(on_destroy), win);

    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *menu_btn = build_menu_button(win);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_btn);
    gtk_window_set_titlebar(GTK_WINDOW(win->window), header);

    editor_view_create(win);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(win->text_view));
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);

    win->ln_scrolled = GTK_WIDGET(win->line_numbers);
    gtk_widget_set_vexpand(win->ln_scrolled, TRUE);

    GtkAdjustment *main_vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    g_signal_connect_swapped(main_vadj, "value-changed",
                             G_CALLBACK(gtk_widget_queue_draw), win->line_numbers);

    win->scrolled_window = scrolled;

    GtkWidget *scroll_overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(scroll_overlay), scrolled);

    win->scrollbar_overlay = gtk_drawing_area_new();
    gtk_widget_set_halign(win->scrollbar_overlay, GTK_ALIGN_END);
    gtk_widget_set_size_request(win->scrollbar_overlay, 6, -1);
    gtk_widget_set_can_target(win->scrollbar_overlay, FALSE);
    search_init_scrollbar_overlay(win);
    gtk_overlay_add_overlay(GTK_OVERLAY(scroll_overlay), win->scrollbar_overlay);

    win->editor_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(win->editor_box), win->ln_scrolled);
    gtk_box_append(GTK_BOX(win->editor_box), scroll_overlay);

    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status_bar, "statusbar");
    gtk_widget_set_margin_start(status_bar, 8);
    gtk_widget_set_margin_end(status_bar, 8);
    gtk_widget_set_margin_top(status_bar, 2);
    gtk_widget_set_margin_bottom(status_bar, 2);

    win->status_encoding = GTK_LABEL(gtk_label_new("UTF-8"));
    gtk_widget_set_halign(GTK_WIDGET(win->status_encoding), GTK_ALIGN_START);
    gtk_widget_set_hexpand(GTK_WIDGET(win->status_encoding), TRUE);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(win->status_encoding));

    win->ssh_status_btn = gtk_button_new_with_label("SSH: Off");
    gtk_widget_add_css_class(win->ssh_status_btn, "flat");
    gtk_widget_set_halign(win->ssh_status_btn, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(status_bar), win->ssh_status_btn);

    win->status_cursor = GTK_LABEL(gtk_label_new("Ln 1, Col 1"));
    gtk_widget_set_halign(GTK_WIDGET(win->status_cursor), GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(win->status_cursor));

    GtkWidget *search_bar = search_build_bar(win);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(win->editor_box, TRUE);
    gtk_box_append(GTK_BOX(vbox), search_bar);
    gtk_box_append(GTK_BOX(vbox), win->editor_box);
    gtk_box_append(GTK_BOX(vbox), status_bar);

    gtk_window_set_child(GTK_WINDOW(win->window), vbox);

    win->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(win->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    actions_setup(win, app);

    notes_window_recent_menu_rebuild(win);

    ssh_window_update_status(win);

    notes_window_apply_settings(win);

    if (win->settings.last_file[0] != '\0')
        notes_window_load_file(win, win->settings.last_file);

    return win;
}
