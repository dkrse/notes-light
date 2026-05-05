#ifndef NOTES_THEME_H
#define NOTES_THEME_H

#include "window.h"

gboolean    notes_is_dark_theme(const char *theme);
const char *notes_theme_fg(const char *theme);
const char *notes_theme_bg(const char *theme);

void notes_apply_theme(NotesWindow *win);
void notes_apply_css(NotesWindow *win);
void notes_apply_source_style(NotesWindow *win);
void notes_apply_source_language(NotesWindow *win, const char *path);

#endif
