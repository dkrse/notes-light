# Architecture

## Overview

Notes Light is a single-window GTK 4 text editor with SSH/SFTP remote file editing. No database, no sidebar, no markdown preview. Five source files, one Makefile.

## File Structure

```
src/
  main.c        Entry point, GtkApplication, title restore idle
  window.h      NotesWindow struct, public API
  window.c      Core UI: text view, line numbers, themes, CSS, file I/O,
                search/replace, go-to-line, SSH state, remote file open/save
  settings.h    NotesSettings struct, SftpConnection/SftpConnections structs
  settings.c    Key=value config file parser/writer, connections load/save
  actions.h     actions_setup declaration
  actions.c     GAction handlers, settings dialog, keyboard shortcuts,
                SFTP connection dialog, remote file browser
  ssh.h         SSH utility declarations
  ssh.c         SSH command execution, ControlMaster, remote file read/write
```

## Data Flow

```
Startup:
  main → AdwApplication → on_activate
    → notes_window_new
      → settings_load (key=value from disk)
      → build UI (headerbar, text view, line numbers, status bar, search bar)
      → actions_setup (GActions + keyboard accels)
      → notes_window_apply_settings (theme, CSS, font, line spacing, intensity)
      → notes_window_load_file (restore last file)
    → gtk_window_present
    → restore_title_cb (idle — re-set title after theme settles)

Editing:
  keystroke → GtkTextBuffer "changed" signal
    → update_dirty_state (title += " *")
    → update_line_numbers (idle scheduled)
    → update_cursor_position (status bar Ln/Col)
    → update_line_highlights (queue_draw)
    → intensity_idle_cb (if intensity < 1.0, re-tag entire buffer)
    → scroll_idle_cb (scroll to cursor)

File Load (local):
  fopen("rb") → fread (max 5 MB) → NUL→'.' → g_utf8_validate
    → g_convert_with_fallback if needed
    → block signals → set_text → set state → unblock signals
    → apply_font_intensity

File Load (remote):
  ssh_cat_file (GSubprocess: ssh cat remote_path)
    → same binary/UTF-8 handling as local
    → virtual path stored as /tmp/note-light-sftp-PID-user@host/path

Save (local):
  get buffer text → fopen tmp → fputs → fflush → rename over original

Save (remote):
  ssh_write_file (GSubprocess: ssh tee remote_path < stdin)

Search:
  on_search_changed → search_highlight_all (forward_search loop)
    → apply search-match tag to all matches
    → collect match line numbers for scrollbar markers
    → draw_scrollbar_markers (GtkDrawingArea overlay on scrollbar)
  Enter/Shift+Enter → search_goto_match (next/prev)

SFTP Connect:
  SFTP dialog → form (host/port/user/key/path) → GTask ssh_connect_thread
    → ssh user@host -- echo ok (async test)
    → notes_window_ssh_connect → ssh_ctl_start (ControlMaster)

Remote File Browser:
  Open Remote File dialog → ssh ls -1pA /path (sync via ControlMaster)
    → populate GtkListBox (dirs first, then files)
    → click dir → navigate, click file → notes_window_open_remote_file

Close:
  on_close_request → auto_save_current → ssh_disconnect if active
    → sync last_file (skip remote paths) → settings_save
    → on_destroy → cancel idle sources → free resources
```

## Key Design Decisions

### Signal Blocking During Load
`on_buffer_changed` is blocked during `gtk_text_buffer_set_text` in `load_file`. Without this, set_text fires "changed" which sets dirty=TRUE and overwrites the title before current_file is set.

### 5 MB Display Cap
GtkTextBuffer is not designed for 100+ MB files. Large files are truncated to 5 MB for display. Binary/truncated files are read-only (Save redirects to Save As).

### Binary File Detection
First 8 KB scanned for NUL bytes. NUL bytes replaced with '.'. Non-UTF-8 content converted via ISO-8859-1 fallback.

### Atomic Writes
All file writes (content, settings) use tmp + rename pattern. A crash during write never corrupts the original file.

### CSS Injection Protection
Font names from config are sanitized via `css_escape_font()` which strips `} { ; " ' \` before embedding into CSS strings.

### Theme System
13 built-in themes. Custom themes define fg/bg colors and full CSS for headerbar, popover, status bar, line numbers, dialog widgets (entry, label, list, button, separator, scrolledwindow). System/light/dark themes use AdwStyleManager color scheme with minimal CSS. All dialogs use AdwHeaderBar for consistent theming.

### Font Intensity
Applied as a text tag with alpha-channel foreground color on the entire buffer. Re-applied after file load and on buffer changes via idle callback.

### Search with Scrollbar Markers
Search highlights all matches with a GtkTextTag. Match line numbers are collected and drawn as orange markers on a GtkDrawingArea overlay positioned at the right edge of the scrolled window.

### SSH/SFTP Without External Libraries
Uses the system `ssh` command via `g_spawn_sync` / `GSubprocess`. No libssh/libssh2 dependency. SSH ControlMaster multiplexes all commands through a single TCP connection (near-zero overhead per command). Connection test runs async via GTask to avoid blocking the UI. Remote file paths use a virtual mount prefix (`/tmp/note-light-sftp-PID-user@host`) for `ssh_path_is_remote()` detection.

### Remote File Save
Uses `ssh tee remote_path` with content piped via GSubprocess stdin. Binary-safe.

### Connection Profiles
Saved in `~/.config/notes-light/connections.conf` (INI format, 0600 permissions). Passwords are never saved — only key paths are persisted.

## NotesWindow Struct

Central state object, heap-allocated via `g_new0`. Holds:
- GTK widget pointers (window, text_view, buffer, line_numbers, labels)
- Settings struct (inline, not pointer)
- CSS provider
- File state (current_file path, dirty flag, is_binary, is_truncated, original_content)
- Idle source IDs (line_numbers, intensity, scroll, title)
- Current line highlight state (line number, RGBA color)
- SSH state (host, user, port, key, remote_path, mount, ctl_path, ctl_dir, status button)
- Search state (search_bar, entries, search_tag, match_lines, match_count, scrollbar_overlay)

Freed in `on_destroy` after all idle sources are cancelled and SSH is disconnected.

## Settings Persistence

Simple key=value format in `~/.config/notes-light/settings.conf`. Locale-safe float parsing (comma→dot replacement). Validated ranges on load (font_size 6-72, window dimensions 200-8192, etc.). File permissions 0600.

SSH connection profiles stored separately in `~/.config/notes-light/connections.conf` (INI format with `[name]` sections, up to 32 profiles).
