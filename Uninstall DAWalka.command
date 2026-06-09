#!/usr/bin/env bash
# Double-click wrapper: launches uninstall.sh in Terminal.app.
#
# The .command extension is recognised by macOS Finder — a double-click
# opens this file in a new Terminal window.  Everything else lives in
# uninstall.sh, so all the actual logic is in one place.
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/uninstall.sh" "$@"
