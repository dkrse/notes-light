# Changelog

## 1.2.1 (2026-04-15)

### Performance
- Dirty detection uses FNV-1a hash — avoids O(n) strcmp on every keystroke; full comparison only when hashes match
- Search navigation uses stored byte offsets — O(1) jump to any match instead of O(n*k) re-scan from buffer start
- Remote file browser (ls) runs async via GTask — no longer blocks the UI while listing directories

### Fixed
- Use-after-free in SFTP connect dialog when dialog closes during in-flight async SSH connection (ref-counted SftpCtx with dialog_alive flag)
- GotoData memory leak when Go to Line dialog is closed without pressing Enter (freed via g_object_set_data_full on dialog)
- match_offsets array freed in search_clear_matches and on_destroy

### Security
- Atomic file writes use g_mkstemp() (exclusive O_EXCL creation) instead of predictable .tmp suffix — prevents symlink attacks on temp files
- connections_save() now uses atomic write (mkstemp + rename) — crash during save no longer corrupts the file

## 1.2.0 (2026-04-15)

### Added
- Syntax highlighting via GtkSourceView 5 with auto language detection from filename
- Syntax Highlight toggle in Settings dialog (on by default, persisted in settings.conf)
- Theme-aware syntax color schemes — each app theme maps to a matching GtkSourceView style scheme (solarized→solarized, monokai→oblivion, nord→cobalt, gruvbox→classic-dark/kate, etc.)
- Custom Assembly language definition (data/language-specs/asm.lang) with x86/ARM registers, instructions, NASM/MASM/GAS directives
- Makefile highlighting support (built-in GtkSourceView language + basename fallback detection)
- GtkSourceView 5 dependency added to Makefile

### Changed
- Text view is now a custom subclass of GtkSourceView instead of GtkTextView
- GtkSourceBuffer replaces GtkTextBuffer (with backward-compatible aliases)
- Language detection and style scheme applied before text is loaded into buffer (fixes highlight missing on startup file restore)
- CSS no longer sets text foreground color when syntax highlighting is on (style scheme controls it)
- Font intensity uses CSS opacity instead of foreground text tag when syntax highlighting is on (preserves syntax colors)

## 1.1.0 (2026-04-15)

### Added
- Find (Ctrl+F) with match highlighting, match count, next/prev (Enter/Shift+Enter)
- Find & Replace (Ctrl+H) with replace one and replace all
- Scrollbar match markers (orange indicators on vertical scrollbar showing match positions)
- Go to Line (Ctrl+G)
- SSH/SFTP remote file editing via system OpenSSH client
  - SFTP Connect dialog with saved connection profiles (host, port, user, key, remote path)
  - Connection profiles persisted in ~/.config/notes-light/connections.conf (0600 permissions)
  - Async connection test via GTask (non-blocking UI)
  - SSH ControlMaster for connection multiplexing (near-zero per-command overhead)
  - Remote file browser dialog (ls -1pA, directory navigation, click to open)
  - Remote file open (ssh cat)
  - Remote file save (ssh tee) — Ctrl+S works for remote files
  - SSH status indicator in status bar
  - Auto-disconnect on window close
  - Remote paths not saved as last_file
- AdwHeaderBar on all dialog windows (Settings, SFTP, Open Remote, Go to Line)
- Full theme CSS for dialog widgets (entry, label, list, button, separator, scrolledwindow, checkbutton, scale)
- Find, Find & Replace, Go to Line in hamburger menu
- Save confirmation dialog on close with unsaved changes (Save / Don't Save / Cancel)
- Smart dirty detection — undo back to original content clears dirty flag and removes `*` from title
- "Open Remote File" and "SFTP Disconnect" menu items disabled when not connected
- Application icon (SVG + PNG) and .desktop launcher

### Fixed
- Font intensity now applies to entire buffer (was only visible range + margin, causing 100% intensity on scroll)
- Font intensity now re-applied after file load (was lost because apply_settings ran before load_file replaced buffer content)

## 1.0.0 (2026-04-14)

Initial release. Stripped-down fork of notes-desktop.

### Added
- Plain text editor with GTK 4 + libadwaita
- 13 color themes (System, Light, Dark, Solarized Light/Dark, Monokai, Gruvbox Light/Dark, Nord, Dracula, Tokyo Night, Catppuccin Latte/Mocha)
- Custom GtkTextView subclass with current line highlight overlay
- Line numbers via Cairo GtkDrawingArea (visible lines only)
- Font intensity control (alpha blending)
- Configurable line spacing and word wrap
- Zoom in/out (Ctrl+/-)
- Status bar: encoding, file type (TEXT/BIN), file size, cursor position (Ln/Col)
- Open any file type including binaries (NUL→'.', non-UTF-8 converted via ISO-8859-1 fallback)
- 5 MB display cap for large files (keeps UI responsive)
- Open File (Ctrl+O), Save (Ctrl+S), Save As (Ctrl+Shift+S), New (Ctrl+N)
- Settings dialog: theme, editor font, GUI font, font intensity, line spacing, line numbers, highlight line, word wrap
- Persistent settings in ~/.config/notes-light/settings.conf
- Auto-restore last opened file on startup
- Auto-save on close (text files only)
- Atomic file writes (tmp + rename) for content and settings
- Signal blocking during file load (prevents dirty state/title corruption)
- Title restore via idle callback after gtk_window_present
- CSS injection protection (font name sanitization)
- Binary/truncated file protection (Save redirects to Save As)
- Idle source cleanup on destroy (prevents use-after-free)

### Removed (vs notes-desktop)
- Markdown rendering (WebKitGTK)
- SQLite FTS5 database
- Sidebar with search
- PDF export (Poppler + Cairo)
- Syntax highlighting (re-added in 1.2.0 via GtkSourceView 5)
- Pack notes (archive)
- Markdown preview
