# Changelog

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
- Syntax highlighting
- Pack notes (archive)
- Markdown preview
