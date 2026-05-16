#!/usr/bin/env bash
#
# graphical_samples.sh — Run graphical sample scenarios under Xvfb.
#
# Samples are end-to-end scenario checks. Native binaries under tests/ remain
# unit-style tests; graphical samples add a real X11 display inside Docker so
# xdotool/xwininfo can inspect and drive windows deterministically.
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
#     windowclose   # alias for altf4
#

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
        command -v xdotool >/dev/null &&
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

wait_for_window() {
    local title="$1"
    local timeout_sec="$2"
    local deadline=$((SECONDS + timeout_sec))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if xdotool search --name "$title" >/dev/null 2>&1; then
            return 0
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

stop_process() {
    local pid="$1"
    local timeout_sec="${2:-2}"
    local deadline=$((SECONDS + timeout_sec))

    if process_is_running "$pid"; then
        kill -- "-$pid" >/dev/null 2>&1 || kill "$pid" >/dev/null 2>&1 || true
    fi

    while process_is_running "$pid" && [ "$SECONDS" -lt "$deadline" ]; do
        sleep 0.1
    done

    if process_is_running "$pid"; then
        kill -KILL -- "-$pid" >/dev/null 2>&1 || kill -KILL "$pid" >/dev/null 2>&1 || true
        deadline=$((SECONDS + 2))
        while process_is_running "$pid" && [ "$SECONDS" -lt "$deadline" ]; do
            sleep 0.1
        done
    fi
}

apply_graphical_inputs() {
    local name="$1"
    local win_id="$2"
    local pid="$3"
    local log_file="$4"
    local inputs_file="$SAMPLES_DIR/$name/applied_inputs.txt"

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
                xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                xdotool key --window "$win_id" --clearmodifiers Alt+F4
                ;;
            windowclose)
                xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
                xdotool key --window "$win_id" --clearmodifiers Alt+F4
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
    unset MY_WINE_SAMPLE_AUTOQUIT
    export WINEDEBUG="${WINEDEBUG:--all}"
    export GRAPHICAL_RUNTIME_SELECTED="$runtime"

    local xvfb_log="/tmp/my_wine_xvfb_${name}.log"
    Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp >"$xvfb_log" 2>&1 &
    local xvfb_pid=$!
    trap 'kill "$xvfb_pid" >/dev/null 2>&1 || true' RETURN
    sleep 0.4

    local output_file="/tmp/my_wine_graphical_${name}.out"
    : >"$output_file"
    : >"${output_file}.err"

    local status_file="/tmp/my_wine_graphical_${name}.status"
    rm -f "$status_file"

    setsid bash -c '
        "$1" "$2" >"$3" 2>"$4"
        printf "%s\n" "$?" >"$5"
    ' _ "$launcher" "$exe" "$output_file" "${output_file}.err" "$status_file" &
    local pid=$!

    if ! wait_for_window "$title" "$timeout_sec"; then
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
    fi

    local win_id geometry width height
    win_id="$(xdotool search --name "$title" 2>/dev/null | head -1)"
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

    if process_is_running "$pid"; then
        # Prefer a guest-visible close path over X11 window destruction.
        if xdotool getwindowname "$win_id" >/dev/null 2>&1; then
            xdotool windowfocus "$win_id" >/dev/null 2>&1 || true
            xdotool key --window "$win_id" --clearmodifiers Alt+F4 || true
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

    while [ ! -f "$status_file" ] && [ "$SECONDS" -lt "$deadline" ]; do
        sleep 0.05
    done

    local ret=0
    if [ -f "$status_file" ]; then
        ret="$(cat "$status_file" 2>/dev/null || echo 1)"
    else
        if is_verbose; then
            print_status "missing_exit_status" "$name" "$pid" "$output_file"
        fi
        echo "FAIL  $name (missing exit status)"
        return 1
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
            if run_in_container run-container "$name"; then
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
