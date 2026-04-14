#ifndef NOTES_WINDOW_H
#define NOTES_WINDOW_H

#include <gtk/gtk.h>
#include "settings.h"

typedef struct {
    GtkApplicationWindow *window;
    GtkTextView          *text_view;
    GtkTextBuffer        *buffer;
    GtkDrawingArea       *line_numbers;
    GtkWidget            *ln_scrolled;
    GtkWidget            *editor_box;
    int                   highlight_line;
    GdkRGBA               highlight_rgba;
    GtkTextTag           *intensity_tag;
    GtkLabel             *status_encoding;
    GtkLabel             *status_cursor;
    NotesSettings         settings;
    GtkCssProvider       *css_provider;
    char                  current_file[2048];
    int                   cached_line_count;
    guint                 line_numbers_idle_id;
    guint                 intensity_idle_id;
    guint                 scroll_idle_id;
    guint                 title_idle_id;
    gboolean              dirty;
    gboolean              is_binary;
    gboolean              is_truncated;
    char                 *original_content;
} NotesWindow;

NotesWindow *notes_window_new(GtkApplication *app);
void         notes_window_apply_settings(NotesWindow *win);
void         notes_window_load_file(NotesWindow *win, const char *path);

#endif
