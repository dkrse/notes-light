#ifndef NOTES_WINDOW_H
#define NOTES_WINDOW_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include "settings.h"
#include "encoding.h"

typedef struct {
    GtkApplicationWindow *window;
    GtkSourceView        *source_view;
    GtkSourceBuffer      *source_buffer;
    GtkTextView          *text_view;     /* alias for source_view */
    GtkTextBuffer        *buffer;        /* alias for source_buffer */
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
    guint                 enc_marks_idle_id;
    gboolean              dirty;
    gboolean              is_binary;
    gboolean              is_truncated;
    char                 *original_content;
    glong                 original_chars;  /* character count of the above */
    guint32               original_hash;   /* of the decoded buffer text */
    guint32               disk_hash;       /* of the raw bytes on disk */

    /* Encoding */
    char                  file_encoding[64];  /* detected source encoding */
    gboolean              encoding_mixed;     /* file mixed UTF-8 + a code page */
    gsize                 encoding_utf8_bytes;
    gsize                 encoding_foreign_bytes;
    int                   encoding_foreign_runs;
    char                  file_eol[8];        /* LF / CRLF / CR / mixed */
    char                  status_extra[64];   /* size / remote / truncation note */
    gboolean              encoding_converted; /* buffer was transcoded to UTF-8 */
    int                   encoding_confidence;
    GtkTextTag           *encoding_tags[NOTES_CHAR_CLASS_COUNT];
    gboolean              encoding_marks_shown;
    NotesEncodingSpan    *enc_spans;          /* non-ASCII runs, char offsets */
    int                   enc_nonascii_total; /* characters, not runs */
    int                   enc_invisible_total;
    int                   enc_span_count;
    int                   enc_span_current;

    /* SSH/SFTP state */
    char                  ssh_host[256];
    char                  ssh_user[128];
    int                   ssh_port;
    char                  ssh_key[1024];
    char                  ssh_remote_path[1024];
    char                  ssh_mount[2048];
    char                  ssh_ctl_path[512];
    char                  ssh_ctl_dir[256];
    GtkWidget            *ssh_status_btn;

    /* Search/Replace */
    GtkWidget            *search_bar;
    GtkWidget            *search_entry;
    GtkWidget            *replace_entry;
    GtkWidget            *replace_box;
    GtkWidget            *match_label;
    GtkTextTag           *search_tag;
    GtkWidget            *scrolled_window;
    GtkWidget            *scrollbar_overlay;
    int                  *match_lines;
    int                  *match_offsets;  /* byte offsets of each match start */
    int                   match_count;
    int                   match_current;
    gboolean              search_busy;    /* re-entrancy guard for replaces */

    /* Auto-reload */
    GFileMonitor         *file_monitor;

    /* Recent files menu (rebuilt dynamically) */
    GMenu                *recent_menu;
} NotesWindow;

guint32      fnv1a_hash(const char *data, gsize len);
NotesWindow *notes_window_new(GtkApplication *app);
/* Load settings.last_file, if any. Only for a start without file arguments. */
void         notes_window_restore_last(NotesWindow *win);
void         notes_window_apply_settings(NotesWindow *win);
void         notes_window_load_file(NotesWindow *win, const char *path);
/* Load `path`, asking first when the current document has unsaved changes.
   This is the entry point for every user-initiated open. */
void         notes_window_request_open(NotesWindow *win, const char *path);
/* charset == NULL or "" -> auto-detect, otherwise decode with that charset */
void         notes_window_load_file_as(NotesWindow *win, const char *path,
                                        const char *charset);
void         notes_window_show_search(NotesWindow *win, gboolean with_replace);
void         notes_window_goto_line(NotesWindow *win);
void         notes_window_ssh_connect(NotesWindow *win,
                                       const char *host, const char *user,
                                       int port, const char *key,
                                       const char *remote_path);
void         notes_window_ssh_disconnect(NotesWindow *win);
gboolean     notes_window_is_remote(NotesWindow *win);
gboolean     save_remote_file(NotesWindow *win);
void         notes_window_open_remote_file(NotesWindow *win, const char *remote_path);
void         notes_window_recent_menu_rebuild(NotesWindow *win);
void         notes_window_print(NotesWindow *win);
/* Validated, atomic, BOM-free UTF-8 write used by every local save. */
gboolean     notes_save_text_utf8(NotesWindow *win, const char *path,
                                   const char *text);
/* Takes ownership of `text` (may be NULL) and refreshes hash + char count. */
void         notes_set_original(NotesWindow *win, char *text);
/* Same, but avoids re-deriving what the caller already knows. */
void         notes_set_original_take(NotesWindow *win, char *text, gsize len,
                                      guint32 hash, glong chars);

#endif
