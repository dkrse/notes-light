#define _GNU_SOURCE
#include "actions_internal.h"
#include "ssh.h"
#include "encoding_view.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void on_new_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    notes_set_original(win, g_strdup(""));
    gtk_text_buffer_set_text(win->buffer, "", -1);
    win->dirty = FALSE;
    win->is_binary = FALSE;
    win->is_truncated = FALSE;
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes Light");
    notes_encoding_reset(win);
    win->current_file[0] = '\0';
    win->settings.last_file[0] = '\0';
    settings_save(&win->settings);
    gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
}


/* Every save goes through here: the text is verified to be valid UTF-8 and
   written out byte for byte, atomically, with no BOM. A GtkTextBuffer can
   only ever hold UTF-8, so the check is a guarantee, not a guess — if it
   ever failed we would rather refuse than write a broken file. */
gboolean notes_save_text_utf8(NotesWindow *win, const char *path,
                              const char *text) {
    gsize len = strlen(text);
    if (!g_utf8_validate(text, (gssize)len, NULL)) {
        GtkAlertDialog *dlg = gtk_alert_dialog_new(
            "Refusing to save: the text is not valid UTF-8.");
        gtk_alert_dialog_set_modal(dlg, TRUE);
        gtk_alert_dialog_show(dlg, GTK_WINDOW(win->window));
        g_object_unref(dlg);
        return FALSE;
    }

    char tmp[2112];
    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    int fd = g_mkstemp(tmp);
    if (fd < 0) return FALSE;

    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        g_remove(tmp);
        return FALSE;
    }

    gboolean ok = (len == 0) || (fwrite(text, 1, len, f) == len);
    ok = ok && (fflush(f) == 0);
    fclose(f);

    if (ok) {
        g_rename(tmp, path);
        /* Remember what is on disk now, so the file monitor recognises this
           write as ours and does not offer to reload. */
        win->disk_hash = fnv1a_hash(text, len);
        return TRUE;
    }
    g_remove(tmp);
    return FALSE;
}

void on_save(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (!win->dirty) return;
    if (win->is_truncated || win->is_binary) {
        g_action_group_activate_action(G_ACTION_GROUP(win->window), "save-as", NULL);
        return;
    }

    if (notes_window_is_remote(win) && ssh_path_is_remote(win->current_file)) {
        save_remote_file(win);
        return;
    }

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

    if (win->current_file[0] != '\0') {
        if (!notes_save_text_utf8(win, win->current_file, text)) {
            g_free(text);
            return;
        }
    } else {
        g_free(text);
        g_action_group_activate_action(G_ACTION_GROUP(win->window), "save-as", NULL);
        return;
    }
    notes_set_original(win, text);
    win->dirty = FALSE;
    win->is_binary = FALSE;
    notes_encoding_mark_utf8(win);

    char *base = g_path_get_basename(win->current_file);
    gtk_window_set_title(GTK_WINDOW(win->window), base);
    g_free(base);
}

static void on_save_as_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(win->buffer, &start, &end);
            char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

            if (notes_save_text_utf8(win, path, text)) {
                snprintf(win->current_file, sizeof(win->current_file), "%s", path);
                snprintf(win->settings.last_file, sizeof(win->settings.last_file),
                         "%s", path);
                notes_set_original(win, text);
                win->dirty = FALSE;
                notes_encoding_mark_utf8(win);
                settings_save(&win->settings);

                char *base = g_path_get_basename(path);
                gtk_window_set_title(GTK_WINDOW(win->window), base);
                g_free(base);
            } else {
                g_free(text);
            }
            g_free(path);
        }
        g_object_unref(file);
    }
}

void on_save_as(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save As");

    gtk_file_dialog_save(dialog, GTK_WINDOW(win->window), NULL, on_save_as_cb, win);
    /* The async call keeps its own reference; the source object handed to
       the callback is borrowed and must not be unreffed there. */
    g_object_unref(dialog);
}

static void on_open_file_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            notes_window_request_open(win, path);
            g_free(path);
        }
        g_object_unref(file);
    }
}

void on_open_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open File");
    gtk_file_dialog_open(dialog, GTK_WINDOW(win->window), NULL, on_open_file_cb, win);
    g_object_unref(dialog);
}

void on_open_recent(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action;
    NotesWindow *win = data;
    if (!param) return;
    const char *path = g_variant_get_string(param, NULL);
    if (path && path[0])
        notes_window_request_open(win, path);
}

void on_print(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    notes_window_print(data);
}
