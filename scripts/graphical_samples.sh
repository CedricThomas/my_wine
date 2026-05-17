#!/usr/bin/env bash
#
# graphical_samples.sh — Run graphical sample scenarios under Xvfb + openbox.
#
# Each sample runs inside Docker with a virtual X display and openbox window
# manager. Window interaction is driven by xdotool and per-sample
# applied_inputs.txt scripts.
#
# Usage:
#   scripts/graphical_samples.sh list
#   scripts/graphical_samples.sh build [NAME]
#   scripts/graphical_samples.sh run [NAME]
# Per-sample input script: samples/<name>/applied_inputs.txt
#   One command per line: sleep MS, focus, key KEY, type TEXT,
#   click X Y, mousemove X Y, altf4, sigint, closewindow

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$PROJECT_DIR/samples"
IMAGE_NAME="my_wine-samples"

# ── Helpers ─────────────────────────────────────────────────────────

parse_sample_info() {
    local key="$1" file="$2"
    grep "^${key}=" "$file" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r' || true
}

discover_graphical_samples() {
    local name="${1:-}"
    if [ -n "$name" ]; then
        local info
        info="$SAMPLES_DIR/$name/sample.info"
        [ -f "$info" ] && [ "$(parse_sample_info type "$info")" = "graphical" ] && echo "$name"
        return 0
    fi
    find "$SAMPLES_DIR" -mindepth 2 -maxdepth 2 -name sample.info -print | sort |
        while IFS= read -r info; do
            [ "$(parse_sample_info type "$info")" = "graphical" ] && basename "$(dirname "$info")"
        done || true
}

is_skipped_sample() {
    local skip info
    skip=""
    info="$SAMPLES_DIR/$1/sample.info"
    [ -f "$info" ] && skip="$(parse_sample_info skip "$info")"
    [ "$skip" = "true" ] || [ "$skip" = "1" ] || [ "$skip" = "yes" ]
}

skip_reason() {
    local info
    info="$SAMPLES_DIR/$1/sample.info"
    [ -f "$info" ] && parse_sample_info skip_reason "$info" || echo "skipped"
}

ensure_image() {
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        echo "  Building Docker image $IMAGE_NAME ..."
        DOCKER_BUILDKIT=0 docker build -q -t "$IMAGE_NAME" "$PROJECT_DIR" >/dev/null
    fi
}

status_line() {
    printf '%s  %s\n' "$1" "$2"
}

# ── Window helpers ──────────────────────────────────────────────────

window_pid() {
    xdotool getwindowpid "$1" 2>/dev/null || true
}

window_has_delete_protocol() {
    xprop -id "$1" WM_PROTOCOLS 2>/dev/null | grep -q "WM_DELETE_WINDOW"
}

pid_in_process_group() {
    local pgid candidate_pid
    pgid="$1"
    candidate_pid="$2"
    [ -z "$pgid" ] || [ -z "$candidate_pid" ] && return 1
    ps -o pgid= -p "$candidate_pid" 2>/dev/null |
        awk -v target="$pgid" '{ gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0); exit($0 == target ? 0 : 1) }'
}

select_window_id() {
    local ids title pid
    ids="$1"
    title="${2:-}"
    pid="${3:-}"
    local pgid best_id best_score id score candidate_title candidate_pid
    pgid=""
    best_id=""
    best_score=-1
    [ -n "$ids" ] || return 1
    [ -n "$pid" ] && pgid="$(process_group_id "$pid")"

    while IFS= read -r id; do
        score=0
        candidate_title=""
        candidate_pid=""
        [ -n "$id" ] || continue
        candidate_title="$(xdotool getwindowname "$id" 2>/dev/null || true)"
        candidate_pid="$(window_pid "$id")"
        [ "$candidate_title" = "$title" ] && score=$((score + 8))
        if [ -n "$pid" ] && [ "$candidate_pid" = "$pid" ]; then
            score=$((score + 16))
        elif pid_in_process_group "$pgid" "$candidate_pid"; then
            score=$((score + 12))
        fi
        window_has_delete_protocol "$id" && score=$((score + 4))
        [ "$score" -gt "$best_score" ] && { best_id="$id"; best_score=$score; }
    done <<< "$ids"
    [ -n "$best_id" ] || return 1
    echo "$best_id"
}

resolve_window_id() {
    local ids
    ids="$(xdotool search --all --onlyvisible --name "$1" 2>/dev/null || true)"
    [ -z "$ids" ] && ids="$(xdotool search --all --name "$1" 2>/dev/null || true)"
    select_window_id "$ids" "$1" "${2:-}"
}

wait_for_window_id() {
    local title timeout_sec pid
    title="$1"
    timeout_sec="$2"
    pid="${3:-}"
    local deadline ids win_id
    deadline=$((SECONDS + timeout_sec))
    while [ "$SECONDS" -lt "$deadline" ]; do
        ids="$(xdotool search --all --onlyvisible --name "$title" 2>/dev/null || true)"
        [ -z "$ids" ] && ids="$(xdotool search --all --name "$title" 2>/dev/null || true)"
        if [ -n "$ids" ]; then
            win_id="$(select_window_id "$ids" "$title" "$pid" || true)"
            [ -n "$win_id" ] && { echo "$win_id"; return 0; }
        fi
        sleep 0.2
    done || true
    return 1
}

# ── Process helpers ─────────────────────────────────────────────────

process_state() {
    ps -o stat= -p "$1" 2>/dev/null | awk '{ print substr($1, 1, 1) }'
}

process_is_running() {
    local state
    state="$(process_state "$1")"
    [ -n "$state" ] && [ "$state" != "Z" ]
}

process_group_id() {
    ps -o pgid= -p "$1" 2>/dev/null | awk '{ gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0); print }'
}

stop_process() {
    local pid timeout_sec deadline pgid
    pid="$1"
    timeout_sec="${2:-2}"
    deadline=$((SECONDS + timeout_sec))
    pgid="$(process_group_id "$pid" || true)"

    if process_is_running "$pid"; then
        [ -n "$pgid" ] && kill -- "-$pgid" >/dev/null 2>&1 || kill "$pid" >/dev/null 2>&1 || true
    fi
    while process_is_running "$pid" && [ "$SECONDS" -lt "$deadline" ]; do sleep 0.1; done

    if process_is_running "$pid"; then
        [ -n "$pgid" ] && kill -KILL -- "-$pgid" >/dev/null 2>&1 || kill -KILL "$pid" >/dev/null 2>&1 || true
        deadline=$((SECONDS + 2))
        while process_is_running "$pid" && [ "$SECONDS" -lt "$deadline" ]; do sleep 0.1; done
    fi
    wait "$pid" 2>/dev/null || true
}

# ── Display session helpers ─────────────────────────────────────────

start_graphical_session() {
    local name
    name="$1"

    if [ "${GRAPHICAL_SHARED_X11:-0}" = "1" ]; then
        return 0
    fi

    local xvfb_log
    xvfb_log="/tmp/my_wine_xvfb_${name}.log"
    Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp >"$xvfb_log" 2>&1 &
    GRAPHICAL_XVFB_PID=$!
    openbox >/dev/null 2>&1 &
    GRAPHICAL_WM_PID=$!

    local wm_ready=0
    for _try in 1 2 3 4 5 6 7 8 9 10; do
        xprop -root _NET_SUPPORTING_WM_CHECK >/dev/null 2>&1 && { wm_ready=1; break; }
        sleep 0.2
    done || true

    if [ "$wm_ready" -ne 1 ]; then
        stop_graphical_session
        echo "ERR: openbox did not become ready"
        return 1
    fi

    xdotool search --name '.*' >/dev/null 2>&1 || true
    sleep 0.2
}

stop_graphical_session() {
    [ -n "${GRAPHICAL_WM_PID:-}" ] && kill "$GRAPHICAL_WM_PID" >/dev/null 2>&1 || true
    [ -n "${GRAPHICAL_XVFB_PID:-}" ] && kill "$GRAPHICAL_XVFB_PID" >/dev/null 2>&1 || true
    GRAPHICAL_WM_PID=""
    GRAPHICAL_XVFB_PID=""
}

# ── Input script ────────────────────────────────────────────────────

apply_graphical_inputs() {
    local name win_id pid log_file inputs_file
    name="$1"
    win_id="$2"
    pid="$3"
    log_file="$4"
    inputs_file="$SAMPLES_DIR/$name/applied_inputs.txt"
    GRAPHICAL_INPUT_USED_SIGINT=0
    GRAPHICAL_INPUT_REQUESTED_CLOSE=0
    GRAPHICAL_INPUT_WAITED=0
    GRAPHICAL_INPUT_WAIT_STATUS=0

    [ -f "$inputs_file" ] || return 0

    local line_no line command rest x y extra sigint_deadline
    line_no=0
    while IFS= read -r line || [ -n "$line" ]; do
        line_no=$((line_no + 1))
        line="${line%$'\r'}"
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"
        [ -z "$line" ] && continue
        [[ "$line" == \#* ]] && continue

        command="${line%%[[:space:]]*}"
        if [ "$command" = "$line" ]; then
            rest=""
        else
            rest="${line#"$command"}"
            rest="${rest#"${rest%%[![:space:]]*}"}"
        fi

        case "$command" in
            sleep)
                [[ "$rest" =~ ^[0-9]+$ ]] || { echo "ERR: $inputs_file:$line_no sleep expects milliseconds"; return 1; }
                sleep "$(awk "BEGIN { printf \"%.3f\", $rest / 1000 }")"
                ;;
            focus)
                xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                ;;
            key)
                [ -z "$rest" ] && { echo "ERR: $inputs_file:$line_no key expects an xdotool key name"; return 1; }
                xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                xdotool key --window "$win_id" --clearmodifiers "$rest"
                ;;
            type)
                xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                xdotool type --window "$win_id" --clearmodifiers --delay 10 "$rest"
                ;;
            click|mousemove)
                read -r x y extra <<< "$rest"
                [[ "${x:-}" =~ ^-?[0-9]+$ && "${y:-}" =~ ^-?[0-9]+$ && -z "${extra:-}" ]] || \
                    { echo "ERR: $inputs_file:$line_no ${command} expects: ${command} X Y"; return 1; }
                xdotool mousemove --window "$win_id" "$x" "$y"
                [ "$command" = "click" ] && xdotool click 1
                ;;
            status) ;;
            altf4)
                GRAPHICAL_INPUT_REQUESTED_CLOSE=1
                xdotool getwindowname "$win_id" >/dev/null 2>&1 && xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                xdotool key --clearmodifiers Alt+F4 >/dev/null 2>&1 || true
                ;;
            closewindow)
                GRAPHICAL_INPUT_REQUESTED_CLOSE=1
                wmctrl -i -c "$win_id" || true
                ;;
            sigint)
                GRAPHICAL_INPUT_USED_SIGINT=1
                GRAPHICAL_INPUT_REQUESTED_CLOSE=1
                GRAPHICAL_INPUT_WAIT_STATUS=130
                kill -INT "$pid" >/dev/null 2>&1 || true
                sigint_deadline=$((SECONDS + 2))
                while process_is_running "$pid" && [ "$SECONDS" -lt "$sigint_deadline" ]; do sleep 0.1; done
                if process_is_running "$pid"; then
                    kill -KILL "$pid" >/dev/null 2>&1 || true
                    GRAPHICAL_INPUT_WAIT_STATUS=137
                fi
                wait "$pid" 2>/dev/null || true
                GRAPHICAL_INPUT_WAITED=1
                ;;
            *)
                echo "ERR: $inputs_file:$line_no unknown input command '$command'"; return 1
                ;;
        esac
    done < "$inputs_file"
}

# ── Run one sample (inside container) ──────────────────────────────

run_container_sample() {
    local name src_dir info exe
    name="$1"
    src_dir="$SAMPLES_DIR/$name"
    info="$src_dir/sample.info"
    exe="$src_dir/${name}.exe"

    [ ! -x "$PROJECT_DIR/my_wine" ] && { echo "ERR: /project/my_wine is missing. Run 'make' first."; return 1; }
    [ ! -f "$exe" ] && { echo "ERR: $exe is missing. Run 'make samples SAMPLE=$name' first."; return 1; }

    local expected_exit timeout_sec title expected_w expected_h arch launcher
    expected_exit="$(parse_sample_info graphical_exit "$info")"
    expected_exit="${expected_exit:-$(parse_sample_info exit "$info")}"
    timeout_sec="$(parse_sample_info timeout "$info")"
    title="$(parse_sample_info window_title "$info")"
    expected_w="$(parse_sample_info window_width "$info")"
    expected_h="$(parse_sample_info window_height "$info")"
    arch="$(parse_sample_info arch "$info")"
    expected_exit="${expected_exit:-0}"
    timeout_sec="${timeout_sec:-10}"
    title="${title:-$name}"
    arch="${arch:-64}"
    launcher="/project/my_wine"
    [ "$arch" = "32" ] && launcher="/project/my_wine32"

    export DISPLAY="${DISPLAY:-:99}"
    export SDL_VIDEODRIVER=x11
    export SDL_AUDIODRIVER=dummy
    export WINEDEBUG="${WINEDEBUG:--all}"

    start_graphical_session "$name" || return 1
    trap 'stop_graphical_session' RETURN

    local output_file pid win_id
    output_file="/tmp/my_wine_graphical_${name}.out"
    : >"$output_file"
    setsid bash -c 'trap - INT TERM; exec "$1" "$2"' _ "$launcher" "$exe" >"$output_file" 2>"${output_file}.err" &
    pid=$!

    win_id="$(wait_for_window_id "$title" "$timeout_sec" "$pid" || true)"
    if [ -z "$win_id" ]; then
        stop_process "$pid"
        echo "FAIL  $name (no window)"
        return 1
    fi

    local geometry width height
    if ! xdotool getwindowgeometry --shell "$win_id" >/dev/null 2>&1; then
        win_id="$(resolve_window_id "$title" "$pid" || true)"
        [ -z "$win_id" ] && { stop_process "$pid"; echo "FAIL  $name (could not resolve window)"; return 1; }
    fi
    geometry="$(xdotool getwindowgeometry --shell "$win_id" 2>/dev/null || true)"
    width="$(printf '%s\n' "$geometry" | awk -F= '$1 == "WIDTH" { print $2 }')"
    height="$(printf '%s\n' "$geometry" | awk -F= '$1 == "HEIGHT" { print $2 }')"

    if [ -n "$expected_w" ] && [ "$width" != "$expected_w" ]; then
        stop_process "$pid"
        echo "FAIL  $name (window width=$width, expected=$expected_w)"
        return 1
    fi
    if [ -n "$expected_h" ] && [ "$height" != "$expected_h" ]; then
        stop_process "$pid"
        echo "FAIL  $name (window height=$height, expected=$expected_h)"
        return 1
    fi

    if ! apply_graphical_inputs "$name" "$win_id" "$pid" "$output_file"; then
        stop_process "$pid"
        echo "FAIL  $name (input script failed)"
        return 1
    fi

    if process_is_running "$pid" && [ "${GRAPHICAL_INPUT_REQUESTED_CLOSE:-0}" != "1" ]; then
        if xdotool getwindowname "$win_id" >/dev/null 2>&1; then
            xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
            xdotool key --clearmodifiers Alt+F4 >/dev/null 2>&1 || true
        fi
    fi

    local deadline
    deadline=$((SECONDS + timeout_sec))
    while process_is_running "$pid" && [ "$SECONDS" -lt "$deadline" ]; do sleep 0.2; done
    if process_is_running "$pid"; then
        stop_process "$pid"
        echo "FAIL  $name (close timeout)"
        return 1
    fi

    local ret
    ret=0
    if [ "$GRAPHICAL_INPUT_WAITED" = "1" ]; then
        ret="$GRAPHICAL_INPUT_WAIT_STATUS"
    elif wait "$pid"; then
        ret=0
    else
        ret=$?
    fi
    [ "$ret" -eq 127 ] && { echo "FAIL  $name (missing exit status)"; return 1; }

    if [ "$GRAPHICAL_INPUT_USED_SIGINT" = "1" ] && { [ "$ret" -eq 0 ] || [ "$ret" -eq 130 ] || [ "$ret" -eq 137 ]; }; then
        status_line "PASS" "$name"
        return 0
    fi
    if [ "$ret" -ne "$expected_exit" ]; then
        echo "FAIL  $name (exit=$ret, expected=$expected_exit)"
        return 1
    fi

    status_line "PASS" "$name"
}

# ── Run all samples in one shared container ─────────────────────────

run_in_graphical_container() {
    local mode="$1"
    local target="${2:-}"

    ensure_image
    docker run --rm \
        -e GRAPHICAL_SHARED_X11=1 \
        -e MY_WINE_DEBUG_LEVEL="${MY_WINE_DEBUG_LEVEL:-}" \
        -e WINEDEBUG="${WINEDEBUG:--all}" \
        -v "$PROJECT_DIR:/project" \
        -w /project \
        "$IMAGE_NAME" \
        bash -lc '
            export DISPLAY=:99
            export SDL_VIDEODRIVER=x11
            export SDL_AUDIODRIVER=dummy
            Xvfb :99 -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
            XVFB_PID=$!
            openbox >/dev/null 2>&1 &
            WM_PID=$!
            trap "kill $WM_PID $XVFB_PID >/dev/null 2>&1 || true" EXIT
            for _try in 1 2 3 4 5; do
                xprop -root _NET_SUPPORTING_WM_CHECK >/dev/null 2>&1 && break
                sleep 0.2
            done
            sleep 0.2
            bash scripts/graphical_samples.sh "$1" "$2"
        ' _ "$mode" "$target"
}

run_graphical_samples_all() {
    local samples_arr=("$@")
    run_in_graphical_container run-many "${samples_arr[*]}"
}

run_graphical_samples_many() {
    local samples_arr=("$@")
    (
        set +e
        local pass=0 fail=0
        local name
        for name in "${samples_arr[@]}"; do
            if bash scripts/graphical_samples.sh run-container "$name"; then
                pass=$((pass + 1))
            else
                fail=$((fail + 1))
            fi
        done
        echo "Graphical sample scenarios: $pass passed, $fail failed"
        [ "$fail" -eq 0 ]
    )
}

run_single_graphical_sample() {
    local target="$1"
    run_in_graphical_container run-container "$target"
}

# ── Mode dispatch ───────────────────────────────────────────────────

MODE="${1:-run}"
TARGET="${2:-}"

case "$MODE" in
    list)
        discover_graphical_samples "$TARGET"
        ;;
    build)
        samples="$(discover_graphical_samples "$TARGET")"
        [ -z "$samples" ] && { echo "ERR: no graphical samples found${TARGET:+ for '$TARGET'}"; exit 1; }
        samples_arr=()
        for name in $samples; do
            if is_skipped_sample "$name"; then
                echo "SKIP  $name ($(skip_reason "$name"))"
            else
                samples_arr+=("$name")
            fi
        done
        [ "${#samples_arr[@]}" -eq 0 ] || "$SCRIPT_DIR/samples.sh" build-many "${samples_arr[@]}"
        ;;
    run)
        samples="$(discover_graphical_samples "$TARGET")"
        [ -z "$samples" ] && { echo "ERR: no graphical samples found${TARGET:+ for '$TARGET'}"; exit 1; }
        [ "${GRAPHICAL_SKIP_BUILD:-0}" = "1" ] || build_needed=1
        samples_arr=()
        skip=0
        for name in $samples; do
            if is_skipped_sample "$name"; then
                echo "SKIP  $name ($(skip_reason "$name"))"
                skip=$((skip + 1))
            else
                samples_arr+=("$name")
            fi
        done
        if [ "${#samples_arr[@]}" -gt 0 ] && [ "${build_needed:-0}" = "1" ]; then
            "$SCRIPT_DIR/samples.sh" build-many "${samples_arr[@]}"
        fi
        if [ "${#samples_arr[@]}" -eq 0 ]; then
            echo "Graphical sample scenarios: 0 passed, 0 failed, $skip skipped"
        elif [ "${#samples_arr[@]}" -eq 1 ]; then
            run_single_graphical_sample "${samples_arr[0]}"
        else
            run_graphical_samples_all "${samples_arr[@]}"
        fi
        ;;
    run-container)
        run_container_sample "$TARGET"
        ;;
    run-many)
        IFS=' ' read -r -a samples_arr <<< "$TARGET"
        run_graphical_samples_many "${samples_arr[@]}"
        ;;
    *)
        echo "Usage: $0 {list|build|run} [sample_name]"
        exit 1
        ;;
esac
