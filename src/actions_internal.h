#ifndef NOTES_ACTIONS_INTERNAL_H
#define NOTES_ACTIONS_INTERNAL_H

#include "actions.h"

/* File actions */
void on_new_file(GSimpleAction *action, GVariant *param, gpointer data);
void on_save(GSimpleAction *action, GVariant *param, gpointer data);
void on_save_as(GSimpleAction *action, GVariant *param, gpointer data);
void on_open_file(GSimpleAction *action, GVariant *param, gpointer data);
void on_open_recent(GSimpleAction *action, GVariant *param, gpointer data);
void on_print(GSimpleAction *action, GVariant *param, gpointer data);

/* View / editor actions */
void on_find(GSimpleAction *action, GVariant *param, gpointer data);
void on_find_replace(GSimpleAction *action, GVariant *param, gpointer data);
void on_goto_line(GSimpleAction *action, GVariant *param, gpointer data);
void on_zoom_in(GSimpleAction *action, GVariant *param, gpointer data);
void on_zoom_out(GSimpleAction *action, GVariant *param, gpointer data);
void on_settings(GSimpleAction *action, GVariant *param, gpointer data);
void on_undo(GSimpleAction *action, GVariant *param, gpointer data);
void on_redo(GSimpleAction *action, GVariant *param, gpointer data);

/* SFTP/SSH actions */
void on_sftp_dialog(GSimpleAction *action, GVariant *param, gpointer data);
void on_open_remote(GSimpleAction *action, GVariant *param, gpointer data);
void on_sftp_disconnect(GSimpleAction *action, GVariant *param, gpointer data);

#endif
