#define _GNU_SOURCE
#include "ssh_window.h"
#include "ssh.h"
#include "editor_view.h"
#include "theme.h"
#include <string.h>
#include <unistd.h>

gboolean notes_window_is_remote(NotesWindow *win) {
    return win->ssh_host[0] != '\0';
}

void ssh_window_update_status(NotesWindow *win) {
    gboolean connected = notes_window_is_remote(win);
    if (connected) {
        char label[512];
        snprintf(label, sizeof(label), "SSH: %s@%s", win->ssh_user, win->ssh_host);
        gtk_button_set_label(GTK_BUTTON(win->ssh_status_btn), label);
    } else {
        gtk_button_set_label(GTK_BUTTON(win->ssh_status_btn), "SSH: Off");
    }

    GAction *a;
    a = g_action_map_lookup_action(G_ACTION_MAP(win->window), "open-remote");
    if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), connected);
    a = g_action_map_lookup_action(G_ACTION_MAP(win->window), "sftp-disconnect");
    if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), connected);
}

void notes_window_ssh_connect(NotesWindow *win,
                               const char *host, const char *user,
                               int port, const char *key,
                               const char *remote_path) {
    if (notes_window_is_remote(win))
        notes_window_ssh_disconnect(win);

    g_strlcpy(win->ssh_host, host, sizeof(win->ssh_host));
    g_strlcpy(win->ssh_user, user, sizeof(win->ssh_user));
    win->ssh_port = port;
    g_strlcpy(win->ssh_key, key, sizeof(win->ssh_key));
    g_strlcpy(win->ssh_remote_path, remote_path, sizeof(win->ssh_remote_path));

    snprintf(win->ssh_mount, sizeof(win->ssh_mount),
             "/tmp/note-light-sftp-%d-%s@%s", (int)getpid(), user, host);

    ssh_ctl_start(win->ssh_ctl_dir, sizeof(win->ssh_ctl_dir),
                  win->ssh_ctl_path, sizeof(win->ssh_ctl_path),
                  host, user, port, key);

    ssh_window_update_status(win);
}

void notes_window_ssh_disconnect(NotesWindow *win) {
    if (!notes_window_is_remote(win)) return;

    ssh_ctl_stop(win->ssh_ctl_path, win->ssh_ctl_dir,
                 win->ssh_host, win->ssh_user);

    win->ssh_host[0] = '\0';
    win->ssh_user[0] = '\0';
    win->ssh_port = 0;
    win->ssh_key[0] = '\0';
    win->ssh_remote_path[0] = '\0';
    win->ssh_mount[0] = '\0';

    ssh_window_update_status(win);
}

void notes_window_open_remote_file(NotesWindow *win, const char *remote_path) {
    char *contents = NULL;
    gsize len = 0;

    if (!ssh_cat_file(win->ssh_host, win->ssh_user, win->ssh_port,
                      win->ssh_key, win->ssh_ctl_path,
                      remote_path, &contents, &len, 5 * 1024 * 1024)) {
        return;
    }

    gboolean is_binary = FALSE;
    gsize check = len < 8192 ? len : 8192;
    for (gsize i = 0; i < check; i++) {
        if (contents[i] == '\0') { is_binary = TRUE; break; }
    }
    if (is_binary) {
        for (gsize i = 0; i < len; i++) {
            if (contents[i] == '\0') contents[i] = '.';
        }
    }
    if (!g_utf8_validate(contents, (gssize)len, NULL)) {
        is_binary = TRUE;
        gsize bw = 0;
        char *utf8 = g_convert_with_fallback(contents, (gssize)len,
                         "UTF-8", "ISO-8859-1", ".", NULL, &bw, NULL);
        if (utf8) { g_free(contents); contents = utf8; len = bw; }
    }

    editor_view_block_signals(win);

    gtk_text_buffer_set_text(win->buffer, contents, (int)len);

    g_free(win->original_content);
    win->original_content = contents;
    win->original_hash = fnv1a_hash(contents, len);
    win->is_binary = is_binary;
    win->is_truncated = FALSE;
    win->dirty = FALSE;

    snprintf(win->current_file, sizeof(win->current_file), "%s%s",
             win->ssh_mount, remote_path);

    char *base = g_path_get_basename(remote_path);
    char title[512];
    snprintf(title, sizeof(title), "%s [%s@%s]", base, win->ssh_user, win->ssh_host);
    gtk_window_set_title(GTK_WINDOW(win->window), title);
    g_free(base);

    GtkTextIter start;
    gtk_text_buffer_get_start_iter(win->buffer, &start);
    gtk_text_buffer_place_cursor(win->buffer, &start);

    editor_view_unblock_signals(win);

    editor_view_update_cursor_position(win);
    if (win->settings.show_line_numbers)
        editor_view_update_line_numbers(win);
    editor_view_update_line_highlights(win);
    editor_view_apply_font_intensity(win);

    char status[128];
    snprintf(status, sizeof(status), "UTF-8 | %s | remote", is_binary ? "BIN" : "TEXT");
    gtk_label_set_text(win->status_encoding, status);
}

gboolean save_remote_file(NotesWindow *win) {
    if (!notes_window_is_remote(win)) return FALSE;
    if (!ssh_path_is_remote(win->current_file)) return FALSE;

    char remote[4096];
    ssh_to_remote_path(win->ssh_mount, win->ssh_remote_path,
                       win->current_file, remote, sizeof(remote));

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);
    char *text = gtk_text_buffer_get_text(win->buffer, &s, &e, FALSE);
    gsize len = strlen(text);

    gboolean ok = ssh_write_file(win->ssh_host, win->ssh_user, win->ssh_port,
                                  win->ssh_key, win->ssh_ctl_path,
                                  remote, text, len);

    if (ok) {
        g_free(win->original_content);
        win->original_content = text;
        win->original_hash = fnv1a_hash(text, len);
        win->dirty = FALSE;

        char *base = g_path_get_basename(remote);
        char title[512];
        snprintf(title, sizeof(title), "%s [%s@%s]", base, win->ssh_user, win->ssh_host);
        gtk_window_set_title(GTK_WINDOW(win->window), title);
        g_free(base);
    } else {
        g_free(text);
    }

    return ok;
}
