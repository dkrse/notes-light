#define _GNU_SOURCE
#include "search.h"
#include "theme.h"
#include <adwaita.h>
#include <stdlib.h>
#include <string.h>

static void search_clear_matches(NotesWindow *win) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);
    gtk_text_buffer_remove_tag(win->buffer, win->search_tag, &s, &e);
    g_free(win->match_lines);
    win->match_lines = NULL;
    g_free(win->match_offsets);
    win->match_offsets = NULL;
    win->match_count = 0;
    win->match_current = -1;
}

static void search_update_label(NotesWindow *win) {
    char buf[64];
    if (win->match_count == 0)
        snprintf(buf, sizeof(buf), "No results");
    else
        snprintf(buf, sizeof(buf), "%d of %d", win->match_current + 1, win->match_count);
    gtk_label_set_text(GTK_LABEL(win->match_label), buf);
}

static void search_highlight_all(NotesWindow *win) {
    search_clear_matches(win);

    const char *text = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    if (!text || text[0] == '\0') {
        search_update_label(win);
        gtk_widget_queue_draw(win->scrollbar_overlay);
        return;
    }

    GArray *lines = g_array_new(FALSE, FALSE, sizeof(int));
    GArray *offsets = g_array_new(FALSE, FALSE, sizeof(int));
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(win->buffer, &start);

    GtkTextIter match_start, match_end;
    while (gtk_text_iter_forward_search(&start, text,
               GTK_TEXT_SEARCH_CASE_INSENSITIVE, &match_start, &match_end, NULL)) {
        gtk_text_buffer_apply_tag(win->buffer, win->search_tag, &match_start, &match_end);
        int line = gtk_text_iter_get_line(&match_start);
        int offset = gtk_text_iter_get_offset(&match_start);
        g_array_append_val(lines, line);
        g_array_append_val(offsets, offset);
        start = match_end;
    }

    win->match_count = (int)lines->len;
    win->match_lines = (int *)g_array_free(lines, FALSE);
    win->match_offsets = (int *)g_array_free(offsets, FALSE);
    win->match_current = win->match_count > 0 ? 0 : -1;

    search_update_label(win);
    gtk_widget_queue_draw(win->scrollbar_overlay);
}

static void search_goto_match(NotesWindow *win, int idx) {
    if (win->match_count == 0 || idx < 0 || !win->match_offsets) return;

    const char *text = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    if (!text || text[0] == '\0') return;

    GtkTextIter ms, me;
    gtk_text_buffer_get_iter_at_offset(win->buffer, &ms, win->match_offsets[idx]);
    glong search_len = g_utf8_strlen(text, -1);
    me = ms;
    gtk_text_iter_forward_chars(&me, (gint)search_len);
    gtk_text_buffer_select_range(win->buffer, &ms, &me);
    gtk_text_view_scroll_to_iter(win->text_view, &ms, 0.2, FALSE, 0, 0);
    win->match_current = idx;
    search_update_label(win);
}

static void on_search_changed(GtkEditable *editable, gpointer data) {
    (void)editable;
    NotesWindow *win = data;
    search_highlight_all(win);
    if (win->match_count > 0)
        search_goto_match(win, 0);
}

static void on_search_next(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;
    if (win->match_count == 0) return;
    int next = (win->match_current + 1) % win->match_count;
    search_goto_match(win, next);
}

static void on_search_prev(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;
    if (win->match_count == 0) return;
    int prev = (win->match_current - 1 + win->match_count) % win->match_count;
    search_goto_match(win, prev);
}

static void search_bar_close(NotesWindow *win) {
    search_clear_matches(win);
    gtk_widget_set_visible(win->search_bar, FALSE);
    gtk_widget_queue_draw(win->scrollbar_overlay);
    gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
}

static void on_search_close(GtkWidget *widget, gpointer data) {
    (void)widget;
    search_bar_close(data);
}

static void on_replace_one(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;
    if (win->match_count == 0 || win->match_current < 0) return;

    const char *find = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    const char *repl = gtk_editable_get_text(GTK_EDITABLE(win->replace_entry));
    if (!find || find[0] == '\0') return;

    GtkTextIter sel_start, sel_end;
    if (gtk_text_buffer_get_selection_bounds(win->buffer, &sel_start, &sel_end)) {
        gtk_text_buffer_delete(win->buffer, &sel_start, &sel_end);
        gtk_text_buffer_insert(win->buffer, &sel_start, repl, -1);
    }

    search_highlight_all(win);
    if (win->match_count > 0) {
        if (win->match_current >= win->match_count)
            win->match_current = 0;
        search_goto_match(win, win->match_current);
    }
}

static void on_replace_all(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;

    const char *find = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    const char *repl = gtk_editable_get_text(GTK_EDITABLE(win->replace_entry));
    if (!find || find[0] == '\0') return;

    GtkTextIter start;
    gtk_text_buffer_get_start_iter(win->buffer, &start);

    int replaced = 0;
    GtkTextIter ms, me;
    gtk_text_buffer_begin_user_action(win->buffer);
    while (gtk_text_iter_forward_search(&start, find,
               GTK_TEXT_SEARCH_CASE_INSENSITIVE, &ms, &me, NULL)) {
        gtk_text_buffer_delete(win->buffer, &ms, &me);
        gtk_text_buffer_insert(win->buffer, &ms, repl, -1);
        start = ms;
        replaced++;
    }
    gtk_text_buffer_end_user_action(win->buffer);

    search_highlight_all(win);

    char buf[64];
    snprintf(buf, sizeof(buf), "Replaced %d", replaced);
    gtk_label_set_text(GTK_LABEL(win->match_label), buf);
}

static void draw_scrollbar_markers(GtkDrawingArea *area, cairo_t *cr,
                                    int width, int height, gpointer data) {
    (void)area;
    NotesWindow *win = data;
    if (win->match_count == 0) return;

    int total_lines = gtk_text_buffer_get_line_count(win->buffer);
    if (total_lines <= 0) return;

    if (notes_is_dark_theme(win->settings.theme))
        cairo_set_source_rgba(cr, 1.0, 0.7, 0.2, 0.9);
    else
        cairo_set_source_rgba(cr, 1.0, 0.5, 0.0, 0.9);

    double marker_h = 2.0;
    for (int i = 0; i < win->match_count; i++) {
        double y = ((double)win->match_lines[i] / total_lines) * height;
        cairo_rectangle(cr, 0, y, width, marker_h);
        cairo_fill(cr);
    }
}

static gboolean on_search_entry_key(GtkEventControllerKey *ctrl,
                                     guint keyval, guint keycode,
                                     GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode;
    NotesWindow *win = data;
    if (keyval == GDK_KEY_Escape) {
        search_bar_close(win);
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        if (state & GDK_SHIFT_MASK)
            on_search_prev(NULL, win);
        else
            on_search_next(NULL, win);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_replace_entry_key(GtkEventControllerKey *ctrl,
                                      guint keyval, guint keycode,
                                      GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state;
    NotesWindow *win = data;
    if (keyval == GDK_KEY_Escape) {
        search_bar_close(win);
        return TRUE;
    }
    return FALSE;
}

GtkWidget *search_build_bar(NotesWindow *win) {
    win->search_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(win->search_bar, 8);
    gtk_widget_set_margin_end(win->search_bar, 8);
    gtk_widget_set_margin_top(win->search_bar, 4);
    gtk_widget_set_margin_bottom(win->search_bar, 4);
    gtk_widget_set_visible(win->search_bar, FALSE);

    GtkWidget *search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    win->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(win->search_entry), "Find...");
    gtk_widget_set_hexpand(win->search_entry, TRUE);
    g_signal_connect(win->search_entry, "changed", G_CALLBACK(on_search_changed), win);

    GtkEventController *search_key = gtk_event_controller_key_new();
    g_signal_connect(search_key, "key-pressed", G_CALLBACK(on_search_entry_key), win);
    gtk_widget_add_controller(win->search_entry, search_key);

    GtkWidget *prev_btn = gtk_button_new_from_icon_name("go-up-symbolic");
    GtkWidget *next_btn = gtk_button_new_from_icon_name("go-down-symbolic");
    g_signal_connect(prev_btn, "clicked", G_CALLBACK(on_search_prev), win);
    g_signal_connect(next_btn, "clicked", G_CALLBACK(on_search_next), win);

    win->match_label = gtk_label_new("");
    gtk_widget_set_size_request(win->match_label, 80, -1);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_search_close), win);

    gtk_box_append(GTK_BOX(search_row), win->search_entry);
    gtk_box_append(GTK_BOX(search_row), prev_btn);
    gtk_box_append(GTK_BOX(search_row), next_btn);
    gtk_box_append(GTK_BOX(search_row), win->match_label);
    gtk_box_append(GTK_BOX(search_row), close_btn);
    gtk_box_append(GTK_BOX(win->search_bar), search_row);

    win->replace_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    win->replace_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(win->replace_entry), "Replace...");
    gtk_widget_set_hexpand(win->replace_entry, TRUE);

    GtkEventController *replace_key = gtk_event_controller_key_new();
    g_signal_connect(replace_key, "key-pressed", G_CALLBACK(on_replace_entry_key), win);
    gtk_widget_add_controller(win->replace_entry, replace_key);

    GtkWidget *repl_btn = gtk_button_new_with_label("Replace");
    GtkWidget *repl_all_btn = gtk_button_new_with_label("All");
    g_signal_connect(repl_btn, "clicked", G_CALLBACK(on_replace_one), win);
    g_signal_connect(repl_all_btn, "clicked", G_CALLBACK(on_replace_all), win);

    gtk_box_append(GTK_BOX(win->replace_box), win->replace_entry);
    gtk_box_append(GTK_BOX(win->replace_box), repl_btn);
    gtk_box_append(GTK_BOX(win->replace_box), repl_all_btn);
    gtk_box_append(GTK_BOX(win->search_bar), win->replace_box);
    gtk_widget_set_visible(win->replace_box, FALSE);

    return win->search_bar;
}

void search_init_scrollbar_overlay(NotesWindow *win) {
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(win->scrollbar_overlay),
                                    draw_scrollbar_markers, win, NULL);
}

void search_show(NotesWindow *win, gboolean with_replace) {
    gtk_widget_set_visible(win->search_bar, TRUE);
    gtk_widget_set_visible(win->replace_box, with_replace);
    gtk_widget_grab_focus(win->search_entry);

    GtkTextIter sel_s, sel_e;
    if (gtk_text_buffer_get_selection_bounds(win->buffer, &sel_s, &sel_e)) {
        if (gtk_text_iter_get_line(&sel_s) == gtk_text_iter_get_line(&sel_e)) {
            char *sel = gtk_text_buffer_get_text(win->buffer, &sel_s, &sel_e, FALSE);
            gtk_editable_set_text(GTK_EDITABLE(win->search_entry), sel);
            g_free(sel);
        }
    }
    gtk_editable_select_region(GTK_EDITABLE(win->search_entry), 0, -1);
}

/* Public API expected by header */
void notes_window_show_search(NotesWindow *win, gboolean with_replace) {
    search_show(win, with_replace);
}

typedef struct { NotesWindow *win; GtkWidget *entry; GtkWidget *dialog; } GotoData;

static void on_goto_activate(GtkEntry *e, gpointer data) {
    (void)e;
    GotoData *gd = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(gd->entry));
    int line = atoi(text);
    if (line < 1) line = 1;
    int total = gtk_text_buffer_get_line_count(gd->win->buffer);
    if (line > total) line = total;

    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line(gd->win->buffer, &iter, line - 1);
    gtk_text_buffer_place_cursor(gd->win->buffer, &iter);
    gtk_text_view_scroll_to_iter(gd->win->text_view, &iter, 0.2, FALSE, 0, 0);

    gtk_window_destroy(GTK_WINDOW(gd->dialog));
}

static gboolean on_goto_key(GtkEventControllerKey *ctrl, guint keyval,
                             guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state;
    if (keyval == GDK_KEY_Escape) {
        gtk_window_destroy(GTK_WINDOW(((GotoData *)data)->dialog));
        return TRUE;
    }
    return FALSE;
}

void search_goto_line_dialog(NotesWindow *win) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Go to Line");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 260, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);

    int total = gtk_text_buffer_get_line_count(win->buffer);
    char label_text[64];
    snprintf(label_text, sizeof(label_text), "Line number (1 - %d):", total);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_box_append(GTK_BOX(vbox), entry);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    GotoData *gd = g_new(GotoData, 1);
    gd->win = win;
    gd->entry = entry;
    gd->dialog = dialog;

    g_signal_connect(entry, "activate", G_CALLBACK(on_goto_activate), gd);

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_goto_key), gd);
    gtk_widget_add_controller(dialog, key);

    g_object_set_data_full(G_OBJECT(dialog), "goto-data", gd, g_free);

    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(entry);
}

void notes_window_goto_line(NotesWindow *win) {
    search_goto_line_dialog(win);
}
