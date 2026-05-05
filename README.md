# Notes Light

Fast, minimal text editor written in C with GTK 4 and libadwaita.

## Features

- Plain text editing with no database, no sidebar, no markdown
- Syntax highlighting via GtkSourceView 5 (auto-detected from file extension, toggle in Settings)
- Opens any file including binaries (displayed as text, max 5 MB view)
- 13 color themes (System, Light, Dark, Solarized, Monokai, Gruvbox, Nord, Dracula, Tokyo Night, Catppuccin)
- Theme-aware syntax color schemes (each theme maps to a matching GtkSourceView style scheme)
- Line numbers with Cairo rendering
- Current line highlight (overlay)
- Show/hide whitespace (spaces, tabs, newlines) toggle in menu
- Font intensity (alpha blending)
- Configurable line spacing and word wrap
- Zoom (Ctrl+/-, Ctrl+mouse wheel)
- Undo / Redo (Ctrl+Z / Ctrl+Shift+Z) in menu
- Print (Ctrl+P) with paginated Pango layout
- Recent files submenu (last 10, persisted in settings.conf)
- Auto-reload when the open file changes on disk (cursor preserved; prompt if buffer is dirty)
- Find & Replace (Ctrl+F / Ctrl+H) with match count, scrollbar markers
- Go to Line (Ctrl+G)
- SSH/SFTP remote file editing (connect, browse, open, save) with disabled menu items when disconnected
- Status bar (encoding, file type, size, cursor position, SSH status)
- Persistent settings (~/.config/notes-light/settings.conf)
- Saved SSH connection profiles (~/.config/notes-light/connections.conf)
- Save confirmation dialog on close with unsaved changes (Save / Don't Save / Cancel)
- Smart dirty detection with FNV-1a hashing (undo back to original clears dirty flag)
- Auto-restore last opened file
- Atomic file writes (exclusive tmp via mkstemp + rename)
- CSS injection protection
- Full theme support for all dialogs
- Async remote file browsing (non-blocking UI)

## Dependencies

- GTK 4
- libadwaita-1
- GtkSourceView 5 (syntax highlighting)
- OpenSSH client (for SFTP features)

## Build

```
sudo apt install libadwaita-1-dev libgtksourceview-5-dev gcc make pkg-config

make
./build/notes-light
```

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+O | Open file |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save As |
| Ctrl+N | New file |
| Ctrl+P | Print |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z / Ctrl+Y | Redo |
| Ctrl+F | Find |
| Ctrl+H | Find & Replace |
| Ctrl+G | Go to Line |
| Ctrl+= / Ctrl++ | Zoom in |
| Ctrl+- | Zoom out |
| Ctrl+mouse wheel | Zoom in/out (1pt step) |
| Ctrl+Q | Quit |

## SSH/SFTP

Notes Light can edit files on remote servers via SSH. No external libraries required — uses the system OpenSSH client with ControlMaster for connection multiplexing.

1. **SFTP Connect** — open from hamburger menu, configure host/port/user/key, save profiles
2. **Open Remote File** — browse remote directories, click to open files
3. **Save** — Ctrl+S writes back to the remote file via SSH
4. **Disconnect** — closes the ControlMaster session

Authentication: private key (recommended) or SSH agent. Passwords are not saved to disk.

## License

MIT - see [LICENSE](LICENSE)

## Author

krse
