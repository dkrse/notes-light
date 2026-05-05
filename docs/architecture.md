# Architecture

## Overview

Notes Light is a single-window GTK 4 text editor with SSH/SFTP remote file editing. No database, no sidebar, no markdown preview. After the 1.3.0 refactor the source tree is split into small focused modules.

## File Structure

```
data/
  language-specs/
    asm.lang             Custom Assembly language definition (x86/ARM)
src/
  main.c                 Entry point, AdwApplication, on_activate, file open
  window.h               NotesWindow struct, public window API
  window.c               Core window: notes_window_new, load_file, close/destroy,
                         file monitor (auto-reload), print, recent menu rebuild
  theme.h / theme.c      Custom themes table, CSS generation, syntax color
                         schemes, source language detection, dark-mode helpers
  editor_view.h          NotesTextView (GtkSourceView subclass) + editor API
  editor_view.c          Snapshot (current-line overlay), line-number Cairo,
                         font intensity, dirty detection, buffer signals,
                         whitespace drawer, Ctrl+scroll zoom
  search.h / search.c    Find & Replace bar, scrollbar match markers,
                         Go to Line dialog
  ssh_window.h           SSH window-state API
  ssh_window.c           connect / disconnect, open_remote_file,
                         save_remote_file, status button, action gating
  settings.h             NotesSettings, recent_files, SftpConnection structs
  settings.c             Key=value config parser/writer, recent push,
                         connections load/save
  actions.h              actions_setup declaration
  actions_internal.h     Cross-file action handler declarations
  actions.c              Action registration + accelerators + toggle-whitespace
  actions_file.c         new / open / save / save-as / open-recent / print
  actions_view.c         find / find-replace / goto-line / zoom / settings
                         dialog / undo / redo
  actions_ssh.c          SFTP dialog + saved connection profiles +
                         async remote file browser
  ssh.h / ssh.c          ssh_argv_new, ssh_ctl_start/stop, ssh_cat_file,
                         ssh_write_file, path helpers
```

## Module Layering

```
                main.c
                  │
                  ▼
              window.c ─────────┐
              /  │  \           │
     theme.c    │  search.c    actions.c
                │              /  │  \
        editor_view.c   actions_file.c
                │       actions_view.c
        ssh_window.c    actions_ssh.c
                │
              ssh.c
```

`actions_*.c` and `ssh_window.c` may call into `editor_view.*`, `theme.*`,
`search.*`. The reverse direction is forbidden — UI primitives never reach
back into action handlers.

## Data Flow

```
Startup:
  main → AdwApplication → on_activate
    → notes_window_new
      → settings_load (incl. recent_files)
      → editor_view_create (NotesTextView, line numbers, intensity tag,
                            search tag, buffer signals, Ctrl+scroll)
      → search_build_bar / search_init_scrollbar_overlay
      → build_menu_button (sections: File, Edit, View, SSH, Settings;
                           "Open Recent" submenu)
      → actions_setup (GActions + accelerators)
      → notes_window_recent_menu_rebuild
      → ssh_window_update_status
      → notes_window_apply_settings (theme + CSS + editor + source style)
      → notes_window_load_file (restore last file if present)
    → gtk_window_present

Editing:
  keystroke → GtkTextBuffer "changed" signal (editor_view.c)
    → editor_view_update_dirty_state (FNV-1a hash compare; full strcmp only
        on hash match; clears dirty if buffer matches original_content)
    → editor_view_update_line_numbers (idle scheduled)
    → editor_view_update_cursor_position (status bar Ln/Col)
    → editor_view_update_line_highlights (queue_draw)
    → intensity_idle_cb (re-tag entire buffer if intensity < 1.0)
    → scroll_idle_cb (scroll to cursor)

  Ctrl+mouse wheel on text view (editor_view.c) → font_size ±1pt →
    notes_window_apply_settings → settings_save

File Load (local):
  notes_window_load_file
    → fopen("rb") → fread (max 5 MB) → NUL→'.' → g_utf8_validate
    → g_convert_with_fallback if needed
    → notes_apply_source_language → notes_apply_source_style
    → install GFileMonitor for auto-reload
    → editor_view_block_signals → set_text → set state → unblock
    → settings_push_recent → notes_window_recent_menu_rebuild
    → settings_save

Auto-reload:
  GFileMonitor "changed" event (window.c)
    → ignore non-relevant event types and remote files
    → re-read file, FNV-1a hash compare with original_hash
    → if hash matches: ignore (our own write)
    → if dirty: GtkAlertDialog (Reload / Keep my version)
    → else: reload_keep_cursor (saves line/col, reloads, restores)

File Load (remote):
  notes_window_open_remote_file (ssh_window.c)
    → ssh_cat_file (GSubprocess: ssh cat remote_path)
    → same binary/UTF-8 handling as local
    → virtual path /tmp/note-light-sftp-PID-user@host/path

Save (local):
  on_save (actions_file.c) → g_mkstemp → fputs → fflush → rename
Save (remote):
  on_save → save_remote_file (ssh_window.c) → ssh_write_file (ssh tee)

Print:
  notes_window_print (window.c)
    → GtkPrintOperation, signals begin-print + draw-page
    → on_begin_print: PangoLayout sized to print context width,
       paginate by line height (page_breaks GArray)
    → on_draw_page: render lines [start..end) for current page

Search (search.c):
  on_search_changed → search_highlight_all (forward_search loop)
    → apply search-match tag, collect line numbers + byte offsets
  Enter/Shift+Enter → search_goto_match (O(1) via stored offset)
  draw_scrollbar_markers → orange ticks on right-edge overlay

SFTP Connect (actions_ssh.c):
  Form → GTask ssh_connect_thread → ssh -- echo ok
    → notes_window_ssh_connect → ssh_ctl_start (ControlMaster)

Remote File Browser (actions_ssh.c):
  Open Remote File dialog → GTask ssh ls -1pA /path
    → populate GtkListBox (".." first, then dirs, then files)
    → click dir → navigate; click file → notes_window_open_remote_file

Close:
  on_close_request → if dirty: GtkAlertDialog (Save / Don't Save / Cancel)
    Save: auto_save_current → close_and_cleanup
    Don't Save: discard → close_and_cleanup
    Cancel: stay open
  close_and_cleanup → ssh_disconnect if active → settings_save → destroy
  on_destroy → cancel idle sources → unref file_monitor + recent_menu →
               unref CSS provider → free
```

## Key Design Decisions

### Modular Split (1.3.0)
The original `window.c` (~1600 lines) and `actions.c` (~1190 lines) were
split into focused modules so any single feature is easy to locate and
edit. Public state still lives in one heap struct (`NotesWindow`); modules
expose imperative entry points (`editor_view_*`, `search_*`,
`ssh_window_*`, `notes_apply_*`). No dynamic dispatch or new abstractions
were introduced.

### Signal Blocking During Load
`on_buffer_changed` is blocked during `gtk_text_buffer_set_text` in
`load_file` and `open_remote_file`. Without this, set_text fires "changed"
which would set `dirty=TRUE` and overwrite the title before
`current_file` is updated. Wrapped behind
`editor_view_block_signals` / `_unblock_signals`.

### 5 MB Display Cap
GtkTextBuffer is not designed for 100+ MB files. Large files are truncated
to 5 MB for display. Binary/truncated files are read-only (Save redirects
to Save As).

### Binary File Detection
First 8 KB scanned for NUL bytes. NUL bytes replaced with '.'. Non-UTF-8
content converted via ISO-8859-1 fallback.

### Atomic Writes
All file writes (content, settings, connection profiles) use exclusive
tmp file via `g_mkstemp()` + rename pattern. The exclusive creation
(`O_EXCL`) prevents symlink attacks on the tmp file. A crash during write
never corrupts the original file.

### CSS Injection Protection
Font names from config are sanitized via `css_escape_font()` (in
`theme.c`) which strips `} { ; " ' \` before embedding into CSS strings.

### Syntax Highlighting
Uses GtkSourceView 5. The text view is `NotesTextView`, a
`GtkSourceView` subclass that overrides `snapshot()` to draw the current
line highlight overlay. Language is auto-detected from the filename via
`gtk_source_language_manager_guess_language` before text is loaded into
the buffer — this ensures highlighting is applied immediately, including
on startup when restoring the last file. Custom language definitions
(e.g. Assembly) are loaded from `data/language-specs/` next to the
executable. When syntax highlighting is enabled, CSS omits `color:` from
`textview text` so the style scheme controls syntax colors. Each app
theme maps to a GtkSourceView style scheme (e.g. solarized→solarized,
monokai→oblivion, nord→cobalt). Toggled via Settings checkbox; state
persisted in `settings.conf`.

### Theme System
13 built-in themes. Custom themes define fg/bg colors and full CSS for
headerbar, popover, status bar, line numbers, dialog widgets (entry,
label, list, button, separator, scrolledwindow). System/light/dark themes
use `AdwStyleManager` color scheme with minimal CSS. All dialogs use
`AdwHeaderBar` for consistent theming. When syntax highlighting is on,
text foreground color is controlled by the GtkSourceView style scheme
rather than CSS.

### Show/Hide Whitespace
A stateful boolean GAction `win.toggle-whitespace` flips
`settings.show_whitespace` and calls `editor_view_apply_whitespace`,
which toggles `GtkSourceSpaceDrawer` (space, tab, newline, NBSP at all
locations). State persisted in `settings.conf`.

### Font Intensity
When syntax highlighting is off: applied as a text tag with alpha-channel
foreground color on the entire buffer. When syntax highlighting is on:
applied as CSS opacity on the text view widget (preserving syntax
colors). Re-applied after file load and on buffer changes via idle
callback.

### Search with Scrollbar Markers
Search highlights all matches with a `GtkTextTag`. Match line numbers and
byte offsets are collected during the highlight pass. Navigation uses
stored offsets via `gtk_text_buffer_get_iter_at_offset` for O(1) jumps
instead of re-scanning. Match positions are drawn as orange markers on a
`GtkDrawingArea` overlay positioned at the right edge of the scrolled
window.

### Auto-reload (file monitor)
A `GFileMonitor` is installed on the currently open local file in
`notes_window_load_file`. On a change event we re-read the file and
compute its FNV-1a hash; if it matches `original_hash` we ignore the
event (it's the result of our own atomic save). Otherwise:
- buffer is clean → silent reload via `reload_keep_cursor`, which saves
  the cursor's line/col before `load_file` and restores it after,
  clamped to the new line/column count;
- buffer is dirty → modal `GtkAlertDialog` with **Reload (discard my
  changes)** / **Keep my version**.

Remote files (`ssh_path_is_remote`) are skipped — no monitor on virtual
mount paths.

### Print
`notes_window_print` opens `GtkPrintOperation` with two signals.
`on_begin_print` builds a single `PangoLayout` for the whole buffer
(monospace font from settings, width = print context width, word-char
wrap), then walks the layout's lines accumulating heights to compute
page breaks, stored in a `GArray` of line indices.
`on_draw_page` renders the lines `[page_breaks[n-1] .. page_breaks[n])`
for the current page. The print job name is set to the file's basename.

### Recent Files
`NotesSettings` holds an array of up to `MAX_RECENT_FILES` (10) absolute
paths plus a count. `settings_push_recent()` deduplicates and rotates the
new path to the front (memmove semantics). After each successful
`load_file` the list is updated and persisted as `recent_N=path` lines in
`settings.conf`.

The hamburger menu has an *Open Recent* submenu backed by a `GMenu` held
in `NotesWindow.recent_menu`. `notes_window_recent_menu_rebuild()` clears
and re-populates that submenu with `GMenuItem`s targeting the
`win.open-recent` action with the path as a GVariant string parameter.
The handler simply calls `notes_window_load_file(path)`.

### Undo / Redo
GTK 4's `GtkTextBuffer` exposes a built-in undo/redo stack
(`gtk_text_buffer_undo`, `gtk_text_buffer_redo`,
`gtk_text_buffer_get_can_undo`, `gtk_text_buffer_get_can_redo`).
`win.undo` / `win.redo` actions wrap those calls and have menu entries
plus accelerators (Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y).

### Ctrl+scroll Zoom
`editor_view_create` adds a `GtkEventControllerScroll` (vertical) on the
text view. The handler returns FALSE without Ctrl (normal scroll),
otherwise adjusts `font_size` by ±1pt (clamped 6..72), calls
`notes_window_apply_settings`, and saves. Step is 1pt, finer than the
2pt step used by `Ctrl+=` / `Ctrl+-`.

### SSH/SFTP Without External Libraries
Uses the system `ssh` command via `g_spawn_sync` / `GSubprocess`. No
libssh/libssh2 dependency. SSH ControlMaster multiplexes all commands
through a single TCP connection (near-zero overhead per command).
Connection test and remote directory listing run async via GTask. The
SFTP dialog uses ref-counted `SftpCtx` with a `dialog_alive` flag to
prevent use-after-free when the dialog closes during an in-flight async
connect. Remote file paths use a virtual mount prefix
(`/tmp/note-light-sftp-PID-user@host`) for `ssh_path_is_remote()`
detection.

### Remote File Save
Uses `ssh tee remote_path` with content piped via GSubprocess stdin.
Binary-safe.

### Connection Profiles
Saved in `~/.config/notes-light/connections.conf` (INI format, 0600
permissions). Passwords are never saved — only key paths are persisted.

### Smart Dirty Detection
`editor_view_update_dirty_state` uses FNV-1a 32-bit hash to quickly
detect whether the buffer has changed. On each keystroke, the current
buffer is hashed and compared with `original_hash`. Only when hashes
match is the full `strcmp` performed to rule out false positives. This
avoids O(n) string comparison on every keystroke for the common case
where the content has changed. If the user undoes all changes back to
the original, the dirty flag clears automatically and the `*` marker is
removed from the title.

### Save Confirmation on Close
When closing with unsaved changes, a `GtkAlertDialog` presents three
options: Save, Don't Save, Cancel. The close request returns TRUE to
block the default close until the user responds.

### SSH Action State Management
`win.open-remote` and `win.sftp-disconnect` are disabled at startup and
toggled via `g_simple_action_set_enabled` in
`ssh_window_update_status`, which runs after connect and disconnect.
This greys out menu items that require an active SSH connection.

## NotesWindow Struct

Central state object, heap-allocated via `g_new0`. Holds:
- GTK widget pointers (window, source_view, source_buffer, text_view /
  buffer aliases, line_numbers, status labels, search bar widgets,
  ssh_status_btn, scrolled_window, scrollbar_overlay)
- Inline `NotesSettings`
- CSS provider
- File state (current_file path, dirty flag, is_binary, is_truncated,
  original_content, original_hash)
- Idle source IDs (line_numbers, intensity, scroll, title)
- Current line highlight state (line number, RGBA color)
- Tags (intensity_tag, search_tag)
- SSH state (host, user, port, key, remote_path, mount, ctl_path,
  ctl_dir)
- Search state (match_lines, match_offsets, match_count, match_current)
- `GFileMonitor *file_monitor` — auto-reload watcher for the current
  local file
- `GMenu *recent_menu` — dynamic submenu rebuilt on each load_file

Freed in `on_destroy` after all idle sources are cancelled, the file
monitor is disconnected and unref'd, and SSH is disconnected.

## Settings Persistence

Simple key=value format in `~/.config/notes-light/settings.conf`.
Locale-safe float parsing (comma→dot replacement). Validated ranges on
load (font_size 6-72, window dimensions 200-8192, etc.). File
permissions 0600. Recent files are stored as `recent_0=...` ...
`recent_9=...` lines.

SSH connection profiles stored separately in
`~/.config/notes-light/connections.conf` (INI format with `[name]`
sections, up to 32 profiles).
