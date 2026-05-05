#define _GNU_SOURCE
#include "editor_view.h"
#include "theme.h"
#include "ssh_window.h"
#include <string.h>

struct _NotesTextView {
    GtkSourceView parent;
    NotesWindow *win;
};

G_DEFINE_TYPE(NotesTextView, notes_text_view, GTK_SOURCE_TYPE_VIEW)

static void notes_text_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
    NotesTextView *self = NOTES_TEXT_VIEW(widget);
    NotesWindow *win = self->win;

    GTK_WIDGET_CLASS(notes_text_view_parent_class)->snapshot(widget, snapshot);

    if (win && win->settings.highlight_current_line) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_line(win->buffer, &iter, win->highlight_line);

        int buf_y, line_height;
        gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(widget), &iter,
                                      &buf_y, &line_height);

        int wx, wy;
        gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(widget),
            GTK_TEXT_WINDOW_WIDGET, 0, buf_y, &wx, &wy);

        int view_width = gtk_widget_get_width(widget);
        int h = line_height > 0 ? line_height : win->settings.font_size + 4;
        int extra = (int)((win->settings.line_spacing - 1.0) * win->settings.font_size * 0.5);
        if (extra < 0) extra = 0;

        graphene_rect_t area = GRAPHENE_RECT_INIT(0, wy - extra, view_width, h + extra * 2);
        gtk_snapshot_append_color(snapshot, &win->highlight_rgba, &area);
    }
}

static void notes_text_view_class_init(NotesTextViewClass *klass) {
    GTK_WIDGET_CLASS(klass)->snapshot = notes_text_view_snapshot;
}

static void notes_text_view_init(NotesTextView *self) {
    (void)self;
}

void editor_view_update_line_highlights(NotesWindow *win) {
    GtkTextMark *mark = gtk_text_buffer_get_insert(win->buffer);
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(win->buffer, &cursor, mark);
    win->highlight_line = gtk_text_iter_get_line(&cursor);
    gtk_widget_queue_draw(GTK_WIDGET(win->text_view));
}

void editor_view_apply_highlight_color(NotesWindow *win) {
    if (notes_is_dark_theme(win->settings.theme))
        win->highlight_rgba = (GdkRGBA){1.0, 1.0, 1.0, 0.06};
    else
        win->highlight_rgba = (GdkRGBA){0.0, 0.0, 0.0, 0.06};
}

void editor_view_update_cursor_position(NotesWindow *win) {
    GtkTextIter iter;
    GtkTextMark *mark = gtk_text_buffer_get_insert(win->buffer);
    gtk_text_buffer_get_iter_at_mark(win->buffer, &iter, mark);
    int line = gtk_text_iter_get_line(&iter) + 1;
    int col = gtk_text_iter_get_line_offset(&iter) + 1;
    char buf[64];
    snprintf(buf, sizeof(buf), "Ln %d, Col %d", line, col);
    gtk_label_set_text(win->status_cursor, buf);
}

static gboolean on_text_view_scroll(GtkEventControllerScroll *ctrl,
                                     double dx, double dy, gpointer data) {
    (void)dx;
    GdkEvent *evt = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(ctrl));
    GdkModifierType state = evt ? gdk_event_get_modifier_state(evt) : 0;
    if (!(state & GDK_CONTROL_MASK)) return FALSE;

    NotesWindow *win = data;
    if (dy < 0 && win->settings.font_size < 72)
        win->settings.font_size += 1;
    else if (dy > 0 && win->settings.font_size > 6)
        win->settings.font_size -= 1;
    else
        return TRUE;

    notes_window_apply_settings(win);
    settings_save(&win->settings);
    return TRUE;
}

static gboolean scroll_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->scroll_idle_id = 0;
    GtkTextMark *insert = gtk_text_buffer_get_insert(win->buffer);
    gtk_text_view_scroll_to_mark(win->text_view, insert, 0.05, FALSE, 0, 0);
    return G_SOURCE_REMOVE;
}

static gboolean intensity_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->intensity_idle_id = 0;
    if (win->settings.font_intensity < 0.99)
        editor_view_apply_font_intensity(win);
    return G_SOURCE_REMOVE;
}

void editor_view_update_dirty_state(NotesWindow *win) {
    if (win->original_content) {
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(win->buffer, &s, &e);
        char *text = gtk_text_buffer_get_text(win->buffer, &s, &e, FALSE);
        gsize len = strlen(text);
        guint32 h = fnv1a_hash(text, len);
        gboolean same = (h == win->original_hash) && (strcmp(text, win->original_content) == 0);
        g_free(text);

        if (same && win->dirty) {
            win->dirty = FALSE;
            if (win->current_file[0]) {
                char *base = g_path_get_basename(win->current_file);
                if (notes_window_is_remote(win)) {
                    char title[512];
                    snprintf(title, sizeof(title), "%s [%s@%s]", base, win->ssh_user, win->ssh_host);
                    gtk_window_set_title(GTK_WINDOW(win->window), title);
                } else {
                    gtk_window_set_title(GTK_WINDOW(win->window), base);
                }
                g_free(base);
            } else {
                gtk_window_set_title(GTK_WINDOW(win->window), "Notes Light");
            }
            return;
        }
    }

    if (!win->dirty) {
        win->dirty = TRUE;
        if (win->current_file[0]) {
            char *base = g_path_get_basename(win->current_file);
            char title[256];
            snprintf(title, sizeof(title), "%s *", base);
            gtk_window_set_title(GTK_WINDOW(win->window), title);
            g_free(base);
        } else {
            gtk_window_set_title(GTK_WINDOW(win->window), "Notes Light *");
        }
    }
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
    NotesWindow *win = data;
    editor_view_update_dirty_state(win);
    if (win->settings.show_line_numbers)
        editor_view_update_line_numbers(win);
    editor_view_update_cursor_position(win);
    editor_view_update_line_highlights(win);
    if (win->settings.font_intensity < 0.99 && win->intensity_idle_id == 0)
        win->intensity_idle_id = g_idle_add(intensity_idle_cb, win);
    if (win->scroll_idle_id == 0)
        win->scroll_idle_id = g_idle_add(scroll_idle_cb, win);
    (void)buffer;
}

static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, gpointer data) {
    (void)buffer; (void)pspec;
    NotesWindow *win = data;
    editor_view_update_cursor_position(win);
    editor_view_update_line_highlights(win);
}

void editor_view_block_signals(NotesWindow *win) {
    g_signal_handlers_block_by_func(win->buffer, on_buffer_changed, win);
    g_signal_handlers_block_by_func(win->buffer, on_cursor_moved, win);
}

void editor_view_unblock_signals(NotesWindow *win) {
    g_signal_handlers_unblock_by_func(win->buffer, on_buffer_changed, win);
    g_signal_handlers_unblock_by_func(win->buffer, on_cursor_moved, win);
}

void editor_view_draw_line_numbers(GtkDrawingArea *area, cairo_t *cr,
                                    int width, int height, gpointer data) {
    (void)area; (void)height;
    NotesWindow *win = data;
    if (!win->settings.show_line_numbers) return;

    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, win->settings.font);
    pango_font_description_set_size(fd, win->settings.font_size * PANGO_SCALE);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
    pango_layout_set_width(layout, (width - 12) * PANGO_SCALE);

    GdkRGBA color;
    const char *theme_fg = notes_theme_fg(win->settings.theme);
    if (theme_fg) {
        gdk_rgba_parse(&color, theme_fg);
    } else if (notes_is_dark_theme(win->settings.theme)) {
        color = (GdkRGBA){0.85, 0.85, 0.85, 1.0};
    } else {
        color = (GdkRGBA){0.12, 0.12, 0.12, 1.0};
    }
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.3);

    GdkRectangle visible;
    gtk_text_view_get_visible_rect(win->text_view, &visible);

    GtkTextIter iter;
    gtk_text_view_get_iter_at_location(win->text_view, &iter,
                                       visible.x, visible.y);
    gtk_text_iter_set_line_offset(&iter, 0);

    while (TRUE) {
        int buf_y, line_height;
        gtk_text_view_get_line_yrange(win->text_view, &iter, &buf_y, &line_height);

        if (buf_y > visible.y + visible.height) break;

        int win_x, win_y;
        gtk_text_view_buffer_to_window_coords(win->text_view,
            GTK_TEXT_WINDOW_WIDGET, 0, buf_y, &win_x, &win_y);

        char num[16];
        snprintf(num, sizeof(num), "%d", gtk_text_iter_get_line(&iter) + 1);
        pango_layout_set_text(layout, num, -1);
        cairo_move_to(cr, 4, win_y);
        pango_cairo_show_layout(cr, layout);

        if (!gtk_text_iter_forward_line(&iter)) break;
    }

    g_object_unref(layout);
    pango_font_description_free(fd);
}

static gboolean line_numbers_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->line_numbers_idle_id = 0;
    if (!win->settings.show_line_numbers) return G_SOURCE_REMOVE;

    int lines = gtk_text_buffer_get_line_count(win->buffer);

    int digits = 1, n = lines;
    while (n >= 10) { digits++; n /= 10; }
    if (digits < 2) digits = 2;

    char sample[16];
    memset(sample, '9', (size_t)digits);
    sample[digits] = '\0';

    PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(win->line_numbers), sample);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, win->settings.font);
    pango_font_description_set_size(fd, win->settings.font_size * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    (void)ph;
    pango_font_description_free(fd);
    g_object_unref(layout);

    int width = pw + 12;
    gtk_widget_set_size_request(GTK_WIDGET(win->line_numbers), width, -1);

    gtk_widget_queue_draw(GTK_WIDGET(win->line_numbers));
    return G_SOURCE_REMOVE;
}

void editor_view_update_line_numbers(NotesWindow *win) {
    if (!win->settings.show_line_numbers) return;
    if (win->line_numbers_idle_id == 0)
        win->line_numbers_idle_id = g_idle_add(line_numbers_idle_cb, win);
}

void editor_view_apply_font_intensity(NotesWindow *win) {
    double alpha = win->settings.font_intensity;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    gtk_text_buffer_remove_tag(win->buffer, win->intensity_tag, &start, &end);

    if (win->settings.highlight_syntax) {
        gtk_widget_set_opacity(GTK_WIDGET(win->text_view),
                               alpha >= 0.99 ? 1.0 : alpha);
        return;
    }

    gtk_widget_set_opacity(GTK_WIDGET(win->text_view), 1.0);

    if (alpha >= 0.99)
        return;

    const char *fg = notes_theme_fg(win->settings.theme);

    GdkRGBA color;
    if (fg) {
        gdk_rgba_parse(&color, fg);
    } else if (notes_is_dark_theme(win->settings.theme)) {
        color = (GdkRGBA){1.0, 1.0, 1.0, 1.0};
    } else {
        color = (GdkRGBA){0.0, 0.0, 0.0, 1.0};
    }
    color.alpha = alpha;
    g_object_set(win->intensity_tag, "foreground-rgba", &color, NULL);

    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    gtk_text_buffer_apply_tag(win->buffer, win->intensity_tag, &start, &end);
}

void editor_view_apply_whitespace(NotesWindow *win) {
    GtkSourceSpaceDrawer *drawer = gtk_source_view_get_space_drawer(win->source_view);
    GtkSourceSpaceTypeFlags types = win->settings.show_whitespace
        ? (GTK_SOURCE_SPACE_TYPE_SPACE | GTK_SOURCE_SPACE_TYPE_TAB |
           GTK_SOURCE_SPACE_TYPE_NEWLINE | GTK_SOURCE_SPACE_TYPE_NBSP)
        : GTK_SOURCE_SPACE_TYPE_NONE;
    gtk_source_space_drawer_set_types_for_locations(drawer,
        GTK_SOURCE_SPACE_LOCATION_ALL, types);
    gtk_source_space_drawer_set_enable_matrix(drawer, win->settings.show_whitespace);
}

void editor_view_apply_settings(NotesWindow *win) {
    int extra = (int)((win->settings.line_spacing - 1.0) * win->settings.font_size * 0.5);
    if (extra < 0) extra = 0;
    gtk_text_view_set_pixels_above_lines(win->text_view, extra);
    gtk_text_view_set_pixels_below_lines(win->text_view, extra);

    gtk_widget_set_visible(win->ln_scrolled, win->settings.show_line_numbers);
    win->cached_line_count = 0;
    if (win->settings.show_line_numbers)
        editor_view_update_line_numbers(win);

    gtk_text_view_set_wrap_mode(win->text_view,
        win->settings.wrap_lines ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);

    editor_view_apply_highlight_color(win);
    editor_view_update_line_highlights(win);
    editor_view_apply_font_intensity(win);
    editor_view_apply_whitespace(win);
}

GtkWidget *editor_view_create(NotesWindow *win) {
    win->line_numbers = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_can_focus(GTK_WIDGET(win->line_numbers), FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(win->line_numbers), "line-numbers");
    gtk_drawing_area_set_draw_func(win->line_numbers,
                                    editor_view_draw_line_numbers, win, NULL);

    win->source_buffer = gtk_source_buffer_new(NULL);
    win->buffer = GTK_TEXT_BUFFER(win->source_buffer);
    NotesTextView *ntv = g_object_new(NOTES_TYPE_TEXT_VIEW, "buffer", win->buffer, NULL);
    ntv->win = win;
    win->source_view = GTK_SOURCE_VIEW(ntv);
    win->text_view = GTK_TEXT_VIEW(ntv);
    gtk_text_view_set_wrap_mode(win->text_view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(win->text_view, 12);
    gtk_text_view_set_right_margin(win->text_view, 12);
    gtk_text_view_set_top_margin(win->text_view, 8);
    gtk_text_view_set_bottom_margin(win->text_view, 8);

    win->search_tag = gtk_text_buffer_create_tag(win->buffer, "search-match",
                                                  "background", "#f0b030",
                                                  "foreground", "#000000", NULL);

    win->intensity_tag = gtk_text_buffer_create_tag(win->buffer, "intensity",
                                                     "foreground-rgba", NULL, NULL);

    g_signal_connect(win->buffer, "changed", G_CALLBACK(on_buffer_changed), win);
    g_signal_connect(win->buffer, "notify::cursor-position",
                     G_CALLBACK(on_cursor_moved), win);

    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_text_view_scroll), win);
    gtk_widget_add_controller(GTK_WIDGET(win->text_view), scroll);

    return GTK_WIDGET(win->text_view);
}
