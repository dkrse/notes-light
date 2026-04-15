# Notes Light

Fast, minimal text editor written in C with GTK 4 and libadwaita.

## Features

- Plain text editing with no database, no sidebar, no markdown
- Opens any file including binaries (displayed as text, max 5 MB view)
- 13 color themes (System, Light, Dark, Solarized, Monokai, Gruvbox, Nord, Dracula, Tokyo Night, Catppuccin)
- Line numbers with Cairo rendering
- Current line highlight (overlay)
- Font intensity (alpha blending)
- Configurable line spacing and word wrap
- Zoom (Ctrl+/-)
- Find & Replace (Ctrl+F / Ctrl+H) with match count, scrollbar markers
- Go to Line (Ctrl+G)
- SSH/SFTP remote file editing (connect, browse, open, save) with disabled menu items when disconnected
- Status bar (encoding, file type, size, cursor position, SSH status)
- Persistent settings (~/.config/notes-light/settings.conf)
- Saved SSH connection profiles (~/.config/notes-light/connections.conf)
- Save confirmation dialog on close with unsaved changes (Save / Don't Save / Cancel)
- Smart dirty detection (undo back to original clears dirty flag)
- Auto-restore last opened file
- Atomic file writes (tmp + rename)
- CSS injection protection
- Full theme support for all dialogs

## Dependencies

- GTK 4
- libadwaita-1
- OpenSSH client (for SFTP features)

## Build

```
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
| Ctrl+F | Find |
| Ctrl+H | Find & Replace |
| Ctrl+G | Go to Line |
| Ctrl+= / Ctrl++ | Zoom in |
| Ctrl+- | Zoom out |
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
