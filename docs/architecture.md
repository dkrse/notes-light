# Architecture

## Overview

Notes Light is a single-window GTK 4 text editor. No database, no sidebar, no markdown preview. Four source files, one Makefile, ~1200 lines of C.

## File Structure

```
src/
  main.c        Entry point, GtkApplication, title restore idle
  window.h      NotesWindow struct, public API
  window.c      Core UI: text view, line numbers, themes, CSS, file I/O
  settings.h    NotesSettings struct
  settings.c    Key=value config file parser/writer
  actions.h     actions_setup declaration
  actions.c     GAction handlers, settings dialog, keyboard shortcuts
```

## Data Flow

```
Startup:
  main → AdwApplication → on_activate
    → notes_window_new
      → settings_load (key=value from disk)
      → build UI (headerbar, text view, line numbers, status bar)
      → actions_setup (GActions + keyboard accels)
      → notes_window_apply_settings (theme, CSS, font, line spacing)
      → notes_window_load_file (restore last file)
    → gtk_window_present
    → restore_title_cb (idle — re-set title after theme settles)

Editing:
  keystroke → GtkTextBuffer "changed" signal
    → update_dirty_state (title += " *")
    → update_line_numbers (idle scheduled)
    → update_cursor_position (status bar Ln/Col)
    → update_line_highlights (queue_draw)
    → intensity_idle_cb (if intensity < 1.0, tag visible range)
    → scroll_idle_cb (scroll to cursor)

File Load:
  fopen("rb") → fread (max 5 MB) → NUL→'.' → g_utf8_validate
    → g_convert_with_fallback if needed
    → block signals → set_text → set state → unblock signals

Save:
  get buffer text → fopen tmp → fputs → fflush → rename over original

Close:
  on_close_request → auto_save_current → sync last_file → settings_save
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
13 built-in themes. Custom themes define fg/bg colors and full CSS for headerbar, popover, status bar, line numbers. System/light/dark themes use AdwStyleManager color scheme with minimal CSS.

## NotesWindow Struct

Central state object, heap-allocated via `g_new0`. Holds:
- GTK widget pointers (window, text_view, buffer, line_numbers, labels)
- Settings struct (inline, not pointer)
- CSS provider
- File state (current_file path, dirty flag, is_binary, is_truncated, original_content)
- Idle source IDs (line_numbers, intensity, scroll, title)
- Current line highlight state (line number, RGBA color)

Freed in `on_destroy` after all idle sources are cancelled.

## Settings Persistence

Simple key=value format in `~/.config/notes-light/settings.conf`. Locale-safe float parsing (comma→dot replacement). Validated ranges on load (font_size 6-72, window dimensions 200-8192, etc.). File permissions 0600.
