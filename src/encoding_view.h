#ifndef NOTES_ENCODING_VIEW_H
#define NOTES_ENCODING_VIEW_H

#include "window.h"
#include "encoding.h"

/* Record the detection result for the freshly loaded buffer and refresh the
   status bar; `utf8` is the decoded text now in the buffer. `extra` is an optional trailing status field ("remote",
   "showing first 5 MB of ...") or NULL. */
void notes_encoding_set_detected(NotesWindow *win,
                                 const NotesEncodingInfo *info,
                                 const char *utf8, gsize utf8_len,
                                 const char *extra);

/* Reset to a plain empty UTF-8 document. */
void notes_encoding_reset(NotesWindow *win);

/* Re-scan the buffer for non-ASCII runs and re-apply the highlight tag
   (no-op when the highlight is toggled off). */
void notes_encoding_refresh_marks(NotesWindow *win);

/* Record that the document is now single-encoding UTF-8 (after a save or a
   conversion) and refresh the status bar. */
void notes_encoding_mark_utf8(NotesWindow *win);

/* Rebuild the status-bar text from the current state. */
void notes_encoding_update_status(NotesWindow *win);

/* Schedule a mark + count refresh after an edit (coalesced on idle, no-op
   while the highlight is off). */
void notes_encoding_schedule_refresh(NotesWindow *win);

/* Free span arrays (called from on_destroy). */
void notes_encoding_free(NotesWindow *win);

/* Action handlers */
void on_encoding_convert(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_highlight(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_next(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_info(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_asciify(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_reload_as(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_save_as(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_fix_mojibake(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_eol(GSimpleAction *action, GVariant *param, gpointer data);
void on_encoding_next_class(GSimpleAction *action, GVariant *param, gpointer data);

#endif
