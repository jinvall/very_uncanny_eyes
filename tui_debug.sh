#!/bin/bash
cd "$(dirname "$0")"
echo "=== TUI Debug Start ===" >&2
echo "Python: $(python3 --version 2>&1)" >&2
echo "Terminal: ${TERM:-unset}" >&2
echo "Rows: ${LINES:-unset} Cols: ${COLUMNS:-unset}" >&2
echo "Binary exists: $([ -f build-ldr/uncanny-eyes ] && echo yes || echo NO)" >&2
echo "=== Starting TUI ===" >&2
python3 uncanny-tui.py 2>&1
echo "=== TUI Exit Code: $? ===" >&2
