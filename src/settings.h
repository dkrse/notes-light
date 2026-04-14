#ifndef NOTES_SETTINGS_H
#define NOTES_SETTINGS_H

#include <gtk/gtk.h>

typedef struct {
    char font[256];
    double line_spacing;
    int font_size;
    char gui_font[256];
    int gui_font_size;
    double font_intensity;  /* 0.3 .. 1.0 */
    char theme[64];
    gboolean show_line_numbers;
    gboolean highlight_current_line;
    gboolean wrap_lines;
    int window_width;
    int window_height;
    char last_file[2048];
} NotesSettings;

void     settings_load(NotesSettings *s);
void     settings_save(const NotesSettings *s);
char    *settings_get_config_path(void);

#endif
