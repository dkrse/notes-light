# Architecture

## Overview

Notes Light is a single-window GTK 4 text editor with SSH/SFTP remote file editing. No database, no sidebar, no markdown preview. Five source files, one Makefile.

## File Structure

```
data/
  language-specs/
    asm.lang    Custom Assembly language definition (x86/ARM)
src/
  main.c        Entry point, GtkApplication, GtkSourceView init, title restore idle
  window.h      NotesWindow struct, public API
  window.c      Core UI: source view (GtkSourceView), line numbers, themes, CSS,
                syntax highlighting, file I/O, search/replace, go-to-line,
                SSH state, remote file open/save
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
      → build UI (headerbar, source view, line numbers, status bar, search bar)
      → actions_setup (GActions + keyboard accels)
      → notes_window_apply_settings (theme, CSS, font, line spacing, intensity)
      → notes_window_load_file (restore last file)
    → gtk_window_present
    → restore_title_cb (idle — re-set title after theme settles)

Editing:
  keystroke → GtkTextBuffer "changed" signal
    → update_dirty_state (FNV-1a hash compare; full strcmp only on hash match;
        set dirty + " *" if different, clear dirty if same)
    → update_line_numbers (idle scheduled)
    → update_cursor_position (status bar Ln/Col)
    → update_line_highlights (queue_draw)
    → intensity_idle_cb (if intensity < 1.0, re-tag entire buffer)
    → scroll_idle_cb (scroll to cursor)

File Load (local):
  fopen("rb") → fread (max 5 MB) → NUL→'.' → g_utf8_validate
    → g_convert_with_fallback if needed
    → apply_source_language (detect from filename) → apply_source_style
    → block signals → set_text → set state → unblock signals
    → apply_font_intensity

File Load (remote):
  ssh_cat_file (GSubprocess: ssh cat remote_path)
    → same binary/UTF-8 handling as local
    → virtual path stored as /tmp/note-light-sftp-PID-user@host/path

Save (local):
  get buffer text → g_mkstemp (exclusive tmp) → fputs → fflush → rename over original

Save (remote):
  ssh_write_file (GSubprocess: ssh tee remote_path < stdin)

Search:
  on_search_changed → search_highlight_all (forward_search loop)
    → apply search-match tag to all matches
    → collect match line numbers + byte offsets for scrollbar markers and O(1) navigation
    → draw_scrollbar_markers (GtkDrawingArea overlay on scrollbar)
  Enter/Shift+Enter → search_goto_match (jump via stored offset, O(1))

SFTP Connect:
  SFTP dialog → form (host/port/user/key/path) → GTask ssh_connect_thread
    → ssh user@host -- echo ok (async test)
    → notes_window_ssh_connect → ssh_ctl_start (ControlMaster)

Remote File Browser:
  Open Remote File dialog → ssh ls -1pA /path (async via GTask + ControlMaster)
    → show "Loading..." → populate GtkListBox (dirs first, then files)
    → click dir → navigate, click file → notes_window_open_remote_file

Close:
  on_close_request
    → if dirty: show GtkAlertDialog (Save / Don't Save / Cancel)
      → Save: auto_save_current → close_and_cleanup
      → Don't Save: discard → close_and_cleanup
      → Cancel: stay open
    → if clean: close_and_cleanup
  close_and_cleanup → ssh_disconnect if active
    → sync last_file (skip remote paths) → settings_save → destroy
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
All file writes (content, settings, connection profiles) use exclusive tmp file via `g_mkstemp()` + rename pattern. The exclusive creation (`O_EXCL`) prevents symlink attacks on the tmp file. A crash during write never corrupts the original file.

### CSS Injection Protection
Font names from config are sanitized via `css_escape_font()` which strips `} { ; " ' \` before embedding into CSS strings.

### Syntax Highlighting
Uses GtkSourceView 5. The text view is a custom subclass of GtkSourceView (instead of GtkTextView) to preserve the current line highlight overlay. Language is auto-detected from the filename via `gtk_source_language_manager_guess_language` before text is loaded into the buffer — this ensures highlighting is applied immediately, including on startup when restoring the last file. Custom language definitions (e.g. Assembly) are loaded from `data/language-specs/` next to the executable. When syntax highlighting is enabled, CSS omits `color:` from `textview text` so the style scheme controls syntax colors. Each app theme maps to a GtkSourceView style scheme (e.g. solarized→solarized, monokai→oblivion, nord→cobalt). Toggled via Settings checkbox; state persisted in settings.conf.

### Theme System
13 built-in themes. Custom themes define fg/bg colors and full CSS for headerbar, popover, status bar, line numbers, dialog widgets (entry, label, list, button, separator, scrolledwindow). System/light/dark themes use AdwStyleManager color scheme with minimal CSS. All dialogs use AdwHeaderBar for consistent theming. When syntax highlighting is on, text foreground color is controlled by the GtkSourceView style scheme rather than CSS.

### Font Intensity
When syntax highlighting is off: applied as a text tag with alpha-channel foreground color on the entire buffer. When syntax highlighting is on: applied as CSS opacity on the text view widget (preserving syntax colors). Re-applied after file load and on buffer changes via idle callback.

### Search with Scrollbar Markers
Search highlights all matches with a GtkTextTag. Match line numbers and byte offsets are collected during the highlight pass. Navigation to a specific match uses stored offsets via `gtk_text_buffer_get_iter_at_offset` for O(1) jumps instead of re-scanning from the start. Match positions are drawn as orange markers on a GtkDrawingArea overlay positioned at the right edge of the scrolled window.

### SSH/SFTP Without External Libraries
Uses the system `ssh` command via `g_spawn_sync` / `GSubprocess`. No libssh/libssh2 dependency. SSH ControlMaster multiplexes all commands through a single TCP connection (near-zero overhead per command). Connection test and remote directory listing run async via GTask to avoid blocking the UI. The SFTP dialog uses ref-counted `SftpCtx` with a `dialog_alive` flag to prevent use-after-free when the dialog closes during an in-flight async connect. Remote file paths use a virtual mount prefix (`/tmp/note-light-sftp-PID-user@host`) for `ssh_path_is_remote()` detection.

### Remote File Save
Uses `ssh tee remote_path` with content piped via GSubprocess stdin. Binary-safe.

### Connection Profiles
Saved in `~/.config/notes-light/connections.conf` (INI format, 0600 permissions). Passwords are never saved — only key paths are persisted.

### Smart Dirty Detection
`update_dirty_state` uses FNV-1a 32-bit hash to quickly detect whether the buffer has changed. On each keystroke, the current buffer is hashed and compared with `original_hash`. Only when hashes match is the full `strcmp` performed to rule out false positives. This avoids O(n) string comparison on every keystroke for the common case where the content has changed. If the user undoes all changes (Ctrl+Z back to original), the dirty flag clears automatically and the `*` marker is removed from the title.

### Save Confirmation on Close
When closing with unsaved changes, a `GtkAlertDialog` presents three options: Save, Don't Save, Cancel. The close request returns TRUE to block the default close until the user responds. Clean files close immediately without a prompt.

### SSH Action State Management
"Open Remote File" and "SFTP Disconnect" actions are disabled at startup and enabled/disabled via `g_simple_action_set_enabled` in `update_ssh_status`, which runs after connect and disconnect. This greys out menu items that require an active SSH connection.

## NotesWindow Struct

Central state object, heap-allocated via `g_new0`. Holds:
- GTK widget pointers (window, source_view, source_buffer, text_view/buffer aliases, line_numbers, labels)
- Settings struct (inline, not pointer)
- CSS provider
- File state (current_file path, dirty flag, is_binary, is_truncated, original_content, original_hash)
- Idle source IDs (line_numbers, intensity, scroll, title)
- Current line highlight state (line number, RGBA color)
- SSH state (host, user, port, key, remote_path, mount, ctl_path, ctl_dir, status button)
- Search state (search_bar, entries, search_tag, match_lines, match_offsets, match_count, scrollbar_overlay)

Freed in `on_destroy` after all idle sources are cancelled and SSH is disconnected.

## Settings Persistence

Simple key=value format in `~/.config/notes-light/settings.conf`. Locale-safe float parsing (comma→dot replacement). Validated ranges on load (font_size 6-72, window dimensions 200-8192, etc.). File permissions 0600.

SSH connection profiles stored separately in `~/.config/notes-light/connections.conf` (INI format with `[name]` sections, up to 32 profiles).
