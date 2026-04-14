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
- Status bar (encoding, file type, size, cursor position)
- Persistent settings (~/.config/notes-light/settings.conf)
- Auto-restore last opened file
- Auto-save on close
- Atomic file writes (tmp + rename)
- CSS injection protection

## Dependencies

- GTK 4
- libadwaita-1

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
| Ctrl+= / Ctrl++ | Zoom in |
| Ctrl+- | Zoom out |
| Ctrl+Q | Quit |

## License

MIT - see [LICENSE](LICENSE)

## Author

krse
