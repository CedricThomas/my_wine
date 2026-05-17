#!/usr/bin/env bash
#
# graphical_samples.sh — Run graphical sample scenarios under Xvfb + a WM.
#
# Samples are end-to-end scenario checks. Native binaries under tests/ remain
# unit-style tests; graphical samples add a real X11 display plus a lightweight
# window manager inside Docker so xdotool/xwininfo can inspect and drive
# windows deterministically.
#
# Usage:
#   scripts/graphical_samples.sh list
#   scripts/graphical_samples.sh build [NAME]
#   scripts/graphical_samples.sh run [NAME]
#   scripts/graphical_samples.sh inspect NAME
#
# Runtime selection:
#   GRAPHICAL_RUNTIME=my_wine   # default
#   GRAPHICAL_RUNTIME=wine      # run the same harness against real Wine
#
# Optional per-sample input script:
#   samples/<name>/applied_inputs.txt
#   One command per line:
#     sleep MS
#     focus
#     key XDOTOOL_KEY
#     type TEXT
#     click X Y
#     mousemove X Y
#     status LABEL
#     altf4
#     sigint
#     closewindow
#
# Notes:
#   - title matches under a real WM may include both the guest client window and
#     the WM decoration/frame. For closewindow, prefer the candidate that belongs
#     to the launched process group and advertises WM_DELETE_WINDOW.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$PROJECT_DIR/samples"
IMAGE_NAME="my_wine-samples"

graphical_runtime() {
    printf '%s' "${GRAPHICAL_RUNTIME:-my_wine}"
}

runtime_launcher() {
    local arch="${1:-64}"
    case "$(graphical_runtime)" in
        my_wine)
            printf '%s' "/project/my_wine"
            ;;
        wine)
            if [ "$arch" = "32" ]; then
                printf '%s' "/usr/lib/wine/wine"
            else
                printf '%s' "/usr/bin/wine64-stable"
            fi
            ;;
        *)
            echo "ERR: unsupported GRAPHICAL_RUNTIME='$(graphical_runtime)' (expected my_wine or wine)" >&2
            return 1
            ;;
    esac
}

is_verbose() {
    [ "${GRAPHICAL_VERBOSE:-0}" = "1" ]
}

graphical_window_manager() {
    printf '%s' "${GRAPHICAL_WINDOW_MANAGER:-openbox}"
}

should_validate_geometry() {
    [ "$(graphical_runtime)" = "my_wine" ]
}

parse_sample_info() {
    local file="$1"
    local key="$2"
    grep "^${key}=" "$file" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r' || true
}

discover_graphical_samples() {
    local name="${1:-}"
    if [ -n "$name" ]; then
        local info="$SAMPLES_DIR/$name/sample.info"
        if [ -f "$info" ] && [ "$(parse_sample_info "$info" type)" = "graphical" ]; then
            echo "$name"
        fi
        return 0
    fi

    find "$SAMPLES_DIR" -mindepth 2 -maxdepth 2 -name sample.info -print | sort |
        while IFS= read -r info; do
            if [ "$(parse_sample_info "$info" type)" = "graphical" ]; then
                basename "$(dirname "$info")"
            fi
        done
}

is_skipped_sample() {
    local name="$1"
    local info="$SAMPLES_DIR/$name/sample.info"
    local skip=""
    if [ -f "$info" ]; then
        skip="$(parse_sample_info "$info" "skip")"
    fi
    [ "$skip" = "true" ] || [ "$skip" = "1" ] || [ "$skip" = "yes" ]
}

skip_reason() {
    local name="$1"
    local info="$SAMPLES_DIR/$name/sample.info"
    local reason=""
    if [ -f "$info" ]; then
        reason="$(parse_sample_info "$info" "skip_reason")"
    fi
    printf '%s' "${reason:-skipped}"
}

ensure_image() {
    local needs_build=0
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        needs_build=1
    elif ! docker run --rm "$IMAGE_NAME" bash -lc '
        command -v Xvfb >/dev/null &&
        command -v openbox >/dev/null &&
        command -v xdotool >/dev/null &&
        command -v wmctrl >/dev/null &&
        test -x /usr/bin/wine64-stable &&
        test -x /usr/lib/wine/wine &&
        ldconfig -p | grep -q libSDL2-2.0.so.0 &&
        test -e /lib/ld-linux.so.2
    ' >/dev/null 2>&1; then
        needs_build=1
    fi

    if [ "$needs_build" -eq 1 ]; then
        echo "  Building Docker image $IMAGE_NAME ..."
        DOCKER_BUILDKIT=0 docker build -q -t "$IMAGE_NAME" "$PROJECT_DIR" >/dev/null
    fi
}

run_in_container() {
    local container_mode="$1"
    local name="$2"
    ensure_image
    docker run --rm \
        -e GRAPHICAL_RUNTIME="$(graphical_runtime)" \
        -e GRAPHICAL_VERBOSE="${GRAPHICAL_VERBOSE:-0}" \
        -e GRAPHICAL_WINDOW_MANAGER="$(graphical_window_manager)" \
        -e MY_WINE_DEBUG_LEVEL="${MY_WINE_DEBUG_LEVEL:-}" \
        -e WINEDEBUG="${WINEDEBUG:--all}" \
        -v "$PROJECT_DIR:/project" \
        -w /project \
        "$IMAGE_NAME" \
        bash scripts/graphical_samples.sh "$container_mode" "$name"
}

print_status() {
    local label="$1"
    local name="$2"
    local pid="${3:-}"
    local log_file="${4:-}"

    echo "GRAPHICAL_STATUS_BEGIN $label"
    echo "sample=$name"
    echo "display=${DISPLAY:-}"
    if command -v xdpyinfo >/dev/null 2>&1; then
        xdpyinfo 2>/dev/null | awk '
            /dimensions:/ { print "display_dimensions=" $2 }
            /default screen number:/ { print "default_screen=" $4 }
        ' || true
    fi
    if [ -n "$pid" ]; then
        if kill -0 "$pid" >/dev/null 2>&1; then
            echo "process=running"
        else
            echo "process=exited"
        fi
        echo "pid=$pid"
        print_process_snapshot "$label" "$pid"
    fi
    if command -v xdotool >/dev/null 2>&1; then
        local ids
        ids="$(xdotool search --all --onlyvisible --name '.*' 2>/dev/null || true)"
        if [ -z "$ids" ]; then
            echo "windows=0"
        else
            echo "$ids" | awk 'NF { count++ } END { print "windows=" count + 0 }'
            while IFS= read -r id; do
                [ -n "$id" ] || continue
                local title geom
                title="$(xdotool getwindowname "$id" 2>/dev/null || true)"
                geom="$(xdotool getwindowgeometry --shell "$id" 2>/dev/null | tr '\n' ' ' || true)"
                echo "window id=$id title=$(printf '%s' "$title" | tr '\n' ' ') $geom"
            done <<< "$ids"
        fi
    fi
    if [ -n "$log_file" ] && [ -f "$log_file" ]; then
        echo "stdout_tail<<EOF"
        tail -n 40 "$log_file" 2>/dev/null || true
        echo "EOF"
        if [ -f "${log_file}.err" ]; then
            echo "stderr_tail<<EOF"
            tail -n 80 "${log_file}.err" 2>/dev/null || true
            echo "EOF"
        fi
    fi
    echo "GRAPHICAL_STATUS_END $label"
}

failure_reason() {
    local log_file="$1"
    if [ -f "${log_file}.err" ]; then
        awk 'NF { print; exit }' "${log_file}.err"
    fi
}

wait_for_wm_ready() {
    local wm="$1"
    local timeout_sec="$2"
    local deadline=$((SECONDS + timeout_sec))

    while [ "$SECONDS" -lt "$deadline" ]; do
        case "$wm" in
            openbox)
                if xprop -root _NET_SUPPORTING_WM_CHECK >/dev/null 2>&1; then
                    return 0
                fi
                ;;
            *)
                if xprop -root >/dev/null 2>&1; then
                    return 0
                fi
                ;;
        esac
        sleep 0.2
    done
    return 1
}

window_pid() {
    local win_id="$1"
    xdotool getwindowpid "$win_id" 2>/dev/null || true
}

window_has_delete_protocol() {
    local win_id="$1"
    xprop -id "$win_id" WM_PROTOCOLS 2>/dev/null | grep -q "WM_DELETE_WINDOW"
}

pid_in_process_group() {
    local pgid="$1"
    local candidate_pid="$2"

    if [ -z "$pgid" ] || [ -z "$candidate_pid" ]; then
        return 1
    fi

    ps -o pgid= -p "$candidate_pid" 2>/dev/null |
        awk -v target="$pgid" 'NR == 1 { gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0); exit($0 == target ? 0 : 1) }'
}

select_window_id() {
    local ids="$1"
    local title="${2:-}"
    local pid="${3:-}"
    local pgid=""
    local best_id=""
    local best_score=-1
    local id=""

    [ -n "$ids" ] || return 1

    if [ -n "$pid" ]; then
        pgid="$(process_group_id "$pid")"
    fi

    while IFS= read -r id; do
        local score=0
        local candidate_title=""
        local candidate_pid=""

        [ -n "$id" ] || continue
        candidate_title="$(xdotool getwindowname "$id" 2>/dev/null || true)"
        candidate_pid="$(window_pid "$id")"

        if [ "$candidate_title" = "$title" ]; then
            score=$((score + 8))
        fi
        if [ -n "$pid" ] && [ "$candidate_pid" = "$pid" ]; then
            score=$((score + 16))
        elif pid_in_process_group "$pgid" "$candidate_pid"; then
            score=$((score + 12))
        fi
        if window_has_delete_protocol "$id"; then
            score=$((score + 4))
        fi

        if [ "$score" -gt "$best_score" ]; then
            best_id="$id"
            best_score="$score"
        fi
    done <<< "$ids"

    [ -n "$best_id" ] || return 1
    printf '%s\n' "$best_id"
}

resolve_window_id() {
    local title="$1"
    local pid="${2:-}"
    local ids=""

    ids="$(xdotool search --all --onlyvisible --name "$title" 2>/dev/null || true)"
    if [ -z "$ids" ]; then
        ids="$(xdotool search --all --name "$title" 2>/dev/null || true)"
    fi
    select_window_id "$ids" "$title" "$pid"
}

wait_for_window_id() {
    local title="$1"
    local timeout_sec="$2"
    local pid="${3:-}"
    local deadline=$((SECONDS + timeout_sec))
    local ids=""
    local win_id=""

    while [ "$SECONDS" -lt "$deadline" ]; do
        ids="$(xdotool search --all --onlyvisible --name "$title" 2>/dev/null || true)"
        if [ -z "$ids" ]; then
            ids="$(xdotool search --all --name "$title" 2>/dev/null || true)"
        fi
        if [ -n "$ids" ]; then
            win_id="$(select_window_id "$ids" "$title" "$pid" || true)"
            if [ -n "$win_id" ]; then
                printf '%s\n' "$win_id"
                return 0
            fi
        fi
        sleep 0.2
    done
    return 1
}

process_state() {
    local pid="$1"
    ps -o stat= -p "$pid" 2>/dev/null | awk 'NR == 1 { print substr($1, 1, 1) }'
}

process_is_running() {
    local pid="$1"
    local state
    state="$(process_state "$pid")"
    [ -n "$state" ] && [ "$state" != "Z" ]
}

process_group_id() {
    local pid="$1"
    ps -o pgid= -p "$pid" 2>/dev/null |
        awk 'NR == 1 { gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0); print }'
}

print_process_snapshot() {
    local label="$1"
    local pid="$2"
    local pgid
    pgid="$(process_group_id "$pid")"

    echo "PROCESS_SNAPSHOT_BEGIN $label"
    echo "tracked_pid=$pid"
    echo "tracked_state=$(process_state "$pid")"
    echo "tracked_pgid=${pgid:-unknown}"
    if [ -n "$pgid" ]; then
        echo "ps_group<<EOF"
        ps -eo pid,ppid,pgid,sid,stat,comm,args --sort=pid 2>/dev/null |
            awk -v target="$pgid" 'NR == 1 || $3 == target { print }'
        echo "EOF"
    else
        echo "ps_pid<<EOF"
        ps -o pid,ppid,pgid,sid,stat,comm,args -p "$pid" 2>/dev/null || true
        echo "EOF"
    fi
    echo "PROCESS_SNAPSHOT_END $label"
}

print_window_candidates() {
    local label="$1"
    local title="$2"
    local pid="${3:-}"
    local ids=""
    local pgid=""
    local id=""

    ids="$(xdotool search --all --onlyvisible --name "$title" 2>/dev/null || true)"
    [ -n "$ids" ] || return 0
    pgid="$(process_group_id "$pid")"

    echo "WINDOW_CANDIDATES_BEGIN $label"
    while IFS= read -r id; do
        local name=""
        local candidate_pid=""
        local flags=""

        [ -n "$id" ] || continue
        name="$(xdotool getwindowname "$id" 2>/dev/null || true)"
        candidate_pid="$(window_pid "$id")"
        if [ -n "$pgid" ] && pid_in_process_group "$pgid" "$candidate_pid"; then
            flags="pgid_match"
        fi
        if window_has_delete_protocol "$id"; then
            flags="${flags:+$flags,}delete_protocol"
        fi
        echo "candidate id=$id pid=${candidate_pid:-unknown} flags=${flags:-none} title=$(printf '%s' "$name" | tr '\n' ' ')"
    done <<< "$ids"
    echo "WINDOW_CANDIDATES_END $label"
}

stop_process() {
    local pid="$1"
    local timeout_sec="${2:-2}"
    local deadline=$((SECONDS + timeout_sec))
    local pgid=""

    pgid="$(process_group_id "$pid")"

    if process_is_running "$pid"; then
        if [ -n "$pgid" ]; then
            kill -- "-$pgid" >/dev/null 2>&1 || true
        else
            kill "$pid" >/dev/null 2>&1 || true
        fi
    fi

    while process_is_running "$pid" && [ "$SECONDS" -lt "$deadline" ]; do
        sleep 0.1
    done

    if process_is_running "$pid"; then
        if [ -n "$pgid" ]; then
            kill -KILL -- "-$pgid" >/dev/null 2>&1 || true
        else
            kill -KILL "$pid" >/dev/null 2>&1 || true
        fi
        deadline=$((SECONDS + 2))
        while process_is_running "$pid" && [ "$SECONDS" -lt "$deadline" ]; do
            sleep 0.1
        done
    fi

    wait "$pid" 2>/dev/null || true
}

graphical_run_retry_count() {
    local retries="${GRAPHICAL_SAMPLE_RETRIES:-2}"

    if ! [[ "$retries" =~ ^[1-9][0-9]*$ ]]; then
        retries=2
    fi
    printf '%s\n' "$retries"
}

run_graphical_sample_with_retries() {
    local name="$1"
    local max_attempts
    local attempt=1

    max_attempts="$(graphical_run_retry_count)"
    while [ "$attempt" -le "$max_attempts" ]; do
        if run_in_container run-container "$name"; then
            return 0
        fi
        if [ "$attempt" -lt "$max_attempts" ]; then
            echo "RETRY $name (attempt $((attempt + 1))/$max_attempts)"
        fi
        attempt=$((attempt + 1))
    done

    return 1
}

window_manager_close() {
    local win_id="$1"
    wmctrl -i -c "$win_id"
}

send_altf4() {
    local win_id="$1"

    # Avoid xdotool's window-targeted key path here: once Alt+F4 starts closing
    # the window, the later synthetic key events can race a destroyed X11 id and
    # emit BadWindow. Focus first, then send through the active window instead.
    if xdotool getwindowname "$win_id" >/dev/null 2>&1; then
        xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
    fi
    xdotool key --clearmodifiers Alt+F4 >/dev/null 2>&1 || true
}

apply_graphical_inputs() {
    local name="$1"
    local win_id="$2"
    local pid="$3"
    local log_file="$4"
    local inputs_file="$SAMPLES_DIR/$name/applied_inputs.txt"
    GRAPHICAL_INPUT_USED_SIGINT=0
    GRAPHICAL_INPUT_REQUESTED_CLOSE=0
    GRAPHICAL_INPUT_WAITED=0
    GRAPHICAL_INPUT_WAIT_STATUS=0

    [ -f "$inputs_file" ] || return 0

    if is_verbose; then
        echo "INPUTS_BEGIN $name"
    fi
    local line_no=0
    local line command rest x y
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

        if is_verbose; then
            echo "INPUT $line_no $command${rest:+ $rest}"
        fi
        case "$command" in
            sleep)
                if ! [[ "$rest" =~ ^[0-9]+$ ]]; then
                    echo "ERR: $inputs_file:$line_no sleep expects milliseconds"
                    return 1
                fi
                sleep "$(awk "BEGIN { printf \"%.3f\", $rest / 1000 }")"
                ;;
            focus)
                xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                ;;
            key)
                if [ -z "$rest" ]; then
                    echo "ERR: $inputs_file:$line_no key expects an xdotool key name"
                    return 1
                fi
                xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                xdotool key --window "$win_id" --clearmodifiers "$rest"
                ;;
            type)
                xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                xdotool type --window "$win_id" --clearmodifiers --delay 10 "$rest"
                ;;
            click)
                read -r x y extra <<< "$rest"
                if ! [[ "${x:-}" =~ ^-?[0-9]+$ && "${y:-}" =~ ^-?[0-9]+$ && -z "${extra:-}" ]]; then
                    echo "ERR: $inputs_file:$line_no click expects: click X Y"
                    return 1
                fi
                xdotool mousemove --window "$win_id" "$x" "$y" click 1
                ;;
            mousemove)
                read -r x y extra <<< "$rest"
                if ! [[ "${x:-}" =~ ^-?[0-9]+$ && "${y:-}" =~ ^-?[0-9]+$ && -z "${extra:-}" ]]; then
                    echo "ERR: $inputs_file:$line_no mousemove expects: mousemove X Y"
                    return 1
                fi
                xdotool mousemove --window "$win_id" "$x" "$y"
                ;;
            status)
                if is_verbose; then
                    print_status "input_${rest:-$line_no}" "$name" "$pid" "$log_file"
                fi
                ;;
            altf4)
                GRAPHICAL_INPUT_REQUESTED_CLOSE=1
                send_altf4 "$win_id"
                ;;
            closewindow)
                GRAPHICAL_INPUT_REQUESTED_CLOSE=1
                window_manager_close "$win_id" || true
                ;;
            sigint)
                GRAPHICAL_INPUT_USED_SIGINT=1
                GRAPHICAL_INPUT_REQUESTED_CLOSE=1
                GRAPHICAL_INPUT_WAIT_STATUS=130
                kill -INT "$pid" >/dev/null 2>&1 || true
                local sigint_deadline=$((SECONDS + 2))
                while process_is_running "$pid" && [ "$SECONDS" -lt "$sigint_deadline" ]; do
                    sleep 0.1
                done
                if process_is_running "$pid"; then
                    kill -KILL "$pid" >/dev/null 2>&1 || true
                    GRAPHICAL_INPUT_WAIT_STATUS=137
                fi
                wait "$pid" 2>/dev/null || true
                GRAPHICAL_INPUT_WAITED=1
                ;;
            *)
                echo "ERR: $inputs_file:$line_no unknown input command '$command'"
                return 1
                ;;
        esac
    done < "$inputs_file"
    if is_verbose; then
        echo "INPUTS_END $name"
    fi
}

run_container_sample() {
    local name="$1"
    local inspect_only="${2:-0}"
    local src_dir="$SAMPLES_DIR/$name"
    local info="$src_dir/sample.info"
    local exe="$src_dir/${name}.exe"
    local runtime
    local launcher
    runtime="$(graphical_runtime)"
    launcher="$(runtime_launcher)"
    if [ "$inspect_only" = "1" ]; then
        export GRAPHICAL_VERBOSE=1
    fi

    if [ "$runtime" = "my_wine" ]; then
        if [ ! -x "$PROJECT_DIR/my_wine" ]; then
            echo "ERR: /project/my_wine is missing or not executable. Run 'make my_wine my_wine64 my_wine32' first."
            return 1
        fi
    fi
    if [ ! -f "$exe" ]; then
        echo "ERR: $exe is missing. Run 'make samples SAMPLE=$name' first."
        return 1
    fi

    local expected_exit timeout_sec title expected_w expected_h
    local arch
    expected_exit="$(parse_sample_info "$info" graphical_exit)"
    expected_exit="${expected_exit:-$(parse_sample_info "$info" exit)}"
    timeout_sec="$(parse_sample_info "$info" timeout)"
    title="$(parse_sample_info "$info" window_title)"
    expected_w="$(parse_sample_info "$info" window_width)"
    expected_h="$(parse_sample_info "$info" window_height)"
    arch="$(parse_sample_info "$info" arch)"
    expected_exit="${expected_exit:-0}"
    timeout_sec="${timeout_sec:-10}"
    title="${title:-$name}"
    arch="${arch:-64}"
    launcher="$(runtime_launcher "$arch")"

    export DISPLAY="${DISPLAY:-:99}"
    export SDL_VIDEODRIVER=x11
    export SDL_AUDIODRIVER=dummy
    export WINEDEBUG="${WINEDEBUG:--all}"
    export GRAPHICAL_RUNTIME_SELECTED="$runtime"
    export GRAPHICAL_WINDOW_MANAGER_SELECTED="$(graphical_window_manager)"

    local xvfb_log="/tmp/my_wine_xvfb_${name}.log"
    Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp >"$xvfb_log" 2>&1 &
    local xvfb_pid=$!
    local wm_log="/tmp/my_wine_wm_${name}.log"
    local wm_pid=""
    trap '
        if [ -n "${wm_pid:-}" ]; then
            kill "$wm_pid" >/dev/null 2>&1 || true
        fi
        kill "$xvfb_pid" >/dev/null 2>&1 || true
    ' RETURN
    sleep 0.4

    case "$GRAPHICAL_WINDOW_MANAGER_SELECTED" in
        openbox)
            openbox >"$wm_log" 2>&1 &
            wm_pid=$!
            ;;
        *)
            echo "ERR: unsupported GRAPHICAL_WINDOW_MANAGER='$GRAPHICAL_WINDOW_MANAGER_SELECTED'"
            return 1
            ;;
    esac

    if ! wait_for_wm_ready "$GRAPHICAL_WINDOW_MANAGER_SELECTED" 5; then
        echo "ERR: window manager '$GRAPHICAL_WINDOW_MANAGER_SELECTED' did not become ready"
        if is_verbose && [ -f "$wm_log" ]; then
            echo "WM_LOG_BEGIN"
            tail -n 80 "$wm_log" 2>/dev/null || true
            echo "WM_LOG_END"
        fi
        return 1
    fi

    local output_file="/tmp/my_wine_graphical_${name}.out"
    : >"$output_file"
    : >"${output_file}.err"

    setsid bash -c 'trap - INT TERM; exec "$1" "$2"' _ "$launcher" "$exe" >"$output_file" 2>"${output_file}.err" &
    local pid=$!

    local win_id
    win_id="$(wait_for_window_id "$title" "$timeout_sec" "$pid" || true)"
    if [ -z "$win_id" ]; then
        if is_verbose; then
            print_status "no_window" "$name" "$pid" "$output_file"
        fi
        stop_process "$pid"
        local reason
        reason="$(failure_reason "$output_file")"
        if [ -n "$reason" ]; then
            echo "FAIL  $name (no window: $reason)"
        else
            echo "FAIL  $name (no window)"
        fi
        return 1
    fi

    if is_verbose; then
        print_status "window_ready" "$name" "$pid" "$output_file"
        print_window_candidates "window_ready" "$title" "$pid"
    fi

    local geometry width height
    if ! xdotool getwindowgeometry --shell "$win_id" >/dev/null 2>&1; then
        win_id="$(resolve_window_id "$title" "$pid" || true)"
    fi
    if [ -z "$win_id" ]; then
        if is_verbose; then
            print_window_candidates "window_unresolved" "$title" "$pid"
        fi
        stop_process "$pid"
        echo "FAIL  $name (could not resolve target window)"
        return 1
    fi
    geometry="$(xdotool getwindowgeometry --shell "$win_id" 2>/dev/null || true)"
    width="$(printf '%s\n' "$geometry" | awk -F= '$1 == "WIDTH" { print $2 }')"
    height="$(printf '%s\n' "$geometry" | awk -F= '$1 == "HEIGHT" { print $2 }')"

    if should_validate_geometry; then
        if [ -n "$expected_w" ] && [ "$width" != "$expected_w" ]; then
            echo "FAIL  $name (window width=$width, expected=$expected_w)"
            stop_process "$pid"
            return 1
        fi
        if [ -n "$expected_h" ] && [ "$height" != "$expected_h" ]; then
            echo "FAIL  $name (window height=$height, expected=$expected_h)"
            stop_process "$pid"
            return 1
        fi
    fi

    if ! apply_graphical_inputs "$name" "$win_id" "$pid" "$output_file"; then
        if is_verbose; then
            print_status "input_failed" "$name" "$pid" "$output_file"
        fi
        stop_process "$pid"
        echo "FAIL  $name (input script failed)"
        return 1
    fi

    if [ "$inspect_only" = "1" ]; then
        stop_process "$pid"
        echo "INSPECT  $name complete"
        return 0
    fi

    if process_is_running "$pid" && [ "${GRAPHICAL_INPUT_REQUESTED_CLOSE:-0}" != "1" ]; then
        # Prefer the guest-visible keyboard close path here. Do not reintroduce
        # a separate window-manager close command without fresh reference proof.
        if xdotool getwindowname "$win_id" >/dev/null 2>&1; then
            send_altf4 "$win_id"
        fi
    fi

    local deadline=$((SECONDS + timeout_sec))
    while process_is_running "$pid" && [ "$SECONDS" -lt "$deadline" ]; do
        sleep 0.2
    done
    if process_is_running "$pid"; then
        if is_verbose; then
            print_status "close_timeout" "$name" "$pid" "$output_file"
        fi
        stop_process "$pid"
        echo "FAIL  $name (close timeout)"
        return 1
    fi

    local ret=0
    if [ "$GRAPHICAL_INPUT_WAITED" = "1" ]; then
        ret="$GRAPHICAL_INPUT_WAIT_STATUS"
    elif wait "$pid"; then
        ret=0
    else
        ret=$?
    fi
    if [ "$ret" -eq 127 ]; then
        if is_verbose; then
            print_status "missing_exit_status" "$name" "$pid" "$output_file"
        fi
        echo "FAIL  $name (missing exit status)"
        return 1
    fi
    if [ "$GRAPHICAL_INPUT_USED_SIGINT" = "1" ] && { [ "$ret" -eq 0 ] || [ "$ret" -eq 130 ] || [ "$ret" -eq 137 ]; }; then
        echo "PASS  $name"
        return 0
    fi
    if [ "$ret" -ne "$expected_exit" ]; then
        if is_verbose; then
            print_status "bad_exit" "$name" "$pid" "$output_file"
        fi
        echo "FAIL  $name (exit=$ret, expected=$expected_exit)"
        return 1
    fi

    echo "PASS  $name"
}

mode="${1:-run}"
target="${2:-}"

case "$mode" in
    list)
        discover_graphical_samples "$target"
        ;;
    build)
        samples="$(discover_graphical_samples "$target")"
        if [ -z "$samples" ]; then
            echo "ERR: no graphical sample scenarios found${target:+ for '$target'}"
            exit 1
        fi
        for name in $samples; do
            if is_skipped_sample "$name"; then
                echo "SKIP  $name ($(skip_reason "$name"))"
                continue
            fi
            "$SCRIPT_DIR/samples.sh" build "$name"
        done
        ;;
    run)
        samples="$(discover_graphical_samples "$target")"
        if [ -z "$samples" ]; then
            echo "ERR: no graphical sample scenarios found${target:+ for '$target'}"
            exit 1
        fi
        pass=0
        fail=0
        skip=0
        for name in $samples; do
            if is_skipped_sample "$name"; then
                echo "SKIP  $name ($(skip_reason "$name"))"
                skip=$((skip + 1))
                continue
            fi
            "$SCRIPT_DIR/samples.sh" build "$name"
            if run_graphical_sample_with_retries "$name"; then
                pass=$((pass + 1))
            else
                fail=$((fail + 1))
            fi
        done
        echo "Graphical sample scenarios: $pass passed, $fail failed, $skip skipped"
        [ "$fail" -eq 0 ]
        ;;
    inspect)
        if [ -z "$target" ]; then
            echo "Usage: $0 inspect NAME"
            exit 1
        fi
        if is_skipped_sample "$target"; then
            echo "SKIP  $target ($(skip_reason "$target"))"
            exit 0
        fi
        "$SCRIPT_DIR/samples.sh" build "$target"
        run_in_container inspect-container "$target"
        ;;
    run-container)
        run_container_sample "$target" 0
        ;;
    inspect-container)
        run_container_sample "$target" 1
        ;;
    *)
        echo "Usage: $0 {list|build|run|inspect} [sample_name]"
        exit 1
        ;;
esac
