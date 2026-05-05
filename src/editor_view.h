#ifndef NOTES_EDITOR_VIEW_H
#define NOTES_EDITOR_VIEW_H

#include "window.h"

#define NOTES_TYPE_TEXT_VIEW (notes_text_view_get_type())
G_DECLARE_FINAL_TYPE(NotesTextView, notes_text_view, NOTES, TEXT_VIEW, GtkSourceView)

GtkWidget *editor_view_create(NotesWindow *win);
void       editor_view_apply_settings(NotesWindow *win);
void       editor_view_apply_whitespace(NotesWindow *win);
void       editor_view_apply_font_intensity(NotesWindow *win);
void       editor_view_update_line_highlights(NotesWindow *win);
void       editor_view_update_cursor_position(NotesWindow *win);
void       editor_view_update_line_numbers(NotesWindow *win);
void       editor_view_apply_highlight_color(NotesWindow *win);
void       editor_view_block_signals(NotesWindow *win);
void       editor_view_unblock_signals(NotesWindow *win);
void       editor_view_update_dirty_state(NotesWindow *win);
void       editor_view_draw_line_numbers(GtkDrawingArea *area, cairo_t *cr,
                                          int width, int height, gpointer data);

#endif
