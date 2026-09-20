#define _GNU_SOURCE
#include "encoding_view.h"
#include "editor_view.h"
#include <adwaita.h>
#include <string.h>

/* ---------------------------------------------------------------- state */

void notes_encoding_free(NotesWindow *win) {
    g_free(win->enc_spans);
    win->enc_spans = NULL;
    win->enc_span_count = 0;
    win->enc_span_current = -1;
    win->enc_nonascii_total = 0;
    win->enc_invisible_total = 0;
}

void notes_encoding_update_status(NotesWindow *win) {
    if (!win->status_encoding) return;

    /* The status bar stays short: just "mixed" — the code pages, byte split
       and stretch count live in the Encoding Info window. */
    const char *enc = win->encoding_mixed ? "mixed"
                    : (win->file_encoding[0] ? win->file_encoding : "UTF-8");
    /* While the highlight is on, the count goes in the status bar: the
       marks themselves are easy to scroll past, a number is not. */
    char marks[96];
    marks[0] = '\0';
    if (win->encoding_marks_shown) {
        if (win->enc_nonascii_total == 0)
            snprintf(marks, sizeof(marks), " | no non-ASCII");
        else if (win->enc_invisible_total > 0)
            snprintf(marks, sizeof(marks), " | %d non-ASCII (%d invisible)",
                     win->enc_nonascii_total, win->enc_invisible_total);
        else
            snprintf(marks, sizeof(marks), " | %d non-ASCII",
                     win->enc_nonascii_total);
    }

    char status[384];

    if (win->encoding_converted)
        snprintf(status, sizeof(status), "%s \xE2\x86\x92 UTF-8 | %s | %s%s%s%s",
                 enc, win->is_binary ? "BIN" : "TEXT",
                 win->file_eol[0] ? win->file_eol : "LF",
                 win->status_extra[0] ? " | " : "", win->status_extra, marks);
    else
        snprintf(status, sizeof(status), "%s | %s | %s%s%s%s",
                 enc, win->is_binary ? "BIN" : "TEXT",
                 win->file_eol[0] ? win->file_eol : "LF",
                 win->status_extra[0] ? " | " : "", win->status_extra, marks);

    gtk_label_set_text(win->status_encoding, status);
}

void notes_encoding_mark_utf8(NotesWindow *win) {
    snprintf(win->file_encoding, sizeof(win->file_encoding), "UTF-8");
    win->encoding_converted     = FALSE;
    win->encoding_confidence    = 100;
    win->encoding_mixed         = FALSE;
    win->encoding_utf8_bytes    = 0;
    win->encoding_foreign_bytes = 0;
    win->encoding_foreign_runs  = 0;
    notes_encoding_update_status(win);
}

void notes_encoding_set_detected(NotesWindow *win,
                                 const NotesEncodingInfo *info,
                                 const char *utf8, gsize utf8_len,
                                 const char *extra) {
    snprintf(win->file_encoding, sizeof(win->file_encoding), "%s", info->name);
    snprintf(win->file_eol, sizeof(win->file_eol), "%s",
             encoding_line_endings(utf8, utf8_len));
    snprintf(win->status_extra, sizeof(win->status_extra), "%s", extra ? extra : "");
    win->encoding_converted  = !info->is_utf8 || info->has_bom;
    win->encoding_confidence = info->confidence;
    win->encoding_mixed          = info->mixed;
    win->encoding_utf8_bytes     = info->utf8_bytes;
    win->encoding_foreign_bytes  = info->foreign_bytes;
    win->encoding_foreign_runs   = info->foreign_runs;

    notes_encoding_free(win);
    notes_encoding_refresh_marks(win);
    notes_encoding_update_status(win);
}

static void encoding_clear_tag(NotesWindow *win) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);
    for (int cls = 1; cls < NOTES_CHAR_CLASS_COUNT; cls++)
        if (win->encoding_tags[cls])
            gtk_text_buffer_remove_tag(win->buffer, win->encoding_tags[cls], &s, &e);
}

void notes_encoding_reset(NotesWindow *win) {
    snprintf(win->file_eol, sizeof(win->file_eol), "LF");
    win->status_extra[0] = '\0';
    notes_encoding_mark_utf8(win);
    notes_encoding_free(win);
    encoding_clear_tag(win);
    notes_encoding_update_status(win);
}

/* ------------------------------------------------------------ highlight */

void notes_encoding_refresh_marks(NotesWindow *win) {
    encoding_clear_tag(win);
    notes_encoding_free(win);
    if (!win->encoding_marks_shown) {
        if (win->scrollbar_overlay) gtk_widget_queue_draw(win->scrollbar_overlay);
        notes_encoding_update_status(win);
        return;
    }

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);
    char *text = gtk_text_buffer_get_text(win->buffer, &s, &e, FALSE);
    GArray *spans = encoding_scan_nonascii(text, strlen(text));
    g_free(text);

    win->enc_span_count   = (int)spans->len;
    win->enc_spans        = (NotesEncodingSpan *)g_array_free(spans, FALSE);
    win->enc_span_current = -1;

    win->enc_nonascii_total = 0;
    win->enc_invisible_total = 0;
    for (int i = 0; i < win->enc_span_count; i++) {
        int n = win->enc_spans[i].nonascii;   /* not end - start: a combining
                                                 run also covers its base */
        win->enc_nonascii_total += n;
        if (win->enc_spans[i].cls == NOTES_CHAR_INVISIBLE)
            win->enc_invisible_total += n;
    }

    for (int i = 0; i < win->enc_span_count; i++) {
        GtkTextIter a, b;
        gtk_text_buffer_get_iter_at_offset(win->buffer, &a, win->enc_spans[i].start);
        gtk_text_buffer_get_iter_at_offset(win->buffer, &b, win->enc_spans[i].end);
        int cls = win->enc_spans[i].cls;
        if (cls > 0 && cls < NOTES_CHAR_CLASS_COUNT && win->encoding_tags[cls])
            gtk_text_buffer_apply_tag(win->buffer, win->encoding_tags[cls], &a, &b);
    }

    if (win->scrollbar_overlay) gtk_widget_queue_draw(win->scrollbar_overlay);
    notes_encoding_update_status(win);
}

static gboolean encoding_marks_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->enc_marks_idle_id = 0;
    notes_encoding_refresh_marks(win);
    return G_SOURCE_REMOVE;
}

/* Rapid typing coalesces into a single rescan on idle — the whole buffer is
   scanned, so doing it per keystroke would cost O(n) each time. */
void notes_encoding_schedule_refresh(NotesWindow *win) {
    if (!win->encoding_marks_shown) return;
    if (win->enc_marks_idle_id) return;
    win->enc_marks_idle_id = g_idle_add(encoding_marks_idle_cb, win);
}

static void encoding_goto_span(NotesWindow *win, int index) {
    if (index < 0 || index >= win->enc_span_count) return;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_offset(win->buffer, &it, win->enc_spans[index].start);
    gtk_text_buffer_place_cursor(win->buffer, &it);
    gtk_text_view_scroll_to_iter(win->text_view, &it, 0.2, FALSE, 0.0, 0.0);
    gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
    editor_view_update_cursor_position(win);
    editor_view_update_line_highlights(win);
}

/* -------------------------------------------------------------- actions */

void on_encoding_highlight(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)param;
    NotesWindow *win = data;
    GVariant *state = g_action_get_state(G_ACTION(action));
    gboolean on = !g_variant_get_boolean(state);
    g_variant_unref(state);
    g_simple_action_set_state(action, g_variant_new_boolean(on));

    win->encoding_marks_shown = on;
    notes_encoding_refresh_marks(win);
}

void on_encoding_next(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    if (!win->encoding_marks_shown) {
        /* Scan on demand even when the highlight is off. */
        win->encoding_marks_shown = TRUE;
        notes_encoding_refresh_marks(win);
        GAction *a = g_action_map_lookup_action(G_ACTION_MAP(win->window),
                                                "encoding-highlight");
        if (a) g_simple_action_set_state(G_SIMPLE_ACTION(a),
                                         g_variant_new_boolean(TRUE));
    }
    if (win->enc_span_count == 0) return;

    win->enc_span_current = (win->enc_span_current + 1) % win->enc_span_count;
    encoding_goto_span(win, win->enc_span_current);
}

/* Swap the whole buffer for `text`, keeping the cursor line/column and
   marking the document dirty. */
static void encoding_replace_buffer(NotesWindow *win, const char *text) {
    GtkTextMark *insert = gtk_text_buffer_get_insert(win->buffer);
    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(win->buffer, &cur, insert);
    int line = gtk_text_iter_get_line(&cur);
    int col  = gtk_text_iter_get_line_offset(&cur);

    editor_view_block_signals(win);
    gtk_text_buffer_set_text(win->buffer, text, -1);
    editor_view_unblock_signals(win);

    int lines = gtk_text_buffer_get_line_count(win->buffer);
    if (line >= lines) line = lines - 1;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(win->buffer, &it, line);
    int line_chars = gtk_text_iter_get_chars_in_line(&it);
    if (col > line_chars - 1) col = line_chars > 0 ? line_chars - 1 : 0;
    gtk_text_buffer_get_iter_at_line_offset(win->buffer, &it, line, col);
    gtk_text_buffer_place_cursor(win->buffer, &it);

    win->dirty = TRUE;
    win->is_binary = FALSE;

    char *base = win->current_file[0]
               ? g_path_get_basename(win->current_file) : g_strdup("Notes Light");
    char title[600];
    snprintf(title, sizeof(title), "*%s", base);
    gtk_window_set_title(GTK_WINDOW(win->window), title);
    g_free(base);

    notes_encoding_refresh_marks(win);
    editor_view_update_cursor_position(win);
    if (win->settings.show_line_numbers)
        editor_view_update_line_numbers(win);
    editor_view_update_line_highlights(win);
    editor_view_apply_font_intensity(win);
}

static char *encoding_buffer_text(NotesWindow *win) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);
    return gtk_text_buffer_get_text(win->buffer, &s, &e, FALSE);
}

/* Plain notification window.

   GtkAlertDialog was the obvious choice, but it holds its object and its
   strings until the user answers: closing the window without pressing the
   button leaks them (measured under ASan), and it also ignores the app's
   AdwHeaderBar theming that every other dialog here uses. A small window we
   own frees deterministically when it is closed. */
static void on_notice_close(GtkButton *btn, gpointer data) {
    (void)btn;
    gtk_window_destroy(GTK_WINDOW(data));
}

static gboolean on_notice_key(GtkEventControllerKey *ctrl, guint keyval,
                              guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state;
    if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_Return ||
        keyval == GDK_KEY_KP_Enter) {
        gtk_window_destroy(GTK_WINDOW(data));
        return TRUE;
    }
    return FALSE;
}

static void encoding_message(NotesWindow *win, const char *title, const char *detail) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);

    GtkWidget *head = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(head), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(head), 0.0);
    gtk_label_set_wrap(GTK_LABEL(head), TRUE);
    gtk_box_append(GTK_BOX(vbox), head);

    if (detail && detail[0]) {
        GtkWidget *body = gtk_label_new(detail);
        gtk_label_set_xalign(GTK_LABEL(body), 0.0);
        gtk_label_set_wrap(GTK_LABEL(body), TRUE);
        gtk_label_set_selectable(GTK_LABEL(body), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(body), 68);
        gtk_box_append(GTK_BOX(vbox), body);
    }

    GtkWidget *close_btn = gtk_button_new_with_label("Close");
    gtk_widget_add_css_class(close_btn, "suggested-action");
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_notice_close), dialog);
    gtk_box_append(GTK_BOX(vbox), close_btn);

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_notice_key), dialog);
    gtk_widget_add_controller(dialog, key);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(close_btn);
}

/* Convert the whole document to plain UTF-8: whatever the file was decoded
   from, the buffer is re-emitted NFC-normalized and without a BOM, then
   marked dirty so the next save writes it out as UTF-8. */
void on_encoding_convert(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    char *text = encoding_buffer_text(win);
    char *norm = g_utf8_normalize(text, -1, G_NORMALIZE_NFC);
    if (!norm) norm = g_strdup(text);

    /* A stray U+FEFF anywhere in the text is never wanted in UTF-8. */
    int stripped = 0;
    GString *clean = g_string_sized_new(strlen(norm));
    for (const char *p = norm; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c == 0xFEFF) { stripped++; continue; }
        g_string_append_unichar(clean, c);
    }
    char *final_text = g_string_free(clean, FALSE);

    gboolean text_changed = !g_str_equal(final_text, text);
    /* Copy, not alias: win->file_encoding is overwritten a few lines down. */
    char old_enc[sizeof(win->file_encoding)];
    snprintf(old_enc, sizeof(old_enc), "%s",
             win->file_encoding[0] ? win->file_encoding : "UTF-8");
    gboolean was_other = win->encoding_converted;

    if (text_changed) encoding_replace_buffer(win, final_text);
    else if (was_other) win->dirty = TRUE;

    gboolean was_mixed  = win->encoding_mixed;
    gsize    mixed_fb   = win->encoding_foreign_bytes;
    int      mixed_runs = win->encoding_foreign_runs;
    notes_encoding_mark_utf8(win);

    /* Verify the result and say exactly what the document now holds. */
    NotesEncodingReport rep;
    encoding_report(final_text, &rep);

    GString *d = g_string_new(NULL);
    if (rep.valid_utf8)
        g_string_append(d,
            "The whole document is now UTF-8 \xE2\x80\x94 there is no other\n"
            "encoding left in it.\n\n");
    else
        g_string_append(d,
            "WARNING: the document still contains byte sequences that are not\n"
            "valid UTF-8. Save As and re-open the file to inspect it.\n\n");

    g_string_append_printf(d, "Source encoding on load: %s\n", old_enc);
    if (was_mixed)
        g_string_append_printf(d,
            "The file on disk mixed two encodings: %d stretch%s (%lu bytes)\n"
            "were not UTF-8 and were decoded separately. After saving, the\n"
            "file holds one encoding only.\n",
            mixed_runs, mixed_runs == 1 ? "" : "es", (unsigned long)mixed_fb);
    if (text_changed || was_other) {
        g_string_append(d, "Applied: ");
        g_string_append(d, "Unicode normalization (NFC)");
        if (stripped)
            g_string_append_printf(d, ", removed %d BOM character%s",
                                   stripped, stripped == 1 ? "" : "s");
        g_string_append_c(d, '\n');
    } else {
        g_string_append(d, "Applied: nothing \xE2\x80\x94 it was already NFC UTF-8 without a BOM\n");
    }

    g_string_append_printf(d,
        "\nWhat the document contains now\n"
        "  %ld characters, all encoded as UTF-8\n"
        "  %ld ASCII characters (1 byte each)\n"
        "  %ld non-ASCII characters (2-%d bytes each)\n"
        "  Line endings: %s\n",
        rep.total_chars, rep.ascii_chars, rep.multibyte_chars,
        rep.max_bytes_per_char < 2 ? 2 : rep.max_bytes_per_char,
        win->file_eol[0] ? win->file_eol : "LF");

    /* Non-ASCII is still UTF-8 — say so, and point at what is worth cleaning. */
    if (rep.multibyte_chars > 0)
        g_string_append(d,
            "\nThe non-ASCII characters (accents, quotes, other scripts, emoji)\n"
            "are UTF-8 too \xE2\x80\x94 UTF-8 encodes them in 2-4 bytes. The highlight\n"
            "colors show what kind of character each one is, not a different\n"
            "encoding.\n");

    if (rep.replacement_chars || rep.invisible_chars || rep.lookalike_chars ||
        g_str_equal(win->file_eol, "mixed")) {
        g_string_append(d, "\nWorth a look\n");
        if (rep.replacement_chars)
            g_string_append_printf(d,
                "  %ld replacement character%s (U+FFFD) \xE2\x80\x94 those bytes could not\n"
                "    be decoded from %s; the original data is lost there\n",
                rep.replacement_chars, rep.replacement_chars == 1 ? "" : "s", old_enc);
        if (rep.invisible_chars)
            g_string_append_printf(d,
                "  %ld invisible character%s (zero-width, soft hyphen)\n"
                "    \xE2\x86\x92 Replace Lookalikes with ASCII removes them\n",
                rep.invisible_chars, rep.invisible_chars == 1 ? "" : "s");
        if (rep.lookalike_chars)
            g_string_append_printf(d,
                "  %ld typographic lookalike%s (curly quotes, dashes, NBSP)\n"
                "    \xE2\x86\x92 Replace Lookalikes with ASCII folds them to ASCII\n",
                rep.lookalike_chars, rep.lookalike_chars == 1 ? "" : "s");
        if (g_str_equal(win->file_eol, "mixed"))
            g_string_append(d, "  Mixed line endings (CRLF and LF in the same file)\n");
    }

    if (text_changed || was_other)
        g_string_append(d, "\nSave the document to write the file as UTF-8 without a BOM.");

    encoding_message(win, rep.valid_utf8 ? "Converted to UTF-8"
                                         : "Conversion incomplete", d->str);
    g_string_free(d, TRUE);

    g_free(final_text);
    g_free(norm);
    g_free(text);
}

/* Replace typographic lookalikes (curly quotes, dashes, NBSP, ellipsis) and
   drop invisible characters, so the text becomes pure ASCII wherever an
   ASCII equivalent exists. */
void on_encoding_asciify(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    char *text = encoding_buffer_text(win);
    int replaced = 0;
    char *clean = encoding_asciify_punctuation(text, &replaced);

    char detail[256];
    if (replaced == 0) {
        snprintf(detail, sizeof(detail),
                 "No typographic lookalikes or invisible characters found.");
        encoding_message(win, "Replace Lookalikes with ASCII", detail);
    } else {
        encoding_replace_buffer(win, clean);
        snprintf(detail, sizeof(detail),
                 "Replaced %d character%s (quotes, dashes, no-break spaces,\n"
                 "ellipses, zero-width and soft-hyphen characters).",
                 replaced, replaced == 1 ? "" : "s");
        encoding_message(win, "Replaced Lookalikes with ASCII", detail);
    }

    g_free(clean);
    g_free(text);
}


/* ------------------------------------------------- encoding picker dialog */

typedef void (*EncodingPickFn)(NotesWindow *win, const NotesCharsetChoice *c);

typedef struct {
    NotesWindow    *win;
    EncodingPickFn  fn;
    GtkWidget      *dialog;
} PickCtx;

static void on_pick_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    PickCtx *ctx = data;
    const NotesCharsetChoice *c = row ? g_object_get_data(G_OBJECT(row), "choice") : NULL;
    GtkWidget *dialog = ctx->dialog;
    NotesWindow *win = ctx->win;
    EncodingPickFn fn = ctx->fn;

    /* Disconnect before destroying: the dialog owns `ctx` and frees it, so a
       second "row-activated" (double click, or Enter arriving right after a
       click) would otherwise run this handler on freed memory. */
    g_signal_handlers_disconnect_by_func(box, on_pick_row_activated, ctx);

    gtk_window_destroy(GTK_WINDOW(dialog));
    if (c && fn) fn(win, c);
}

static void encoding_pick_dialog(NotesWindow *win, const char *title,
                                 const char *subtitle, gboolean include_auto,
                                 EncodingPickFn fn) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 480);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);

    GtkWidget *sub = gtk_label_new(subtitle);
    gtk_label_set_wrap(GTK_LABEL(sub), TRUE);
    gtk_label_set_xalign(GTK_LABEL(sub), 0.0);
    gtk_widget_add_css_class(sub, "dim-label");
    gtk_box_append(GTK_BOX(vbox), sub);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "boxed-list");

    PickCtx *ctx = g_new0(PickCtx, 1);
    ctx->win = win;
    ctx->fn = fn;
    ctx->dialog = dialog;
    g_object_set_data_full(G_OBJECT(dialog), "pick-ctx", ctx, g_free);
    g_signal_connect(list, "row-activated", G_CALLBACK(on_pick_row_activated), ctx);

    int count = 0;
    const NotesCharsetChoice *all = encoding_charset_list(&count);
    for (int i = 0; i < count; i++) {
        if (!include_auto && all[i].charset[0] == '\0') continue;

        GtkWidget *lbl = gtk_label_new(all[i].label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_widget_set_margin_start(lbl, 10);
        gtk_widget_set_margin_end(lbl, 10);
        gtk_widget_set_margin_top(lbl, 8);
        gtk_widget_set_margin_bottom(lbl, 8);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        g_object_set_data(G_OBJECT(row), "choice", (gpointer)&all[i]);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
    gtk_box_append(GTK_BOX(vbox), scroller);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ------------------------------------------- 1. reload with an encoding */

static void reload_with_choice(NotesWindow *win, const NotesCharsetChoice *c) {
    if (win->current_file[0] == '\0') return;

    /* Keep the caret where it was, and never hand load_file_as a pointer
       into the very buffer it writes to. */
    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(win->buffer, &cur,
                                     gtk_text_buffer_get_insert(win->buffer));
    int line = gtk_text_iter_get_line(&cur);
    int col  = gtk_text_iter_get_line_offset(&cur);

    char path[2048];
    g_strlcpy(path, win->current_file, sizeof(path));
    notes_window_load_file_as(win, path, c->charset[0] ? c->charset : NULL);

    int total = gtk_text_buffer_get_line_count(win->buffer);
    if (line >= total) line = total - 1;
    if (line < 0) line = 0;
    GtkTextIter restore;
    gtk_text_buffer_get_iter_at_line(win->buffer, &restore, line);
    int chars = gtk_text_iter_get_chars_in_line(&restore);
    if (col > chars - 1) col = chars > 0 ? chars - 1 : 0;
    gtk_text_buffer_get_iter_at_line_offset(win->buffer, &restore, line, col);
    gtk_text_buffer_place_cursor(win->buffer, &restore);
    gtk_text_view_scroll_to_iter(win->text_view, &restore, 0.2, FALSE, 0.0, 0.0);

    notes_encoding_refresh_marks(win);
    editor_view_update_cursor_position(win);
    editor_view_update_line_highlights(win);
}

static void on_reload_confirm(GObject *src, GAsyncResult *res, gpointer data) {
    NotesWindow *win = data;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, NULL);
    if (choice != 0) return;   /* 0 = Discard and reload */
    encoding_pick_dialog(win, "Reload with Encoding",
        "The file is read again from disk and decoded with the encoding you "
        "pick. Use this when the automatic guess got it wrong.",
        TRUE, reload_with_choice);
}

void on_encoding_reload_as(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    if (win->current_file[0] == '\0') {
        encoding_message(win, "Reload with Encoding",
                         "This document has no file on disk yet.");
        return;
    }
    if (notes_window_is_remote(win)) {
        encoding_message(win, "Reload with Encoding",
                         "Only local files can be reloaded with a different "
                         "encoding for now.");
        return;
    }

    if (win->dirty) {
        GtkAlertDialog *dlg = gtk_alert_dialog_new(
            "Reloading discards your unsaved changes.");
        const char *buttons[] = {"Discard and Reload", "Cancel", NULL};
        gtk_alert_dialog_set_buttons(dlg, buttons);
        gtk_alert_dialog_set_cancel_button(dlg, 1);
        gtk_alert_dialog_set_default_button(dlg, 1);
        gtk_alert_dialog_set_modal(dlg, TRUE);
        gtk_alert_dialog_choose(dlg, GTK_WINDOW(win->window), NULL,
                                on_reload_confirm, win);
        g_object_unref(dlg);
        return;
    }

    encoding_pick_dialog(win, "Reload with Encoding",
        "The file is read again from disk and decoded with the encoding you "
        "pick. Use this when the automatic guess got it wrong.",
        TRUE, reload_with_choice);
}

/* --------------------------------------------- 2. save in a chosen encoding */

typedef struct {
    NotesWindow             *win;
    const NotesCharsetChoice *choice;
} SaveAsCtx;

static void on_save_encoded_ready(GObject *src, GAsyncResult *res, gpointer data) {
    SaveAsCtx *ctx = data;
    NotesWindow *win = ctx->win;
    const NotesCharsetChoice *c = ctx->choice;
    g_free(ctx);

    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!file) return;

    char *path = g_file_get_path(file);
    g_object_unref(file);
    if (!path) return;

    char *text = encoding_buffer_text(win);
    GError *err = NULL;
    gsize out_len = 0;
    char *bytes = encoding_from_utf8(text, c->charset, c->bom, &out_len, &err);

    if (!bytes) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "The document cannot be written as %s:\n%s\n\n"
                 "That encoding has no room for some of the characters in the "
                 "text. Save it as UTF-8, or remove those characters first.",
                 c->label, err ? err->message : "conversion failed");
        encoding_message(win, "Cannot save in this encoding", msg);
        g_clear_error(&err);
        g_free(text);
        g_free(path);
        return;
    }

    GError *werr = NULL;
    gboolean ok = g_file_set_contents(path, bytes, (gssize)out_len, &werr);

    char msg[512];
    if (ok) {
        snprintf(msg, sizeof(msg),
                 "Saved %lu bytes as %s.\n\n"
                 "The document in the editor stays UTF-8 — only the file on "
                 "disk uses %s.",
                 (unsigned long)out_len, c->label, c->charset);
        encoding_message(win, "Saved with encoding", msg);
    } else {
        snprintf(msg, sizeof(msg), "%s", werr ? werr->message : "write failed");
        encoding_message(win, "Save failed", msg);
        g_clear_error(&werr);
    }

    g_free(bytes);
    g_free(text);
    g_free(path);
}

static void save_with_choice(NotesWindow *win, const NotesCharsetChoice *c) {
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save As with Encoding");
    if (win->current_file[0] != '\0') {
        char *base = g_path_get_basename(win->current_file);
        gtk_file_dialog_set_initial_name(dialog, base);
        g_free(base);
    }
    SaveAsCtx *ctx = g_new0(SaveAsCtx, 1);
    ctx->win = win;
    ctx->choice = c;
    gtk_file_dialog_save(dialog, GTK_WINDOW(win->window), NULL,
                         on_save_encoded_ready, ctx);
    g_object_unref(dialog);
}

void on_encoding_save_as(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    encoding_pick_dialog((NotesWindow *)data, "Save As with Encoding",
        "Pick the encoding for the file on disk. The document in the editor "
        "stays UTF-8. Characters the encoding cannot represent will stop the "
        "save with an error rather than being replaced silently.",
        FALSE, save_with_choice);
}

/* ------------------------------------------------- 5. mojibake repair */

void on_encoding_fix_mojibake(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    char *text = encoding_buffer_text(win);
    int fixed = 0;
    const char *via = NULL;
    char *repaired = encoding_fix_mojibake(text, &fixed, &via);

    if (!repaired || g_str_equal(repaired, text)) {
        encoding_message(win, "Fix Mojibake",
            "No mojibake found.\n\n"
            "This repairs text that was already UTF-8 but got read once as a "
            "single-byte code page, which turns \"Príliš\" into \"PrÃ­liÅ¡\". "
            "The text in this document does not look like that.");
    } else {
        encoding_replace_buffer(win, repaired);
        char msg[384];
        snprintf(msg, sizeof(msg),
                 "Recovered %d character%s that had been read as %s.\n\n"
                 "Check a few spots before saving — the repair is only "
                 "applied when the result is valid UTF-8 throughout.",
                 fixed, fixed == 1 ? "" : "s", via ? via : "a code page");
        encoding_message(win, "Mojibake repaired", msg);
    }

    g_free(repaired);
    g_free(text);
}

/* --------------------------------------------------- 6. line endings */

void on_encoding_eol(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)param;
    NotesWindow *win = data;
    const char *name = g_action_get_name(G_ACTION(action));
    const char *want = g_str_has_suffix(name, "crlf") ? "CRLF" : "LF";

    char *text = encoding_buffer_text(win);
    int changed = 0;
    char *converted = encoding_convert_eol(text, want, &changed);

    if (changed == 0 && g_str_equal(text, converted)) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "Every line already ends with %s.", want);
        encoding_message(win, "Line Endings", msg);
    } else {
        encoding_replace_buffer(win, converted);
        snprintf(win->file_eol, sizeof(win->file_eol), "%s", want);
        notes_encoding_update_status(win);
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "Converted %d line ending%s to %s.\nSave to write it to the file.",
                 changed, changed == 1 ? "" : "s", want);
        encoding_message(win, "Line Endings", msg);
    }

    g_free(converted);
    g_free(text);
}

/* ------------------------------------------ 4. navigate one class only */

void on_encoding_next_class(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action;
    NotesWindow *win = data;
    int want = param ? g_variant_get_int32(param) : 0;

    if (!win->encoding_marks_shown) {
        win->encoding_marks_shown = TRUE;
        notes_encoding_refresh_marks(win);
        GAction *a = g_action_map_lookup_action(G_ACTION_MAP(win->window),
                                                "encoding-highlight");
        if (a) g_simple_action_set_state(G_SIMPLE_ACTION(a),
                                         g_variant_new_boolean(TRUE));
    }

    int start = win->enc_span_current;
    for (int step = 1; step <= win->enc_span_count; step++) {
        int idx = (start + step) % win->enc_span_count;
        if (win->enc_spans[idx].cls == want) {
            win->enc_span_current = idx;
            encoding_goto_span(win, idx);
            return;
        }
    }

    char msg[160];
    snprintf(msg, sizeof(msg), "No %s characters in this document.",
             encoding_class_name(want));
    encoding_message(win, "Next by kind", msg);
}

/* ------------------------------------------------- Encoding Info window */

typedef struct {
    NotesWindow *win;
    int          offset;      /* character offset to jump to */
} InfoJump;

static void on_info_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    NotesWindow *win = data;
    InfoJump *j = g_object_get_data(G_OBJECT(row), "jump");
    if (!j) return;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_offset(win->buffer, &it, j->offset);
    gtk_text_buffer_place_cursor(win->buffer, &it);
    gtk_text_view_scroll_to_iter(win->text_view, &it, 0.25, FALSE, 0.0, 0.0);
    editor_view_update_cursor_position(win);
    editor_view_update_line_highlights(win);
}

static GtkWidget *info_section_label(const char *text) {
    GtkWidget *l = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(l), markup);
    g_free(markup);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_margin_top(l, 10);
    return l;
}

static void info_add_pair(GtkGrid *grid, int row, const char *key, const char *value) {
    GtkWidget *k = gtk_label_new(key);
    gtk_widget_set_halign(k, GTK_ALIGN_START);
    gtk_widget_set_valign(k, GTK_ALIGN_START);
    gtk_widget_add_css_class(k, "dim-label");
    GtkWidget *v = gtk_label_new(value);
    gtk_widget_set_halign(v, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(v), TRUE);
    gtk_label_set_xalign(GTK_LABEL(v), 0.0);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);
    gtk_grid_attach(grid, k, 0, row, 1, 1);
    gtk_grid_attach(grid, v, 1, row, 1, 1);
}

/* A colored chip showing the tag color used for a character class. */
static GtkWidget *info_color_chip(NotesCharClass cls) {
    GtkWidget *l = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped(
        "<span background=\"%s\" foreground=\"#ffffff\"><tt>  %s  </tt></span>",
        encoding_class_color(cls), encoding_class_color(cls));
    gtk_label_set_markup(GTK_LABEL(l), markup);
    g_free(markup);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_valign(l, GTK_ALIGN_START);
    return l;
}

void on_encoding_info(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    char *text = encoding_buffer_text(win);
    gsize byte_len = strlen(text);
    GArray *spans = encoding_scan_nonascii(text, byte_len);

    int per_class[NOTES_CHAR_CLASS_COUNT];
    int runs_class[NOTES_CHAR_CLASS_COUNT];
    memset(per_class, 0, sizeof(per_class));
    memset(runs_class, 0, sizeof(runs_class));
    int nonascii = 0;
    for (guint i = 0; i < spans->len; i++) {
        NotesEncodingSpan *sp = &g_array_index(spans, NotesEncodingSpan, i);
        int n = sp->nonascii;
        if (sp->cls > 0 && sp->cls < NOTES_CHAR_CLASS_COUNT) {
            per_class[sp->cls] += n;
            runs_class[sp->cls]++;
        }
        nonascii += n;
    }
    long total_chars = g_utf8_strlen(text, (gssize)byte_len);
    long ascii_chars = total_chars - nonascii;

    /* ---- window ---- */
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Encoding Info");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 620, 620);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 16);

    /* ---- file ---- */
    gtk_box_append(GTK_BOX(vbox), info_section_label("File"));
    GtkWidget *fgrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(fgrid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(fgrid), 4);
    int r = 0;
    info_add_pair(GTK_GRID(fgrid), r++, "Path",
                  win->current_file[0] ? win->current_file : "(not saved yet)");
    info_add_pair(GTK_GRID(fgrid), r++, "Location",
                  notes_window_is_remote(win) ? "remote (SFTP)" : "local");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s%s%s",
             win->is_binary ? "binary (read-only)" : "text",
             win->is_truncated ? ", truncated to 5 MB" : "",
             win->dirty ? ", unsaved changes" : "");
    info_add_pair(GTK_GRID(fgrid), r++, "State", buf);
    gtk_box_append(GTK_BOX(vbox), fgrid);

    /* ---- encoding ---- */
    gtk_box_append(GTK_BOX(vbox), info_section_label("Encoding"));
    GtkWidget *egrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(egrid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(egrid), 4);
    r = 0;
    info_add_pair(GTK_GRID(egrid), r++, "Detected on load",
                  win->file_encoding[0] ? win->file_encoding : "UTF-8");
    snprintf(buf, sizeof(buf), "%d %%%s", win->encoding_confidence,
             win->encoding_confidence == 100 ? " (BOM or exact match)" : " (heuristic)");
    info_add_pair(GTK_GRID(egrid), r++, "Confidence", buf);
    if (win->encoding_mixed) {
        snprintf(buf, sizeof(buf),
                 "yes — %lu bytes were UTF-8, %lu bytes in %d stretch%s came "
                 "from %s and were decoded separately",
                 (unsigned long)win->encoding_utf8_bytes,
                 (unsigned long)win->encoding_foreign_bytes,
                 win->encoding_foreign_runs,
                 win->encoding_foreign_runs == 1 ? "" : "es",
                 win->file_encoding);
        info_add_pair(GTK_GRID(egrid), r++, "Mixed source", buf);
    }
    info_add_pair(GTK_GRID(egrid), r++, "In the editor",
                  win->encoding_converted ? "UTF-8 (converted on load)" : "UTF-8");
    info_add_pair(GTK_GRID(egrid), r++, "On save",
                  "UTF-8 without BOM");
    info_add_pair(GTK_GRID(egrid), r++, "Line endings",
                  win->file_eol[0] ? win->file_eol : "LF");
    snprintf(buf, sizeof(buf), "%ld characters, %d lines, %lu bytes as UTF-8",
             total_chars, gtk_text_buffer_get_line_count(win->buffer),
             (unsigned long)byte_len);
    info_add_pair(GTK_GRID(egrid), r++, "Size", buf);
    gtk_box_append(GTK_BOX(vbox), egrid);

    /* ---- character classes / color legend ---- */
    gtk_box_append(GTK_BOX(vbox), info_section_label("Character classes and colors"));
    GtkWidget *hint = gtk_label_new(
        "Every character below is UTF-8 \xE2\x80\x94 the classes say what kind of "
        "character it is, not what encoding it is in. Colors are the ones used by "
        "View \xE2\x86\x92 Highlight Non-ASCII Text.");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_append(GTK_BOX(vbox), hint);

    GtkWidget *cgrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(cgrid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(cgrid), 8);
    gtk_widget_set_margin_top(cgrid, 6);
    int row = 0;
    for (int cls = 0; cls < NOTES_CHAR_CLASS_COUNT; cls++) {
        GtkWidget *chip;
        if (cls == NOTES_CHAR_ASCII) {
            chip = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(chip),
                "<span foreground=\"#7f8c8d\"><tt>not highlighted</tt></span>");
            gtk_widget_set_halign(chip, GTK_ALIGN_START);
            gtk_widget_set_valign(chip, GTK_ALIGN_START);
        } else {
            chip = info_color_chip(cls);
        }
        gtk_grid_attach(GTK_GRID(cgrid), chip, 0, row, 1, 1);

        int count = cls == NOTES_CHAR_ASCII ? (int)ascii_chars : per_class[cls];
        char *markup;
        if (cls == NOTES_CHAR_ASCII)
            markup = g_markup_printf_escaped(
                "<b>%s</b> \xE2\x80\x94 %d characters\n<small>%s\nExamples: %s</small>",
                encoding_class_name(cls), count,
                encoding_class_description(cls), encoding_class_examples(cls));
        else
            markup = g_markup_printf_escaped(
                "<b>%s</b> \xE2\x80\x94 %d characters in %d run%s\n<small>%s\nExamples: %s</small>",
                encoding_class_name(cls), count, runs_class[cls],
                runs_class[cls] == 1 ? "" : "s",
                encoding_class_description(cls), encoding_class_examples(cls));

        GtkWidget *desc = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(desc), markup);
        g_free(markup);
        gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
        gtk_label_set_xalign(GTK_LABEL(desc), 0.0);
        gtk_widget_set_halign(desc, GTK_ALIGN_START);
        gtk_widget_set_hexpand(desc, TRUE);
        if (count == 0) gtk_widget_set_opacity(desc, 0.55);
        gtk_grid_attach(GTK_GRID(cgrid), desc, 1, row, 1, 1);
        row++;
    }
    gtk_box_append(GTK_BOX(vbox), cgrid);

    /* ---- occurrence list ---- */
    gtk_box_append(GTK_BOX(vbox), info_section_label("Non-ASCII characters"));
    if (spans->len == 0) {
        GtkWidget *none = gtk_label_new("This document is pure ASCII.");
        gtk_widget_set_halign(none, GTK_ALIGN_START);
        gtk_widget_add_css_class(none, "dim-label");
        gtk_box_append(GTK_BOX(vbox), none);
    } else {
        GtkWidget *note = gtk_label_new("Click a row to jump to it in the text.");
        gtk_widget_set_halign(note, GTK_ALIGN_START);
        gtk_widget_add_css_class(note, "dim-label");
        gtk_box_append(GTK_BOX(vbox), note);

        GtkWidget *list = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
        gtk_widget_add_css_class(list, "boxed-list");
        gtk_widget_set_margin_top(list, 6);
        g_signal_connect(list, "row-activated",
                         G_CALLBACK(on_info_row_activated), win);

        const int MAX_ROWS = 200;
        int shown = 0;
        for (guint i = 0; i < spans->len && shown < MAX_ROWS; i++) {
            NotesEncodingSpan *sp = &g_array_index(spans, NotesEncodingSpan, i);
            for (int off = sp->start; off < sp->end && shown < MAX_ROWS; off++) {
                GtkTextIter a;
                gtk_text_buffer_get_iter_at_offset(win->buffer, &a, off);
                gunichar c = gtk_text_iter_get_char(&a);
                if (c < 0x80) continue;   /* base letter of a combining run */
                shown++;
                char utf8_char[8] = {0};
                int n = (int)g_unichar_to_utf8(c, utf8_char);
                utf8_char[n] = '\0';

                char *markup = g_markup_printf_escaped(
                    "<tt>Ln %d, Col %d</tt>   U+%04X   <span background=\"%s\" "
                    "foreground=\"#ffffff\"> %s </span>   <small>%s, %d bytes in UTF-8</small>",
                    gtk_text_iter_get_line(&a) + 1,
                    gtk_text_iter_get_line_offset(&a) + 1,
                    (unsigned)c, encoding_class_color(sp->cls),
                    g_unichar_isgraph(c) ? utf8_char : "\xE2\x90\xA3",
                    encoding_class_name(sp->cls), n);

                GtkWidget *lbl = gtk_label_new(NULL);
                gtk_label_set_markup(GTK_LABEL(lbl), markup);
                g_free(markup);
                gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
                gtk_widget_set_margin_start(lbl, 8);
                gtk_widget_set_margin_end(lbl, 8);
                gtk_widget_set_margin_top(lbl, 6);
                gtk_widget_set_margin_bottom(lbl, 6);

                GtkWidget *lrow = gtk_list_box_row_new();
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(lrow), lbl);
                InfoJump *j = g_new(InfoJump, 1);
                j->win = win;
                j->offset = off;
                g_object_set_data_full(G_OBJECT(lrow), "jump", j, g_free);
                gtk_list_box_append(GTK_LIST_BOX(list), lrow);
            }
        }
        gtk_box_append(GTK_BOX(vbox), list);

        if (nonascii > shown) {
            char more[96];
            snprintf(more, sizeof(more), "... and %d more (listing capped at %d).",
                     nonascii - shown, MAX_ROWS);
            GtkWidget *l = gtk_label_new(more);
            gtk_widget_set_halign(l, GTK_ALIGN_START);
            gtk_widget_add_css_class(l, "dim-label");
            gtk_widget_set_margin_top(l, 6);
            gtk_box_append(GTK_BOX(vbox), l);
        }
    }

    g_array_free(spans, TRUE);
    g_free(text);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), vbox);
    gtk_window_set_child(GTK_WINDOW(dialog), scroller);
    gtk_window_present(GTK_WINDOW(dialog));
}
