#!/usr/bin/env bash
#
# samples.sh — Convention-driven build and run for my_wine samples.
#
# Conventions (no hardcoded sample names):
#   - Discover samples: every subdirectory of samples/
#   - EXE build: .c files in the sample dir → {sample_name}.exe
#   - DLL build: .c + .def files in dlls/ subdirectory → parent sample dir
#   - A sample with dlls/ builds BOTH its DLLs and its EXE (if .c exists)
#   - Run mode: reads expected exit code / timeout from sample.info
#   - Samples with no .c in the main dir (DLL-producers only) are SKIPPED
#   - Graphical samples are forwarded to graphical_samples.sh
#
# Usage:
#   ./samples.sh              # Build all samples
#   ./samples.sh build        # Build all samples
#   ./samples.sh build NAME   # Build one sample
#   ./samples.sh run          # Build all + run all
#   ./samples.sh run NAME     # Build + run one sample
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$PROJECT_DIR/samples"
MY_WINE="$PROJECT_DIR/my_wine"
IMAGE_NAME="my_wine-samples"

# ── Helpers ───────────────────────────────────────────────────────

parse_sample_info() {
    local file="$1" key="$2"
    grep "^${key}=" "$file" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r' || true
}

sample_type() {
    local info="$SAMPLES_DIR/$1/sample.info"
    [ -f "$info" ] && parse_sample_info "$info" "type"
}

discover_samples() {
    local name
    if [ "$#" -gt 0 ]; then
        for name in "$@"; do
            [ -d "$SAMPLES_DIR/$name" ] && echo "$name"
        done
    else
        find "$SAMPLES_DIR" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort
    fi
}

resolve_samples_array() {
    if [ "${#TARGETS[@]}" -gt 0 ]; then
        mapfile -t samples_arr < <(discover_samples "${TARGETS[@]}")
    else
        mapfile -t samples_arr < <(discover_samples)
    fi
}

ensure_image() {
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "  Building Docker image $IMAGE_NAME ..."
        DOCKER_BUILDKIT=0 docker build -t "$IMAGE_NAME" "$PROJECT_DIR" 2>&1 || {
            echo "  FAIL: Docker build failed"; return 1
        }
    fi
}

in_shared_build_container() {
    [ "${SAMPLES_SHARED_BUILD_CONTAINER:-0}" = "1" ]
}

build_samples_in_shared_container() {
    local samples_arr=("$@")
    [ "${#samples_arr[@]}" -gt 0 ] || return 0
    ensure_image || return 1
    docker run --rm \
        -e SAMPLES_SHARED_BUILD_CONTAINER=1 \
        -v "$PROJECT_DIR:/project" \
        -w /project \
        "$IMAGE_NAME" \
        bash scripts/samples.sh build-in-container "${samples_arr[@]}"
}

build_selected_samples() {
    local samples_arr=("$@")
    local name
    [ "${#samples_arr[@]}" -gt 0 ] || return 0

    if ! in_shared_build_container && [ "${#samples_arr[@]}" -gt 1 ]; then
        build_samples_in_shared_container "${samples_arr[@]}"
        return $?
    fi

    for name in "${samples_arr[@]}"; do
        build_sample "$name" || { echo "  FAIL  $name (build failed)"; return 1; }
    done
}

select_compiler() {
    local arch="64"
    local info="$1/sample.info"
    [ -f "$info" ] && arch="$(parse_sample_info "$info" "arch")"
    [ "$arch" = "32" ] && echo "i686-w64-mingw32-gcc" || echo "x86_64-w64-mingw32-gcc"
}

to_container_path() {
    echo "/project${1#$PROJECT_DIR}"
}

# ── Build: DLLs ───────────────────────────────────────────────────

build_dlls() {
    local name
    name="$1"
    local src_dir
    src_dir="$SAMPLES_DIR/$name"
    local dll_dir
    dll_dir="$src_dir/dlls"
    [ -d "$dll_dir" ] || return 0
    local defs
    defs=$(find "$dll_dir" -name '*.def' 2>/dev/null | sort)
    [ -z "$defs" ] && return 0
    for def in $defs; do
        local dll_base
        dll_base=$(basename "$def" .def)
        local out_dll="$src_dir/${dll_base}.dll"
        local c_src="$dll_dir/${dll_base}.c"
        [ -f "$c_src" ] || continue

        local need_build=0
        if [ ! -f "$out_dll" ]; then
            need_build=1
        else
            for src in "$c_src" "$def"; do
                [ "$src" -nt "$out_dll" ] && { need_build=1; break; }
            done
        fi
        [ "$need_build" -eq 0 ] && continue

        local CC
        CC=$(select_compiler "$src_dir")
        echo "  CC  ${name}/dlls/${dll_base}.dll (mingw-dll)"
        if in_shared_build_container; then
            "$CC" \
                -Wall -Wextra -Wno-cast-function-type -Wno-array-bounds -Wno-stringop-overflow -O2 -shared \
                -Wl,"$(to_container_path "$def")" \
                -o "$out_dll" \
                "$c_src" 2>&1 || {
                    echo "  FAIL ${name}/dlls/${dll_base}.dll"; return 1
                }
        else
            ensure_image || return 1
            docker run --rm \
                -v "$PROJECT_DIR:/project:ro" \
                -v "$src_dir:/out" \
                "$IMAGE_NAME" \
                "$CC" \
                -Wall -Wextra -Wno-cast-function-type -Wno-array-bounds -Wno-stringop-overflow -O2 -shared \
                -Wl,"$(to_container_path "$def")" \
                -o "/out/${dll_base}.dll" \
                "$(to_container_path "$c_src")" 2>&1 || {
                    echo "  FAIL ${name}/dlls/${dll_base}.dll"; return 1
                }
        fi
        echo "  OK  $name -> ${dll_base}.dll"
    done
}

# ── Build: EXE ────────────────────────────────────────────────────

build_exe() {
    local name
    name="$1"
    local src_dir
    src_dir="$SAMPLES_DIR/$name"
    local out_exe
    out_exe="$src_dir/${name}.exe"
    local srcs
    srcs=$(find "$src_dir" -maxdepth 1 -name '*.c' 2>/dev/null | sort)
    [ -z "$srcs" ] && return 0

    local need_build=0
    if [ ! -f "$out_exe" ]; then
        need_build=1
    else
        for src in $srcs; do
            [ "$src" -nt "$out_exe" ] && { need_build=1; break; }
        done
    fi
    [ "$need_build" -eq 0 ] && return 0

    local container_srcs=""
    for src in $srcs; do
        container_srcs="$container_srcs $(to_container_path "$src")"
    done

    local CC
    CC=$(select_compiler "$src_dir")

    local OPT_LEVEL="2"
    local info="$src_dir/sample.info"
    if [ -f "$info" ]; then
        local opt_val
        opt_val=$(parse_sample_info "$info" "optimize")
        [[ "$opt_val" =~ ^[012]$ ]] && OPT_LEVEL="$opt_val"
    fi

    local EXTRA_FLAGS=""
    [ "$CC" = "i686-w64-mingw32-gcc" ] && EXTRA_FLAGS="-fno-stack-protector"

    echo "  CC  $name (mingw)"
    if in_shared_build_container; then
        # shellcheck disable=SC2086
        "$CC" \
            -Wall -Wextra -Wno-cast-function-type -Wno-array-bounds -Wno-stringop-overflow -O${OPT_LEVEL} -mconsole \
            $EXTRA_FLAGS \
            -o "$out_exe" \
            $srcs 2>&1 || {
                echo "  FAIL $name"; return 1
            }
    else
        ensure_image || return 1
        docker run --rm \
            -v "$PROJECT_DIR:/project:ro" \
            -v "$src_dir:/out" \
            "$IMAGE_NAME" \
            "$CC" \
            -Wall -Wextra -Wno-cast-function-type -Wno-array-bounds -Wno-stringop-overflow -O${OPT_LEVEL} -mconsole \
            $EXTRA_FLAGS \
            -o "/out/${name}.exe" \
            $container_srcs 2>&1 || {
                echo "  FAIL $name"; return 1
            }
    fi
    echo "  OK  $name -> ${name}.exe"
}

build_sample() {
    build_dlls "$1"
    build_exe "$1"
}

# ── Run ───────────────────────────────────────────────────────────

run_sample() {
    local name="$1"
    local src_dir="$SAMPLES_DIR/$name"
    local exe="$src_dir/${name}.exe"
    local info="$src_dir/sample.info"

    # Skip samples with no .c in the main dir
    if ! find "$src_dir" -maxdepth 1 -name '*.c' 2>/dev/null | head -1 | grep -q .; then
        echo "  SKIP  $name (no .c in main dir sample)"
        return 2
    fi

    if [ ! -f "$exe" ]; then
        echo "  ERR: $exe not built. Run './scripts/samples.sh build $name' first."
        return 1
    fi
    if [ ! -f "$MY_WINE" ]; then
        echo "  ERR: $MY_WINE not found. Run 'make' first."
        return 1
    fi

    local expected_exit=0 timeout_sec=5
    if [ -f "$info" ]; then
        local e t
        e=$(parse_sample_info "$info" "exit")
        t=$(parse_sample_info "$info" "timeout")
        [ -n "$e" ] && expected_exit="$e"
        [ -n "$t" ] && timeout_sec="$t"
    fi

    local output_file ret_file
    output_file=$(mktemp) || { echo "  ERR: $name (mktemp failed)"; return 1; }
    ret_file=$(mktemp) || { rm -f "$output_file"; echo "  ERR: $name (mktemp failed)"; return 1; }
    trap "rm -f '$output_file' '$ret_file' '${output_file}.err'" RETURN

    # Subshell: always exits 0; real exit code written to ret_file.
    # Suppresses bash signal diagnostic messages (e.g. "Segmentation fault").
    (
        set +e
        timeout "$timeout_sec" "$MY_WINE" "$exe" >"$output_file" 2>/dev/null
        echo $? >"$ret_file"
        exit 0
    ) 2>/dev/null

    local ret=0
    [ -f "$ret_file" ] && { ret=$(cat "$ret_file"); [ -z "$ret" ] && ret=0; }

    if [ "$ret" -ne "$expected_exit" ]; then
        echo "  FAIL  $name (exit=$ret, expected=$expected_exit)"
        return 1
    fi

    if [ -f "$src_dir/expected_output.txt" ]; then
        if ! diff -q "$src_dir/expected_output.txt" "$output_file" >/dev/null 2>&1; then
            echo "  FAIL  $name (output mismatch)"
            return 1
        fi
    fi

    if [ -f "$src_dir/expected_output_regex.txt" ]; then
        local -a regex_lines=()
        while IFS= read -r line || [ -n "$line" ]; do
            regex_lines+=("${line%$'\r'}")
        done < "$src_dir/expected_output_regex.txt"

        local actual_count=0 idx=0
        while IFS= read -r line || [ -n "$line" ]; do
            line="${line%$'\r'}"
            if ! printf '%s\n' "$line" | grep -qE "^${regex_lines[$idx]}$"; then
                echo "  FAIL  $name (output regex mismatch)"
                return 1
            fi
            actual_count=$((actual_count + 1))
            idx=$((idx + 1))
        done < "$output_file"
        if [ "$actual_count" -ne "${#regex_lines[@]}" ]; then
            echo "  FAIL  $name (output regex mismatch)"
            return 1
        fi
    fi

    echo "  PASS  $name"
}

# ── Main ────────────────────────────────────────────────────────────

MODE="${1:-build}"
shift || true
TARGET="${1:-}"
TARGETS=("$@")

case "$MODE" in
    build)
        resolve_samples_array
        if [ "${#samples_arr[@]}" -eq 0 ]; then
            [ -n "$TARGET" ] && { echo "ERR: sample '$TARGET' not found"; exit 1; }
            echo "No samples found in $SAMPLES_DIR/"; exit 0
        fi
        build_selected_samples "${samples_arr[@]}" || exit 1
        ;;

    build-in-container)
        resolve_samples_array
        if [ "${#samples_arr[@]}" -eq 0 ]; then
            [ -n "$TARGET" ] && { echo "ERR: sample '$TARGET' not found"; exit 1; }
            echo "No samples found in $SAMPLES_DIR/"; exit 0
        fi
        SAMPLES_SHARED_BUILD_CONTAINER=1 build_selected_samples "${samples_arr[@]}" || exit 1
        ;;

    build-many)
        resolve_samples_array
        if [ "${#samples_arr[@]}" -eq 0 ]; then
            [ -n "$TARGET" ] && { echo "ERR: sample '$TARGET' not found"; exit 1; }
            echo "No samples found in $SAMPLES_DIR/"; exit 0
        fi
        build_selected_samples "${samples_arr[@]}" || exit 1
        ;;

    run)
        resolve_samples_array
        if [ "${#samples_arr[@]}" -eq 0 ]; then
            [ -n "$TARGET" ] && { echo "ERR: sample '$TARGET' not found"; exit 1; }
            echo "No samples found in $SAMPLES_DIR/"; exit 0
        fi

        if [ -n "$TARGET" ] && [ "$(sample_type "$TARGET")" = "graphical" ]; then
            build_sample "$TARGET" || { echo "  FAIL  $TARGET (build failed)"; exit 1; }
            GRAPHICAL_SKIP_BUILD=1 exec "$SCRIPT_DIR/graphical_samples.sh" run "$TARGET"
        fi

        pass=0 fail=0 skip=0
        console_samples_arr=()
        for name in "${samples_arr[@]}"; do
            [ "$(sample_type "$name")" = "graphical" ] && continue
            console_samples_arr+=("$name")
        done
        if [ "${RUN_SKIP_BUILD:-0}" != "1" ] && [ "${#console_samples_arr[@]}" -gt 0 ]; then
            build_selected_samples "${console_samples_arr[@]}" || exit 1
        fi
        for name in "${samples_arr[@]}"; do
            if [ "$(sample_type "$name")" = "graphical" ]; then
                if [ "${MY_WINE_SKIP_GRAPHICAL_SAMPLES_SILENT:-0}" = "1" ]; then
                    continue
                fi
                echo "  SKIP  $name (graphical sample; use scripts/graphical_samples.sh or make run-samples-scenarios)"
                skip=$((skip + 1))
                continue
            fi
            result=0
            run_sample "$name" || result=$?
            case $result in
                0) pass=$((pass + 1)) ;;
                2) skip=$((skip + 1)) ;;
                *) fail=$((fail + 1)) ;;
            esac
        done
        echo ""; echo "Results: $pass passed, $fail failed, $skip skipped"
        [ "$fail" -eq 0 ]
        ;;

    *)
        echo "Usage: $0 {build|build-many|run} [sample_name ...]"; exit 1
        ;;
esac
