#define _GNU_SOURCE
#include "actions_internal.h"
#include "ssh.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void on_new_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    g_free(win->original_content);
    win->original_content = g_strdup("");
    win->original_hash = fnv1a_hash("", 0);
    gtk_text_buffer_set_text(win->buffer, "", -1);
    win->dirty = FALSE;
    win->is_binary = FALSE;
    win->is_truncated = FALSE;
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes Light");
    gtk_label_set_text(win->status_encoding, "UTF-8");
    win->current_file[0] = '\0';
    win->settings.last_file[0] = '\0';
    settings_save(&win->settings);
    gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
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
        char tmp[2112];
        snprintf(tmp, sizeof(tmp), "%s.XXXXXX", win->current_file);
        int fd = g_mkstemp(tmp);
        if (fd >= 0) {
            FILE *f = fdopen(fd, "w");
            if (f) {
                gboolean ok = (fputs(text, f) != EOF);
                ok = ok && (fflush(f) == 0);
                fclose(f);
                if (ok)
                    g_rename(tmp, win->current_file);
                else
                    g_remove(tmp);
            } else {
                close(fd);
                g_remove(tmp);
            }
        }
    } else {
        g_free(text);
        g_action_group_activate_action(G_ACTION_GROUP(win->window), "save-as", NULL);
        return;
    }
    g_free(win->original_content);
    win->original_content = text;
    win->original_hash = fnv1a_hash(text, strlen(text));
    win->dirty = FALSE;
    win->is_binary = FALSE;

    char *base = g_path_get_basename(win->current_file);
    gtk_window_set_title(GTK_WINDOW(win->window), base);
    g_free(base);
}

static void on_save_as_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *file = gtk_file_dialog_save_finish(dialog, result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(win->buffer, &start, &end);
            char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

            char tmp[2112];
            snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
            int fd = g_mkstemp(tmp);
            if (fd >= 0) {
                FILE *f = fdopen(fd, "w");
                if (f) {
                    gboolean ok = (fputs(text, f) != EOF);
                    ok = ok && (fflush(f) == 0);
                    fclose(f);
                    if (ok)
                        g_rename(tmp, path);
                    else
                        g_remove(tmp);
                } else {
                    close(fd);
                    g_remove(tmp);
                }
            }

            snprintf(win->current_file, sizeof(win->current_file), "%s", path);
            snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", path);
            g_free(win->original_content);
            win->original_content = text;
            win->original_hash = fnv1a_hash(text, strlen(text));
            win->dirty = FALSE;
            settings_save(&win->settings);

            char *base = g_path_get_basename(path);
            gtk_window_set_title(GTK_WINDOW(win->window), base);
            g_free(base);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(dialog);
}

void on_save_as(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save As");

    gtk_file_dialog_save(dialog, GTK_WINDOW(win->window), NULL, on_save_as_cb, win);
}

static void on_open_file_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            notes_window_load_file(win, path);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(dialog);
}

void on_open_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open File");
    gtk_file_dialog_open(dialog, GTK_WINDOW(win->window), NULL, on_open_file_cb, win);
}

void on_open_recent(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action;
    NotesWindow *win = data;
    if (!param) return;
    const char *path = g_variant_get_string(param, NULL);
    if (path && path[0])
        notes_window_load_file(win, path);
}

void on_print(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    notes_window_print(data);
}
