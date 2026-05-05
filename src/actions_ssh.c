#define _GNU_SOURCE
#include "actions_internal.h"
#include "ssh.h"
#include <adwaita.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static GtkWidget *make_label(const char *text) {
    GtkWidget *lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
    return lbl;
}

typedef struct {
    NotesWindow     *win;
    GtkWindow       *dialog;
    SftpConnections  conns;
    GtkListBox      *conn_list;
    GtkEntry        *name_entry;
    GtkEntry        *host_entry;
    GtkEntry        *port_entry;
    GtkEntry        *user_entry;
    GtkEntry        *path_entry;
    GtkEntry        *password_entry;
    GtkCheckButton  *use_key_check;
    GtkEntry        *key_entry;
    GtkWidget       *key_browse_btn;
    GtkWidget       *password_row;
    GtkWidget       *key_row;
    GtkWidget       *key_btn_row;
    int              selected_idx;
    int              ref_count;
    gboolean         dialog_alive;
} SftpCtx;

static SftpCtx *sftp_ctx_ref(SftpCtx *ctx) {
    ctx->ref_count++;
    return ctx;
}

static void sftp_ctx_unref(SftpCtx *ctx) {
    if (--ctx->ref_count <= 0)
        g_free(ctx);
}

static void sftp_populate_list(SftpCtx *ctx) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ctx->conn_list))))
        gtk_list_box_remove(ctx->conn_list, child);
    for (int i = 0; i < ctx->conns.count; i++) {
        GtkWidget *lbl = gtk_label_new(ctx->conns.items[i].name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 8);
        gtk_widget_set_margin_end(lbl, 8);
        gtk_widget_set_margin_top(lbl, 4);
        gtk_widget_set_margin_bottom(lbl, 4);
        gtk_list_box_append(ctx->conn_list, lbl);
    }
}

static void sftp_update_auth_visibility(SftpCtx *ctx) {
    gboolean use_key = gtk_check_button_get_active(ctx->use_key_check);
    gtk_widget_set_visible(ctx->password_row, !use_key);
    gtk_widget_set_visible(GTK_WIDGET(ctx->password_entry), !use_key);
    gtk_widget_set_visible(ctx->key_row, use_key);
    gtk_widget_set_visible(GTK_WIDGET(ctx->key_entry), use_key);
    gtk_widget_set_visible(ctx->key_btn_row, use_key);
}

static void on_use_key_toggled(GtkCheckButton *btn, gpointer data) {
    (void)btn;
    sftp_update_auth_visibility(data);
}

static void on_key_file_selected(GObject *src, GAsyncResult *res, gpointer data) {
    SftpCtx *ctx = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(src);
    GFile *file = gtk_file_dialog_open_finish(dialog, res, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            gtk_editable_set_text(GTK_EDITABLE(ctx->key_entry), path);
            g_free(path);
        }
        g_object_unref(file);
    }
}

static void on_key_browse(GtkButton *btn, gpointer data) {
    (void)btn;
    SftpCtx *ctx = data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Private Key");
    char ssh_dir[1024];
    snprintf(ssh_dir, sizeof(ssh_dir), "%s/.ssh", g_get_home_dir());
    GFile *init = g_file_new_for_path(ssh_dir);
    gtk_file_dialog_set_initial_folder(dialog, init);
    g_object_unref(init);
    gtk_file_dialog_open(dialog, ctx->dialog, NULL, on_key_file_selected, ctx);
}

static void on_conn_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    SftpCtx *ctx = data;
    if (!row) { ctx->selected_idx = -1; return; }
    int idx = gtk_list_box_row_get_index(row);
    if (idx < 0 || idx >= ctx->conns.count) return;
    ctx->selected_idx = idx;
    SftpConnection *c = &ctx->conns.items[idx];
    gtk_editable_set_text(GTK_EDITABLE(ctx->name_entry), c->name);
    gtk_editable_set_text(GTK_EDITABLE(ctx->host_entry), c->host);
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", c->port);
    gtk_editable_set_text(GTK_EDITABLE(ctx->port_entry), port_str);
    gtk_editable_set_text(GTK_EDITABLE(ctx->user_entry), c->user);
    gtk_editable_set_text(GTK_EDITABLE(ctx->path_entry), c->remote_path);
    gtk_check_button_set_active(ctx->use_key_check, c->use_key);
    gtk_editable_set_text(GTK_EDITABLE(ctx->key_entry), c->key_path);
    gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), "");
    sftp_update_auth_visibility(ctx);
}

static void sftp_save_form_to_conn(SftpCtx *ctx, SftpConnection *c) {
    g_strlcpy(c->name, gtk_editable_get_text(GTK_EDITABLE(ctx->name_entry)), sizeof(c->name));
    g_strlcpy(c->host, gtk_editable_get_text(GTK_EDITABLE(ctx->host_entry)), sizeof(c->host));
    c->port = atoi(gtk_editable_get_text(GTK_EDITABLE(ctx->port_entry)));
    if (c->port <= 0) c->port = 22;
    g_strlcpy(c->user, gtk_editable_get_text(GTK_EDITABLE(ctx->user_entry)), sizeof(c->user));
    g_strlcpy(c->remote_path, gtk_editable_get_text(GTK_EDITABLE(ctx->path_entry)), sizeof(c->remote_path));
    c->use_key = gtk_check_button_get_active(ctx->use_key_check);
    g_strlcpy(c->key_path, gtk_editable_get_text(GTK_EDITABLE(ctx->key_entry)), sizeof(c->key_path));
}

static void on_sftp_save(GtkButton *btn, gpointer data) {
    (void)btn;
    SftpCtx *ctx = data;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(ctx->name_entry));
    if (!name[0]) return;
    if (ctx->selected_idx >= 0 && ctx->selected_idx < ctx->conns.count) {
        sftp_save_form_to_conn(ctx, &ctx->conns.items[ctx->selected_idx]);
    } else if (ctx->conns.count < MAX_CONNECTIONS) {
        int idx = ctx->conns.count++;
        memset(&ctx->conns.items[idx], 0, sizeof(SftpConnection));
        sftp_save_form_to_conn(ctx, &ctx->conns.items[idx]);
        ctx->selected_idx = idx;
    }
    connections_save(&ctx->conns);
    sftp_populate_list(ctx);
}

static void sftp_clear_form(SftpCtx *ctx);

static void on_sftp_delete(GtkButton *btn, gpointer data) {
    (void)btn;
    SftpCtx *ctx = data;
    if (ctx->selected_idx < 0 || ctx->selected_idx >= ctx->conns.count) return;
    for (int i = ctx->selected_idx; i < ctx->conns.count - 1; i++)
        ctx->conns.items[i] = ctx->conns.items[i + 1];
    ctx->conns.count--;
    connections_save(&ctx->conns);
    sftp_populate_list(ctx);
    sftp_clear_form(ctx);
}

static void sftp_clear_form(SftpCtx *ctx) {
    ctx->selected_idx = -1;
    gtk_list_box_unselect_all(ctx->conn_list);
    gtk_editable_set_text(GTK_EDITABLE(ctx->name_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(ctx->host_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(ctx->port_entry), "22");
    gtk_editable_set_text(GTK_EDITABLE(ctx->user_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(ctx->path_entry), "/");
    gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(ctx->key_entry), "");
    gtk_check_button_set_active(ctx->use_key_check, FALSE);
    sftp_update_auth_visibility(ctx);
    gtk_widget_grab_focus(GTK_WIDGET(ctx->name_entry));
}

static void on_sftp_new(GtkButton *btn, gpointer data) {
    (void)btn;
    sftp_clear_form(data);
}

typedef struct {
    SftpCtx    *ctx;
    GPtrArray  *argv;
    char        host[256];
    char        user[128];
    int         port;
    char        key[1024];
    char        remote[1024];
    GtkWidget  *connect_btn;
} ConnectTaskData;

static void connect_task_data_free(gpointer p) {
    ConnectTaskData *d = p;
    if (d->argv) g_ptr_array_unref(d->argv);
    g_free(d);
}

static void ssh_connect_thread(GTask *task, gpointer src, gpointer data,
                                GCancellable *cancel) {
    (void)src; (void)cancel;
    ConnectTaskData *d = data;
    g_ptr_array_add(d->argv, NULL);

    char *stdout_buf = NULL;
    GError *err = NULL;
    gint status = 0;
    gboolean ok = g_spawn_sync(
        NULL, (char **)d->argv->pdata, NULL,
        G_SPAWN_SEARCH_PATH,
        NULL, NULL, &stdout_buf, NULL, &status, &err);
    g_free(stdout_buf);

    if (!ok) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
            "SSH failed: %s", err ? err->message : "unknown");
        if (err) g_error_free(err);
        return;
    }
    if (!g_spawn_check_wait_status(status, NULL)) {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
            "SSH connection failed (exit %d).\nCheck hostname, credentials, and SSH key.", code);
        return;
    }
    g_task_return_boolean(task, TRUE);
}

static void ssh_connect_done(GObject *src, GAsyncResult *res, gpointer data) {
    (void)src;
    ConnectTaskData *d = data;
    SftpCtx *ctx = d->ctx;
    GError *err = NULL;

    if (!g_task_propagate_boolean(G_TASK(res), &err)) {
        if (ctx->dialog_alive) {
            gtk_widget_set_sensitive(d->connect_btn, TRUE);
            gtk_button_set_label(GTK_BUTTON(d->connect_btn), "Connect");
            GtkAlertDialog *alert = gtk_alert_dialog_new("%s", err->message);
            gtk_alert_dialog_show(alert, ctx->dialog);
            g_object_unref(alert);
        }
        g_error_free(err);
        sftp_ctx_unref(ctx);
        return;
    }

    notes_window_ssh_connect(ctx->win, d->host, d->user, d->port, d->key, d->remote);
    if (ctx->dialog_alive)
        gtk_window_destroy(GTK_WINDOW(ctx->dialog));
    sftp_ctx_unref(ctx);
}

static void on_sftp_connect(GtkButton *btn, gpointer data) {
    SftpCtx *ctx = data;
    const char *host = gtk_editable_get_text(GTK_EDITABLE(ctx->host_entry));
    const char *user = gtk_editable_get_text(GTK_EDITABLE(ctx->user_entry));
    const char *remote = gtk_editable_get_text(GTK_EDITABLE(ctx->path_entry));
    const char *port_str = gtk_editable_get_text(GTK_EDITABLE(ctx->port_entry));
    gboolean use_key = gtk_check_button_get_active(ctx->use_key_check);

    if (!host[0] || !user[0]) return;
    if (!remote[0]) remote = "/";

    int port = atoi(port_str[0] ? port_str : "22");
    if (port <= 0) port = 22;

    gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
    gtk_button_set_label(btn, "Connecting...");

    GPtrArray *av = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(av, g_strdup("ssh"));
    g_ptr_array_add(av, g_strdup("-p"));
    g_ptr_array_add(av, g_strdup_printf("%d", port));
    g_ptr_array_add(av, g_strdup("-o"));
    g_ptr_array_add(av, g_strdup("StrictHostKeyChecking=accept-new"));
    g_ptr_array_add(av, g_strdup("-o"));
    g_ptr_array_add(av, g_strdup("BatchMode=yes"));
    g_ptr_array_add(av, g_strdup("-o"));
    g_ptr_array_add(av, g_strdup("ConnectTimeout=10"));
    if (use_key) {
        const char *key = gtk_editable_get_text(GTK_EDITABLE(ctx->key_entry));
        if (key[0]) {
            g_ptr_array_add(av, g_strdup("-i"));
            g_ptr_array_add(av, g_strdup(key));
        }
    }
    g_ptr_array_add(av, g_strdup_printf("%s@%s", user, host));
    g_ptr_array_add(av, g_strdup("--"));
    g_ptr_array_add(av, g_strdup("echo"));
    g_ptr_array_add(av, g_strdup("ok"));

    ConnectTaskData *td = g_new0(ConnectTaskData, 1);
    td->ctx = sftp_ctx_ref(ctx);
    td->argv = av;
    td->connect_btn = GTK_WIDGET(btn);
    g_strlcpy(td->host, host, sizeof(td->host));
    g_strlcpy(td->user, user, sizeof(td->user));
    td->port = port;
    if (use_key) {
        const char *key = gtk_editable_get_text(GTK_EDITABLE(ctx->key_entry));
        g_strlcpy(td->key, key, sizeof(td->key));
    }
    g_strlcpy(td->remote, remote, sizeof(td->remote));

    GTask *task = g_task_new(NULL, NULL, ssh_connect_done, td);
    g_task_set_task_data(task, td, connect_task_data_free);
    g_task_run_in_thread(task, ssh_connect_thread);
    g_object_unref(task);
}

static void on_sftp_dialog_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    SftpCtx *ctx = data;
    ctx->dialog_alive = FALSE;
    sftp_ctx_unref(ctx);
}

void on_sftp_dialog(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    SftpCtx *ctx = g_new0(SftpCtx, 1);
    ctx->win = win;
    ctx->selected_idx = -1;
    ctx->ref_count = 1;
    ctx->dialog_alive = TRUE;
    connections_load(&ctx->conns);

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "SFTP/SSH Connection");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 460);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());
    ctx->dialog = GTK_WINDOW(dialog);

    g_signal_connect(dialog, "destroy", G_CALLBACK(on_sftp_dialog_destroy), ctx);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox, 12);
    gtk_widget_set_margin_top(hbox, 12);
    gtk_widget_set_margin_bottom(hbox, 12);
    gtk_window_set_child(GTK_WINDOW(dialog), hbox);

    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(left_box, 160, -1);

    GtkWidget *list_label = gtk_label_new("Connections");
    gtk_label_set_xalign(GTK_LABEL(list_label), 0);
    gtk_box_append(GTK_BOX(left_box), list_label);

    GtkWidget *list_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(list_scroll, TRUE);
    ctx->conn_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(ctx->conn_list, GTK_SELECTION_SINGLE);
    g_signal_connect(ctx->conn_list, "row-activated", G_CALLBACK(on_conn_selected), ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroll), GTK_WIDGET(ctx->conn_list));
    gtk_box_append(GTK_BOX(left_box), list_scroll);

    gtk_box_append(GTK_BOX(hbox), left_box);
    gtk_box_append(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(right_box, TRUE);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    int row = 0;

    gtk_grid_attach(GTK_GRID(grid), make_label("Name:"), 0, row, 1, 1);
    ctx->name_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(ctx->name_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->name_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("Host:"), 0, row, 1, 1);
    ctx->host_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(ctx->host_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->host_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("Port:"), 0, row, 1, 1);
    ctx->port_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(ctx->port_entry), "22");
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->port_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("User:"), 0, row, 1, 1);
    ctx->user_entry = GTK_ENTRY(gtk_entry_new());
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->user_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("Remote Path:"), 0, row, 1, 1);
    ctx->path_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(ctx->path_entry), "/");
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->path_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("Use Private Key:"), 0, row, 1, 1);
    ctx->use_key_check = GTK_CHECK_BUTTON(gtk_check_button_new());
    g_signal_connect(ctx->use_key_check, "toggled", G_CALLBACK(on_use_key_toggled), ctx);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->use_key_check), 1, row++, 2, 1);

    GtkWidget *pass_lbl = make_label("Password:");
    gtk_grid_attach(GTK_GRID(grid), pass_lbl, 0, row, 1, 1);
    ctx->password_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_visibility(ctx->password_entry, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(ctx->password_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->password_entry), 1, row++, 2, 1);
    ctx->password_row = pass_lbl;

    GtkWidget *key_lbl = make_label("Key File:");
    gtk_grid_attach(GTK_GRID(grid), key_lbl, 0, row, 1, 1);
    ctx->key_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(ctx->key_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->key_entry), 1, row, 1, 1);
    ctx->key_row = key_lbl;

    ctx->key_browse_btn = gtk_button_new_with_label("...");
    g_signal_connect(ctx->key_browse_btn, "clicked", G_CALLBACK(on_key_browse), ctx);
    gtk_grid_attach(GTK_GRID(grid), ctx->key_browse_btn, 2, row++, 1, 1);
    ctx->key_btn_row = ctx->key_browse_btn;

    sftp_update_auth_visibility(ctx);
    gtk_box_append(GTK_BOX(right_box), grid);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btn_box, 12);
    gtk_widget_set_valign(btn_box, GTK_ALIGN_END);
    gtk_widget_set_vexpand(btn_box, TRUE);

    GtkWidget *new_btn = gtk_button_new_with_label("New");
    g_signal_connect(new_btn, "clicked", G_CALLBACK(on_sftp_new), ctx);
    gtk_box_append(GTK_BOX(btn_box), new_btn);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(del_btn, "destructive-action");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_sftp_delete), ctx);
    gtk_box_append(GTK_BOX(btn_box), del_btn);

    GtkWidget *save_btn = gtk_button_new_with_label("Save");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_sftp_save), ctx);
    gtk_box_append(GTK_BOX(btn_box), save_btn);

    GtkWidget *connect_btn = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(connect_btn, "suggested-action");
    g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_sftp_connect), ctx);
    gtk_box_append(GTK_BOX(btn_box), connect_btn);

    gtk_box_append(GTK_BOX(right_box), btn_box);
    gtk_box_append(GTK_BOX(hbox), right_box);

    sftp_populate_list(ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ── Open Remote File browser dialog ── */

typedef struct {
    NotesWindow *win;
    GtkWindow   *dialog;
    GtkLabel    *path_label;
    GtkListBox  *file_list;
    char         current_dir[4096];
} OpenRemoteCtx;

static void remote_browse_populate(OpenRemoteCtx *ctx);

static void on_remote_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    OpenRemoteCtx *ctx = data;
    if (!row) return;

    GtkWidget *lbl = gtk_widget_get_first_child(GTK_WIDGET(row));
    if (!lbl) return;
    const char *name = gtk_label_get_text(GTK_LABEL(lbl));
    if (!name || !name[0]) return;

    size_t nlen = strlen(name);

    if (strcmp(name, "..") == 0) {
        size_t dlen = strlen(ctx->current_dir);
        if (dlen > 1 && ctx->current_dir[dlen - 1] == '/')
            ctx->current_dir[dlen - 1] = '\0';
        char *last = strrchr(ctx->current_dir, '/');
        if (last && last != ctx->current_dir)
            *(last + 1) = '\0';
        else
            g_strlcpy(ctx->current_dir, "/", sizeof(ctx->current_dir));
        remote_browse_populate(ctx);
        return;
    }

    if (name[nlen - 1] == '/') {
        size_t dlen = strlen(ctx->current_dir);
        if (dlen > 0 && ctx->current_dir[dlen - 1] != '/')
            g_strlcat(ctx->current_dir, "/", sizeof(ctx->current_dir));
        g_strlcat(ctx->current_dir, name, sizeof(ctx->current_dir));
        remote_browse_populate(ctx);
        return;
    }

    char full_path[4096];
    size_t dlen = strlen(ctx->current_dir);
    if (dlen > 0 && ctx->current_dir[dlen - 1] == '/')
        snprintf(full_path, sizeof(full_path), "%s%s", ctx->current_dir, name);
    else
        snprintf(full_path, sizeof(full_path), "%s/%s", ctx->current_dir, name);

    notes_window_open_remote_file(ctx->win, full_path);
    gtk_window_destroy(GTK_WINDOW(ctx->dialog));
}

typedef struct {
    GPtrArray *argv;
    char      *stdout_buf;
    gboolean   ok;
} BrowseTaskData;

static void browse_task_data_free(gpointer p) {
    BrowseTaskData *d = p;
    if (d->argv) g_ptr_array_unref(d->argv);
    g_free(d->stdout_buf);
    g_free(d);
}

static void browse_thread(GTask *task, gpointer src, gpointer data,
                           GCancellable *cancel) {
    (void)src; (void)cancel;
    BrowseTaskData *d = data;
    d->ok = ssh_spawn_sync(d->argv, &d->stdout_buf, NULL);
    g_task_return_boolean(task, TRUE);
}

static void browse_done(GObject *src, GAsyncResult *res, gpointer data) {
    (void)src;
    OpenRemoteCtx *ctx = data;
    BrowseTaskData *d = g_task_get_task_data(G_TASK(res));

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ctx->file_list))))
        gtk_list_box_remove(ctx->file_list, child);

    if (strcmp(ctx->current_dir, "/") != 0) {
        GtkWidget *lbl = gtk_label_new("..");
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 8);
        gtk_widget_set_margin_top(lbl, 2);
        gtk_widget_set_margin_bottom(lbl, 2);
        gtk_list_box_append(ctx->file_list, lbl);
    }

    if (!d->ok || !d->stdout_buf) {
        GtkWidget *lbl = gtk_label_new("(failed to list directory)");
        gtk_list_box_append(ctx->file_list, lbl);
        return;
    }

    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    char **lines = g_strsplit(d->stdout_buf, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        if (lines[i][0] == '\0') continue;
        size_t len = strlen(lines[i]);
        if (lines[i][len - 1] == '/')
            g_ptr_array_add(dirs, g_strdup(lines[i]));
        else
            g_ptr_array_add(files, g_strdup(lines[i]));
    }
    g_strfreev(lines);

    for (guint i = 0; i < dirs->len; i++) {
        const char *name = g_ptr_array_index(dirs, i);
        GtkWidget *lbl = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 8);
        gtk_widget_set_margin_top(lbl, 2);
        gtk_widget_set_margin_bottom(lbl, 2);
        gtk_list_box_append(ctx->file_list, lbl);
    }

    for (guint i = 0; i < files->len; i++) {
        const char *name = g_ptr_array_index(files, i);
        GtkWidget *lbl = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 8);
        gtk_widget_set_margin_top(lbl, 2);
        gtk_widget_set_margin_bottom(lbl, 2);
        gtk_list_box_append(ctx->file_list, lbl);
    }

    g_ptr_array_unref(dirs);
    g_ptr_array_unref(files);
}

static void remote_browse_populate(OpenRemoteCtx *ctx) {
    char label_text[512];
    snprintf(label_text, sizeof(label_text), "%s@%s:%s",
             ctx->win->ssh_user, ctx->win->ssh_host, ctx->current_dir);
    gtk_label_set_text(ctx->path_label, label_text);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ctx->file_list))))
        gtk_list_box_remove(ctx->file_list, child);

    GtkWidget *loading = gtk_label_new("Loading...");
    gtk_list_box_append(ctx->file_list, loading);

    GPtrArray *av = ssh_argv_new(ctx->win->ssh_host, ctx->win->ssh_user,
                                  ctx->win->ssh_port, ctx->win->ssh_key,
                                  ctx->win->ssh_ctl_path);
    g_ptr_array_add(av, g_strdup("--"));
    g_ptr_array_add(av, g_strdup("ls"));
    g_ptr_array_add(av, g_strdup("-1pA"));
    g_ptr_array_add(av, g_strdup(ctx->current_dir));

    BrowseTaskData *d = g_new0(BrowseTaskData, 1);
    d->argv = av;

    GTask *task = g_task_new(NULL, NULL, browse_done, ctx);
    g_task_set_task_data(task, d, browse_task_data_free);
    g_task_run_in_thread(task, browse_thread);
    g_object_unref(task);
}

static void on_open_remote_destroy(GtkWidget *w, gpointer data) {
    (void)w;
    g_free(data);
}

static gboolean on_open_remote_key(GtkEventControllerKey *ctrl, guint keyval,
                                    guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state;
    OpenRemoteCtx *ctx = data;
    if (keyval == GDK_KEY_Escape) {
        gtk_window_destroy(GTK_WINDOW(ctx->dialog));
        return TRUE;
    }
    return FALSE;
}

void on_open_remote(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    OpenRemoteCtx *ctx = g_new0(OpenRemoteCtx, 1);
    ctx->win = win;
    g_strlcpy(ctx->current_dir, win->ssh_remote_path, sizeof(ctx->current_dir));

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Open Remote File");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, 500);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());
    ctx->dialog = GTK_WINDOW(dialog);

    g_signal_connect(dialog, "destroy", G_CALLBACK(on_open_remote_destroy), ctx);

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_open_remote_key), ctx);
    gtk_widget_add_controller(dialog, key);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    ctx->path_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(ctx->path_label, 0);
    gtk_label_set_ellipsize(ctx->path_label, PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(vbox), GTK_WIDGET(ctx->path_label));

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    ctx->file_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(ctx->file_list, GTK_SELECTION_SINGLE);
    g_signal_connect(ctx->file_list, "row-activated", G_CALLBACK(on_remote_row_activated), ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(ctx->file_list));
    gtk_box_append(GTK_BOX(vbox), scroll);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    remote_browse_populate(ctx);

    gtk_window_present(GTK_WINDOW(dialog));
}

void on_sftp_disconnect(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    notes_window_ssh_disconnect(win);
}
