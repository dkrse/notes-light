#include "actions_internal.h"
#include "editor_view.h"

static void on_toggle_whitespace(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)param;
    NotesWindow *win = data;
    GVariant *state = g_action_get_state(G_ACTION(action));
    gboolean new_state = !g_variant_get_boolean(state);
    g_variant_unref(state);
    g_simple_action_set_state(action, g_variant_new_boolean(new_state));
    win->settings.show_whitespace = new_state;
    editor_view_apply_whitespace(win);
    settings_save(&win->settings);
}

void actions_setup(NotesWindow *win, GtkApplication *app) {
    static const GActionEntry win_entries[] = {
        {"new-file",        on_new_file,       NULL, NULL, NULL, {0}},
        {"save",            on_save,           NULL, NULL, NULL, {0}},
        {"save-as",         on_save_as,        NULL, NULL, NULL, {0}},
        {"open-file",       on_open_file,      NULL, NULL, NULL, {0}},
        {"settings",        on_settings,       NULL, NULL, NULL, {0}},
        {"find",            on_find,           NULL, NULL, NULL, {0}},
        {"find-replace",    on_find_replace,   NULL, NULL, NULL, {0}},
        {"goto-line",       on_goto_line,      NULL, NULL, NULL, {0}},
        {"sftp-connect",    on_sftp_dialog,    NULL, NULL, NULL, {0}},
        {"sftp-disconnect", on_sftp_disconnect, NULL, NULL, NULL, {0}},
        {"open-remote",     on_open_remote,    NULL, NULL, NULL, {0}},
        {"zoom-in",         on_zoom_in,        NULL, NULL, NULL, {0}},
        {"zoom-out",        on_zoom_out,       NULL, NULL, NULL, {0}},
        {"undo",            on_undo,           NULL, NULL, NULL, {0}},
        {"redo",            on_redo,           NULL, NULL, NULL, {0}},
        {"print",           on_print,          NULL, NULL, NULL, {0}},
        {"open-recent",     on_open_recent,    "s",  NULL, NULL, {0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(win->window),
                                   win_entries, G_N_ELEMENTS(win_entries), win);

    /* Stateful toggle action for whitespace visibility (renders as check item) */
    GSimpleAction *ws = g_simple_action_new_stateful("toggle-whitespace", NULL,
        g_variant_new_boolean(win->settings.show_whitespace));
    g_signal_connect(ws, "activate", G_CALLBACK(on_toggle_whitespace), win);
    g_action_map_add_action(G_ACTION_MAP(win->window), G_ACTION(ws));
    g_object_unref(ws);

    const char *zoom_in_accels[]  = {"<Control>plus", "<Control>equal", NULL};
    const char *zoom_out_accels[] = {"<Control>minus", NULL};
    const char *quit_accels[]     = {"<Control>q", NULL};
    const char *open_accels[]     = {"<Control>o", NULL};
    const char *save_accels[]     = {"<Control>s", NULL};
    const char *new_accels[]      = {"<Control>n", NULL};
    const char *save_as_accels[]  = {"<Control><Shift>s", NULL};
    const char *find_accels[]     = {"<Control>f", NULL};
    const char *replace_accels[]  = {"<Control>h", NULL};
    const char *goto_accels[]     = {"<Control>g", NULL};
    const char *print_accels[]    = {"<Control>p", NULL};
    const char *undo_accels[]     = {"<Control>z", NULL};
    const char *redo_accels[]     = {"<Control><Shift>z", "<Control>y", NULL};

    gtk_application_set_accels_for_action(app, "win.find",         find_accels);
    gtk_application_set_accels_for_action(app, "win.find-replace", replace_accels);
    gtk_application_set_accels_for_action(app, "win.goto-line",    goto_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-in",      zoom_in_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-out",     zoom_out_accels);
    gtk_application_set_accels_for_action(app, "app.quit",         quit_accels);
    gtk_application_set_accels_for_action(app, "win.open-file",    open_accels);
    gtk_application_set_accels_for_action(app, "win.save",         save_accels);
    gtk_application_set_accels_for_action(app, "win.new-file",     new_accels);
    gtk_application_set_accels_for_action(app, "win.save-as",      save_as_accels);
    gtk_application_set_accels_for_action(app, "win.print",        print_accels);
    gtk_application_set_accels_for_action(app, "win.undo",         undo_accels);
    gtk_application_set_accels_for_action(app, "win.redo",         redo_accels);
}
