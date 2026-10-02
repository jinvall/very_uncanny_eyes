# Uncanny Eyes TUI Controller

A terminal-based controller for the Uncanny Eyes SDL2 application.

## Features

- Configure all command-line options for uncanny-eyes
- Launch, stop, and restart the eye animation
- Toggle fullscreen and dozing mode
- Monitor process output
- Manage LDR (light-dependent resistor) serial settings

## Usage

```sh
python3 uncanny-tui.py
```

Or use the wrapper script:

```sh
./tui.sh
```

## Controls

| Key | Action |
|-----|--------|
| `Tab` / `↑` / `↓` | Navigate between fields |
| `Enter` | Cycle dropdown / toggle checkbox |
| `e` | Edit text field |
| `l` / `L` | Launch uncanny-eyes |
| `s` / `S` | Stop running process |
| `r` / `R` | Restart with current settings |
| `f` / `F` | Toggle fullscreen (restart) |
| `d` / `D` | Toggle dozing mode (restart) |
| `F1` | Toggle help screen |
| `F10` / `q` | Quit |

## Layout

- **Left panel**: Configuration form with all available options
- **Right panel**: Process status and output log
- **Footer**: Action buttons and keyboard shortcuts

## Requirements

- Python 3.6+
- curses (built-in on Linux)
- The uncanny-eyes binary (built from `build-ldr/`)
