#!/usr/bin/env bash
#
# capture_screenshot.sh - Capture a window/root screenshot from a Docker/Xvfb run.
#
# Defaults target the Doom95 debug path:
#   scripts/capture_screenshot.sh
#
# Generic usage:
#   scripts/capture_screenshot.sh --title "Test" --out artifacts/screenshots/test.png -- ./my_wine samples/foo/foo.exe
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE_NAME="${SCREENSHOT_IMAGE:-my_wine-samples}"

TITLE="Doom 95"
DELAY_SEC="8"
TIMEOUT_SEC="25"
DEBUG_LEVEL="${MY_WINE_DEBUG_LEVEL:-1}"
BUILD32="${SCREENSHOT_BUILD32:-1}"
CAPTURE_ROOT="${SCREENSHOT_CAPTURE_ROOT:-1}"
DISPLAY_SIZE="${SCREENSHOT_DISPLAY_SIZE:-1024x768x24}"
OUT=""

usage() {
    cat <<EOF
capture_screenshot.sh - Capture a window/root screenshot from a Docker/Xvfb run.

Defaults target the Doom95 debug path:
  scripts/capture_screenshot.sh

Generic usage:
  scripts/capture_screenshot.sh --title "Test" --out artifacts/screenshots/test.png -- ./my_wine samples/foo/foo.exe

Options:
  --title TITLE       Window title regex/name to wait for. Default: Doom 95
  --out PATH          Window PNG path. Relative paths are project-relative.
  --delay SEC         Seconds to wait after finding the window. Default: 8
  --timeout SEC       Seconds to wait for the window. Default: 25
  --debug LEVEL       MY_WINE_DEBUG_LEVEL inside the container. Default: 1
  --no-build32        Do not rebuild my_wine32 inside the container first.
  --no-root           Do not also capture the root display.
  --image NAME        Docker image name. Default: my_wine-samples
  -h, --help          Show this help.

Environment:
  SCREENSHOT_IMAGE, SCREENSHOT_BUILD32, SCREENSHOT_CAPTURE_ROOT,
  SCREENSHOT_DISPLAY_SIZE, MY_WINE_DEBUG_LEVEL
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --title)
            TITLE="${2:?--title needs a value}"
            shift 2
            ;;
        --out)
            OUT="${2:?--out needs a value}"
            shift 2
            ;;
        --delay)
            DELAY_SEC="${2:?--delay needs a value}"
            shift 2
            ;;
        --timeout)
            TIMEOUT_SEC="${2:?--timeout needs a value}"
            shift 2
            ;;
        --debug)
            DEBUG_LEVEL="${2:?--debug needs a value}"
            shift 2
            ;;
        --no-build32)
            BUILD32=0
            shift
            ;;
        --no-root)
            CAPTURE_ROOT=0
            shift
            ;;
        --image)
            IMAGE_NAME="${2:?--image needs a value}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            echo "ERR: unknown option '$1'" >&2
            usage >&2
            exit 2
            ;;
        *)
            break
            ;;
    esac
done

if ! [[ "$DELAY_SEC" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "ERR: --delay expects seconds" >&2
    exit 2
fi
if ! [[ "$TIMEOUT_SEC" =~ ^[0-9]+$ ]]; then
    echo "ERR: --timeout expects integer seconds" >&2
    exit 2
fi

if [ "$#" -eq 0 ]; then
    CMD=(./my_wine32 samples/unpacked/doom95/DOOM95.EXE)
else
    CMD=("$@")
fi

if [ -z "$OUT" ]; then
    mkdir -p "$PROJECT_DIR/artifacts/screenshots"
    OUT="artifacts/screenshots/$(printf '%s' "$TITLE" | tr -cs '[:alnum:]' '_' | tr '[:upper:]' '[:lower:]')_$(date +%Y%m%d_%H%M%S).png"
fi

case "$OUT" in
    /*) OUT_ABS="$OUT" ;;
    *) OUT_ABS="$PROJECT_DIR/$OUT" ;;
esac

case "$OUT_ABS" in
    "$PROJECT_DIR"/*) ;;
    *)
        echo "ERR: output must be inside the project directory so Docker can write it: $OUT_ABS" >&2
        exit 2
        ;;
esac

ROOT_ABS="${OUT_ABS%.png}_root.png"
mkdir -p "$(dirname "$OUT_ABS")"

if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "Building Docker image $IMAGE_NAME ..."
    DOCKER_BUILDKIT=0 docker build -t "$IMAGE_NAME" "$PROJECT_DIR"
fi

OUT_CONTAINER="/project/${OUT_ABS#"$PROJECT_DIR"/}"
ROOT_CONTAINER="/project/${ROOT_ABS#"$PROJECT_DIR"/}"

docker run --rm -i \
    -e SCREENSHOT_HOST_UID="$(id -u)" \
    -e SCREENSHOT_HOST_GID="$(id -g)" \
    -e SCREENSHOT_TITLE="$TITLE" \
    -e SCREENSHOT_DELAY="$DELAY_SEC" \
    -e SCREENSHOT_TIMEOUT="$TIMEOUT_SEC" \
    -e SCREENSHOT_OUT="$OUT_CONTAINER" \
    -e SCREENSHOT_ROOT_OUT="$ROOT_CONTAINER" \
    -e SCREENSHOT_CAPTURE_ROOT="$CAPTURE_ROOT" \
    -e SCREENSHOT_BUILD32="$BUILD32" \
    -e SCREENSHOT_DISPLAY_SIZE="$DISPLAY_SIZE" \
    -e MY_WINE_DEBUG_LEVEL="$DEBUG_LEVEL" \
    -v "$PROJECT_DIR:/project" \
    -w /project \
    "$IMAGE_NAME" \
    bash -s -- "${CMD[@]}" <<'INNER'
set -euo pipefail

cmd=("$@")
export DISPLAY=:99
export SDL_VIDEODRIVER=x11
export SDL_AUDIODRIVER=dummy

if [ "${SCREENSHOT_BUILD32:-0}" = "1" ]; then
    make my_wine32
fi

rm -f "$SCREENSHOT_OUT" "$SCREENSHOT_ROOT_OUT" /tmp/my_wine_screenshot.out /tmp/my_wine_screenshot.err

Xvfb :99 -screen 0 "${SCREENSHOT_DISPLAY_SIZE:-1024x768x24}" -nolisten tcp >/tmp/my_wine_screenshot_xvfb.log 2>&1 &
xvfb_pid=$!
openbox >/tmp/my_wine_screenshot_openbox.log 2>&1 &
wm_pid=$!
cleanup() {
    if [ -n "${guest_pid:-}" ]; then
        guest_pgid="$(ps -o pgid= -p "$guest_pid" 2>/dev/null | awk '{ print $1 }' || true)"
        [ -n "$guest_pgid" ] && kill -TERM "-$guest_pgid" >/dev/null 2>&1 || true
        sleep 0.3
        [ -n "$guest_pgid" ] && kill -KILL "-$guest_pgid" >/dev/null 2>&1 || true
    fi
    kill "$wm_pid" "$xvfb_pid" >/dev/null 2>&1 || true
}
trap cleanup EXIT

for _try in $(seq 1 20); do
    xprop -root _NET_SUPPORTING_WM_CHECK >/dev/null 2>&1 && break
    sleep 0.2
done

setsid "${cmd[@]}" >/tmp/my_wine_screenshot.out 2>/tmp/my_wine_screenshot.err &
guest_pid=$!

win=""
deadline=$((SECONDS + SCREENSHOT_TIMEOUT))
while [ "$SECONDS" -lt "$deadline" ]; do
    win="$(xdotool search --onlyvisible --name "$SCREENSHOT_TITLE" 2>/dev/null | tail -1 || true)"
    [ -z "$win" ] && win="$(xdotool search --name "$SCREENSHOT_TITLE" 2>/dev/null | tail -1 || true)"
    [ -n "$win" ] && break
    sleep 0.25
done

if [ -z "$win" ]; then
    echo "ERR: no window matched '$SCREENSHOT_TITLE'" >&2
    tail -120 /tmp/my_wine_screenshot.err >&2 || true
    exit 1
fi

xdotool windowfocus "$win" >/dev/null 2>&1 || true
sleep "$SCREENSHOT_DELAY"

import -window "$win" "$SCREENSHOT_OUT"
if [ "${SCREENSHOT_CAPTURE_ROOT:-1}" = "1" ]; then
    import -window root "$SCREENSHOT_ROOT_OUT"
fi

echo "window_id=$win"
identify "$SCREENSHOT_OUT"
if [ "${SCREENSHOT_CAPTURE_ROOT:-1}" = "1" ]; then
    identify "$SCREENSHOT_ROOT_OUT"
fi
if [ -n "${SCREENSHOT_HOST_UID:-}" ] && [ -n "${SCREENSHOT_HOST_GID:-}" ]; then
    chown "$SCREENSHOT_HOST_UID:$SCREENSHOT_HOST_GID" "$SCREENSHOT_OUT" >/dev/null 2>&1 || true
    if [ "${SCREENSHOT_CAPTURE_ROOT:-1}" = "1" ]; then
        chown "$SCREENSHOT_HOST_UID:$SCREENSHOT_HOST_GID" "$SCREENSHOT_ROOT_OUT" >/dev/null 2>&1 || true
    fi
fi
ps -p "$guest_pid" -o pid,stat,pcpu,comm || true
echo "stderr_tail:"
tail -40 /tmp/my_wine_screenshot.err || true
INNER

if [ ! -s "$OUT_ABS" ]; then
    echo "ERR: screenshot was not created: $OUT_ABS" >&2
    exit 1
fi
if [ "$CAPTURE_ROOT" = "1" ] && [ ! -s "$ROOT_ABS" ]; then
    echo "ERR: root screenshot was not created: $ROOT_ABS" >&2
    exit 1
fi

echo "Wrote $OUT_ABS"
if [ "$CAPTURE_ROOT" = "1" ]; then
    echo "Wrote $ROOT_ABS"
fi
