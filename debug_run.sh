#!/bin/bash
cd "$(dirname "$0")"
echo "=== DEBUG START ==="
echo "TERM=$TERM"
echo "Python: $(python3 --version 2>&1)"
echo "Binary: $(ls -la build-ldr/uncanny-eyes 2>&1)"
echo "=== Running TUI ==="
python3 -u uncanny-tui.py 2>&1
echo "=== EXIT CODE: $? ==="
