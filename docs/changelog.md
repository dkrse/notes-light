# Changelog

## 1.0.0 (2026-04-14)

Initial release. Stripped-down fork of notes-desktop.

### Added
- Plain text editor with GTK 4 + libadwaita
- 13 color themes (System, Light, Dark, Solarized Light/Dark, Monokai, Gruvbox Light/Dark, Nord, Dracula, Tokyo Night, Catppuccin Latte/Mocha)
- Custom GtkTextView subclass with current line highlight overlay
- Line numbers via Cairo GtkDrawingArea (visible lines only)
- Font intensity control (alpha blending on visible range)
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
