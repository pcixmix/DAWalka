#!/usr/bin/env bash
# =============================================================================
#  DAWalka — uninstaller
# =============================================================================
#
#  Removes DAWalka from the computer.  By default asks for confirmation
#  per item (plugin, venv, models, settings).
#
#  Usage:
#      ./uninstall.sh                  # interactive
#      ./uninstall.sh --yes            # remove plugin + venv (defaults), keep models/settings
#      ./uninstall.sh --all            # remove EVERYTHING except your generated WAVs
#      ./uninstall.sh --keep-models    # keep models (~6.7 GB)
#      ./uninstall.sh --keep-settings  # keep history and settings
#      ./uninstall.sh --dry-run        # show what would be removed
#
#  From Finder: double-click "Uninstall DAWalka.command".
#  From DAWalka.app: the "Uninstall" button invokes --all
# =============================================================================
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_PATH="$HOME/Library/Audio/Plug-Ins/Components/DAWalka.component"
VST3_INSTALL_PATH="$HOME/Library/Audio/Plug-Ins/VST3/DAWalka.vst3"
LOG_DIR="$HOME/Library/Application Support/DAWalka"
VENV_DIR="$LOG_DIR/venv"
MODELS_DIR="$LOG_DIR/models"
OUTPUT_DIR="$HOME/Documents/DAWalka"
LAUNCHER_FILE="$LOG_DIR/launcher_args.json"
# JUCE's PropertiesFile is a single file (no .xml suffix) living in
# the same directory as the plugin support directory itself.
SETTINGS_FILE="$HOME/Library/Application Support/DAWalka.settings"

# ─── Pretty output ─────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    BOLD=$'\033[1m'; DIM=$'\033[2m'; RESET=$'\033[0m'
    RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'
    BLUE=$'\033[0;34m'; CYAN=$'\033[0;36m'
else
    BOLD=""; DIM=""; RESET=""; RED=""; GREEN=""; YELLOW=""; BLUE=""; CYAN=""
fi

step()  { printf "\n${BLUE}${BOLD}==>${RESET} ${BOLD}%s${RESET}\n" "$1"; }
ok()    { printf "  ${GREEN}✓${RESET} %s\n" "$1"; }
info()  { printf "  ${DIM}%s${RESET}\n" "$1"; }
warn()  { printf "  ${YELLOW}!${RESET} %s\n" "$1"; }
hr()    { printf "${DIM}%s${RESET}\n" "───────────────────────────────────────────────────────────────"; }

banner() {
    printf "${CYAN}${BOLD}"
    cat <<'EOF'

        ____  ____  __      __    _      _  __      __  _   _
       |  _ \/ _  \ \ \    / /   / \    / \ \ \    / / | \ | |
       | | | | | | | \ \  / /   / _ \  / _ \ \ \  / /  |  \| |
       | |_| | |_| |  \ \/ /   / ___ \/ ___ \ \ \/ /   | |\  |
       |____/ \___/    \__/   /_/   \_\/   \_\ \__/    |_| \_|

                       AI Audio Generator - uninstaller

EOF
    printf "${RESET}\n"
}

notify() {
    if command -v osascript >/dev/null 2>&1; then
        osascript -e "display notification \"$2\" with title \"$1\"" >/dev/null 2>&1 || true
    fi
}

# Ask yes/no, default = first char of default arg
#   ask "Prompt?" "Y"   ->  [Y/n]
#   ask "Prompt?" "N"   ->  [y/N]
ask() {
    local prompt="$1" default="$2"

    # Non-interactive (--yes / --all) short-circuit: return the
    # default immediately, BEFORE touching the case-modification
    # expressions below.  macOS ships bash 3.2.57 (released 2007),
    # which doesn't understand ${var^^} / ${var,,} and dies with
    # "bad substitution".  Doing this check first means --all / --yes
    # never trip the bash 3.2 bug — the GUI's Uninstall button is
    # always non-interactive, so this is the only path that matters
    # for the .app launcher.
    if [[ "${ASSUME_YES:-0}" -eq 1 ]]; then
        if [[ "$default" == "Y" || "$default" == "y" ]]; then return 0; fi
        return 1
    fi

    # tr-based upper/lower-case: works in any POSIX shell, including
    # the macOS bash 3.2.57.  ${var^^}/${var,,} are bash 4+ only.
    local def_upper def_lower hint
    def_upper=$(printf '%s' "$default" | tr '[:lower:]' '[:upper:]')
    def_lower=$(printf '%s' "$default" | tr '[:upper:]' '[:lower:]')
    if [[ "$def_upper" == "Y" ]]; then hint="[Y/n]"; else hint="[y/N]"; fi

    while true; do
        printf "  %s %s " "$prompt" "$hint"
        # /dev/tty so this works even when run from a .command file
        # where stdin might be redirected.
        read -r reply </dev/tty
        reply="${reply:-$def_upper}"   # empty -> default
        # Lowercase the reply via tr (bash 3.2 doesn't support
        # ${reply,,}).  Then match against y/n.
        local reply_lower
        reply_lower=$(printf '%s' "$reply" | tr '[:upper:]' '[:lower:]')
        case "$reply_lower" in
            y|yes) return 0;;
            n|no)  return 1;;
            *) printf "  ${YELLOW}! Please type y or n${RESET}\n";;
        esac
    done
}

# Size of a path in human-readable form
du_h() {
    du -sh "$1" 2>/dev/null | cut -f1
}

# Remove path; prints the action.  $1 = "category", $2 = path
do_remove() {
    local category="$1" path="$2"
    if [[ -e "$path" ]]; then
        rm -rf "$path"
        ok "$category: removed ($path)"
    else
        info "$category: not found ($path)"
    fi
}

# ─── CLI flags ─────────────────────────────────────────────────────────────
ASSUME_YES=0
KEEP_MODELS=0
KEEP_SETTINGS=0
DRY_RUN=0
DELETE_ALL=0   # set by --all: remove plugin+venv+models+settings in one shot
while [[ $# -gt 0 ]]; do
    case "$1" in
        --yes|-y)           ASSUME_YES=1; shift;;
        --all|-a)           ASSUME_YES=1; DELETE_ALL=1; shift;;
        --keep-models)      KEEP_MODELS=1; shift;;
        --keep-settings)    KEEP_SETTINGS=1; shift;;
        --dry-run)          DRY_RUN=1; shift;;
        -h|--help)          sed -n '2,17p' "$0"; exit 0;;
        *)                  warn "Unknown flag: $1"; shift;;
    esac
done

# Default-answer overrides driven by --all.  In interactive mode the
# defaults are N (keep models, keep settings) so the user is
# prompted before blowing away anything beyond the plugin/venv.  In
# --all mode the GUI has already confirmed and the user expects a
# full teardown — so the defaults flip to Y for everything except
# the output folder, which holds the user's generated WAVs and is
# always kept (output-folder deletion is opt-in only).
MODELS_DEFAULT="N"
SETTINGS_DEFAULT="N"
if [[ $DELETE_ALL -eq 1 ]]; then
    MODELS_DEFAULT="Y"
    SETTINGS_DEFAULT="Y"
fi

# ─── Banner ────────────────────────────────────────────────────────────────
banner
hr
echo "  Scanning installed DAWalka components..."
hr
echo

# ─── Inventory ─────────────────────────────────────────────────────────────
step "What's installed"
have_plugin=0
have_vst3=0
have_venv=0
have_models=0
have_settings=0
have_output=0
have_launcher=0

[[ -d "$INSTALL_PATH" ]]    && have_plugin=1   && info "AU plugin:     $INSTALL_PATH  ($(du_h "$INSTALL_PATH"))"
[[ -d "$VST3_INSTALL_PATH" ]] && have_vst3=1   && info "VST3 plugin:   $VST3_INSTALL_PATH  ($(du_h "$VST3_INSTALL_PATH"))"
[[ -d "$VENV_DIR" ]]        && have_venv=1     && info "Python venv:   $VENV_DIR  ($(du_h "$VENV_DIR"))"
[[ -d "$MODELS_DIR" ]]      && have_models=1   && info "Models:        $MODELS_DIR  ($(du_h "$MODELS_DIR"))"
[[ -d "$OUTPUT_DIR" ]]      && have_output=1   && info "Output folder: $OUTPUT_DIR  ($(du_h "$OUTPUT_DIR"))"
# Settings = the JUCE PropertiesFile (single file in $HOME/Library/Application
# Support/, NOT inside $LOG_DIR) and/or the history.json the plugin
# maintains inside $LOG_DIR.  Either of those is enough to consider
# "settings installed".
[[ -f "$SETTINGS_FILE" || -f "$LOG_DIR/history.json" ]] \
                                && have_settings=1 && info "Settings/history: $SETTINGS_FILE, $LOG_DIR/history.json"
[[ -f "$LAUNCHER_FILE" ]]   && have_launcher=1 && info "Launcher args: $LAUNCHER_FILE"

if [[ $have_plugin -eq 0 && $have_vst3 -eq 0 && $have_venv -eq 0 && $have_models -eq 0 \
   && $have_output -eq 0 && $have_settings -eq 0 ]]; then
    ok "DAWalka is not installed.  Nothing to remove."
    exit 0
fi

# ─── Dry run ───────────────────────────────────────────────────────────────
if [[ $DRY_RUN -eq 1 ]]; then
    step "Dry run — what WOULD be removed"
    [[ $have_plugin -eq 1 ]]   && echo "  - AU plugin:     $INSTALL_PATH"
    [[ $have_vst3 -eq 1 ]]     && echo "  - VST3 plugin:   $VST3_INSTALL_PATH"
    [[ $have_venv -eq 1 ]]     && echo "  - Python venv:   $VENV_DIR"
    [[ $have_models -eq 1 && $KEEP_MODELS -eq 0 ]] && echo "  - Models:        $MODELS_DIR"
    [[ $have_settings -eq 1 && $KEEP_SETTINGS -eq 0 ]] && echo "  - Settings/history: $SETTINGS_FILE, $LOG_DIR/history.json"
    [[ $have_output -eq 1 ]]   && echo "  - Output folder: $OUTPUT_DIR (YOUR GENERATED WAV FILES)"
    [[ $have_launcher -eq 1 ]] && echo "  - Launcher args: $LAUNCHER_FILE"
    exit 0
fi

# ─── Confirm ───────────────────────────────────────────────────────────────
# Skip the prompt entirely when the caller already confirmed (--yes
# from CLI, --all from the GUI's NSAlert).  Without this guard the
# GUI's "Uninstall" button reaches the script, but the script's own
# "Continue?" ask() runs without a tty → reads nothing → returns the
# default ("N") → exits with "Cancelled by user" before doing
# anything.  That's the exact bug that left the user's plugin
# installed even after clicking Uninstall.
if [[ $ASSUME_YES -eq 0 ]]; then
    step "Confirmation"
    warn "This is IRREVERSIBLE.  Deleted models have to be downloaded again (~6.7 GB)."
    echo
    if ! ask "Continue with the uninstall?" "N"; then
        info "Cancelled by user."
        exit 0
    fi
fi

# ─── Stop running backend ──────────────────────────────────────────────────
step "Stopping the running Python backend"
pkill -9 -f "DAWalka.*server.py" 2>/dev/null && ok "Backend stopped" || info "Backend not running"
killall -9 audiounitservicecrasher 2>/dev/null || true
killall -9 "DAWalka Launcher" 2>/dev/null || true
# Give the OS a moment to release any open file handles on the venv/models
sleep 1

# ─── Plug-ins (always removed) ─────────────────────────────────────────────
if [[ $have_plugin -eq 1 ]]; then
    if ask "Remove the Audio Unit plugin from $INSTALL_PATH?" "Y"; then
        do_remove "AU plugin" "$INSTALL_PATH"
    fi
else
    info "AU plugin: not installed"
fi

if [[ $have_vst3 -eq 1 ]]; then
    if ask "Remove the VST3 plugin from $VST3_INSTALL_PATH?" "Y"; then
        do_remove "VST3 plugin" "$VST3_INSTALL_PATH"
    fi
else
    info "VST3 plugin: not installed"
fi

# ─── Venv (always removed unless you want to keep it) ─────────────────────
if [[ $have_venv -eq 1 ]]; then
    if ask "Remove the Python venv ($VENV_DIR, $(du_h "$VENV_DIR"))?" "Y"; then
        do_remove "venv" "$VENV_DIR"
    fi
else
    info "venv: not found"
fi

# ─── Models (kept by default with --keep-models, or on user choice) ──────
if [[ $have_models -eq 1 ]]; then
    if [[ $KEEP_MODELS -eq 1 ]]; then
        info "Models: kept (--keep-models)"
    else
        warn "Models take up $(du_h "$MODELS_DIR").  You can keep them to avoid re-downloading."
        if ask "Remove the models ($MODELS_DIR)?" "$MODELS_DEFAULT"; then
            do_remove "Models" "$MODELS_DIR"
        else
            info "Models: kept"
        fi
    fi
else
    info "Models: not found"
fi

# ─── Settings / history (kept by default with --keep-settings) ─────────────
if [[ $have_settings -eq 1 ]]; then
    if [[ $KEEP_SETTINGS -eq 1 ]]; then
        info "Settings/history: kept (--keep-settings)"
    else
        warn "Settings and history are your list of generated clips."
        if ask "Remove the settings/history?" "$SETTINGS_DEFAULT"; then
            # Remove ONLY the user-data files, not the whole support
            # directory (so the directory structure survives and the
            # next install finds it).  Note: the settings file is a
            # single file in $HOME/Library/Application Support/, NOT
            # a .xml inside $LOG_DIR — the plugin uses JUCE's
            # PropertiesFile which writes a flat file.
            rm -f "$SETTINGS_FILE"
            rm -f "$LOG_DIR/history.json"
            rm -f "$LOG_DIR/backend.log"
            rm -f "$LOG_DIR/backend.pid"
            rm -f "$LAUNCHER_FILE"
            ok "Settings/history removed"
        else
            info "Settings/history: kept"
        fi
    fi
fi

# ─── Output folder (user's WAVs — protect by default) ──────────────────────
if [[ $have_output -eq 1 ]]; then
    warn "YOUR generated WAV files are in $OUTPUT_DIR.  By default we do NOT remove them."
    if ask "Remove your generated files in $OUTPUT_DIR?" "N"; then
        do_remove "Output folder" "$OUTPUT_DIR"
    else
        info "Output folder: kept (your generated WAV files are still there)"
    fi
fi

# ─── Summary ───────────────────────────────────────────────────────────────
hr
echo
printf "  ${GREEN}${BOLD}Done.${RESET}\n"
hr
echo
echo "  To install again, grab the latest from:"
echo "      https://github.com/pcixmix/DAWalka"
echo

notify "DAWalka" "Uninstalled ✅"
