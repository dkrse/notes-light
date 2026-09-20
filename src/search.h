#ifndef NOTES_SEARCH_H
#define NOTES_SEARCH_H

#include "window.h"

GtkWidget *search_build_bar(NotesWindow *win);
void       search_init_scrollbar_overlay(NotesWindow *win);
void       search_show(NotesWindow *win, gboolean with_replace);
void       search_goto_line_dialog(NotesWindow *win);

/* Re-run the highlight after the buffer changed; stored match offsets would
   otherwise point at stale positions. No-op while the bar is hidden. */
void search_refresh_after_edit(NotesWindow *win);

#endif
