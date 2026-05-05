#!/usr/bin/env python3
"""
gen_dispatcher.py — Regenerate dispatcher switch bodies from nt_syscalls.def

Reads include/nt_syscalls.def and generates both dispatcher switch bodies.
Uses template bodies for all non-trivial patterns.

Usage:
  python3 scripts/gen_dispatcher.py               # verify mode
  python3 scripts/gen_dispatcher.py --generate     # write dispatcher_generated.c
  python3 scripts/gen_dispatcher.py --help
"""

import sys
import os
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRIPT_DIR)
DEF_FILE = os.path.join(ROOT, "include/nt_syscalls.def")
OUT_FILE = os.path.join(ROOT, "src/syscall/dispatcher_generated.c")

# ── Template bodies for each syscall ─────────────────────────────
# Placeholders:  STACK(n)  → read_guest_stack(n) or read_guest_stack_ctx(ctx, n)
#                 RET(expr) → return (uint64_t)expr;  or  return expr;
#                 DISPATCH_PTR(arg, var, short, name) → dispatch_ptr_inout call
#
# Templates define the complete switch case body for both dispatchers.

TEMPLATES = {
    # ── simple cases ──
    "NtCallbackReturn": "result = handler_NtCallbackReturn();",
    "NtClose": "result = handler_NtClose(arg1);",
    "NtUnmapViewOfSection": "result = handler_NtUnmapViewOfSection(arg1, arg2);",
    "NtTerminateProcess": "result = handler_NtTerminateProcess(arg1, arg2);",
    "NtReleaseMutex": "result = handler_NtReleaseMutex(arg1, arg2);",

    # ── validate-only cases ──
    "NtGetContextThread": "int status = read_guest_ptr(arg2, NULL, NULL, \"context\");\n"
                          "    if (status != 0) RET(status)\n"
                          "    result = handler_NtGetContextThread(arg1, arg2);",
    "NtSetContextThread": "int status = read_guest_ptr(arg2, NULL, NULL, \"context\");\n"
                          "    if (status != 0) RET(status)\n"
                          "    result = handler_NtSetContextThread(arg1, arg2);",

    # ── inout cases (single ptr) ──
    "NtQuerySystemTime": "uint64_t h_ft_val = 0;\n"
                         "    void *p_ft = NULL;\n"
                         "    DISPATCH_PTR(arg1, h_ft_val, ft, filetime_ptr)\n"
                         "    result = handler_NtQuerySystemTime((PVOID)&h_ft_val);\n"
                         "    WRITE_BACK(p_ft, h_ft_val);",
    "NtDelayExecution": "uint64_t h_timeout = 0;\n"
                        "    void *p_timeout = NULL;\n"
                        "    DISPATCH_PTR(arg2, h_timeout, timeout, timeout_ptr)\n"
                        "    result = handler_NtDelayExecution(arg1, (PVOID)&h_timeout);\n"
                        "    WRITE_BACK(p_timeout, h_timeout);",
    "NtCreateEvent": "uint64_t h_handle = 0;\n"
                     "    void *p_handle = NULL;\n"
                     "    DISPATCH_PTR(arg1, h_handle, handle, event_handle)\n"
                     "    result = handler_NtCreateEvent(&h_handle, arg2, arg3, arg4, STACK(1));\n"
                     "    WRITE_BACK(p_handle, h_handle);",
    "NtCreateThreadEx": "uint64_t h_handle = 0;\n"
                        "    void *p_handle = NULL;\n"
                        "    DISPATCH_PTR(arg1, h_handle, handle, thread_handle)\n"
                        "    result = handler_NtCreateThreadEx(&h_handle, arg2, arg3, arg4,\n"
                        "        STACK(1), STACK(2), STACK(3), STACK(4), STACK(5), STACK(6), STACK(7));\n"
                        "    WRITE_BACK(p_handle, h_handle);",
    "NtQueryPerformanceCounter": "uint64_t h_counter = 0;\n"
                                 "    void *p_counter = NULL;\n"
                                 "    DISPATCH_PTR(arg1, h_counter, counter, counter_ptr)\n"
                                 "    result = handler_NtQueryPerformanceCounter((PVOID)&h_counter);\n"
                                 "    WRITE_BACK(p_counter, h_counter);",
    "NtQueryPerformanceFrequency": "uint64_t h_freq = 0;\n"
                                   "    void *p_freq = NULL;\n"
                                   "    DISPATCH_PTR(arg1, h_freq, freq, frequency_ptr)\n"
                                   "    result = handler_NtQueryPerformanceFrequency((PVOID)&h_freq);\n"
                                   "    WRITE_BACK(p_freq, h_freq);",
    "NtSetEvent": "uint64_t h_prev = 0;\n"
                  "    void *p_prev = NULL;\n"
                  "    DISPATCH_PTR(arg2, h_prev, prev, previous_state)\n"
                  "    result = handler_NtSetEvent(arg1, (PVOID)&h_prev);\n"
                  "    WRITE_BACK(p_prev, h_prev);",
    "NtResetEvent": "uint64_t h_prev = 0;\n"
                    "    void *p_prev = NULL;\n"
                    "    DISPATCH_PTR(arg2, h_prev, prev, previous_state)\n"
                    "    result = handler_NtResetEvent(arg1, (PVOID)&h_prev);\n"
                    "    WRITE_BACK(p_prev, h_prev);",
    "NtWaitForSingleObject": "uint64_t h_timeout = 0;\n"
                             "    void *p_timeout = NULL;\n"
                             "    DISPATCH_PTR(arg3, h_timeout, timeout, timeout_ptr)\n"
                             "    result = handler_NtWaitForSingleObject(arg1, arg2, (PVOID)&h_timeout);\n"
                             "    WRITE_BACK(p_timeout, h_timeout);",
    "NtCreateMutex": "uint64_t h_handle = 0;\n"
                     "    void *p_handle = NULL;\n"
                     "    DISPATCH_PTR(arg1, h_handle, handle, mutex_handle)\n"
                     "    result = handler_NtCreateMutex(&h_handle, arg2, arg3);\n"
                     "    WRITE_BACK(p_handle, h_handle);",

    # ── multi cases (complex) ──
    "NtQueryInformationProcess":
        "uint64_t h_buffer = arg3;\n"
        "    uint64_t h_ret_len = STACK(1);\n"
        "    int status = read_guest_ptr(h_buffer, NULL, NULL, \"buffer\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(h_ret_len, NULL, NULL, \"return_length\");\n"
        "    if (status != 0) RET(status)\n"
        "    result = handler_NtQueryInformationProcess(arg1, arg2, h_buffer, arg4, h_ret_len);",

    "NtAllocateVirtualMemory":
        "uint64_t h_base_addr = 0;\n"
        "    uint64_t h_region_sz = 0;\n"
        "    void *p_base = NULL;\n"
        "    void *p_region = NULL;\n"
        "    int status = read_guest_ptr(arg2, &h_base_addr, &p_base, \"base_address\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(arg4, &h_region_sz, &p_region, \"region_size\");\n"
        "    if (status != 0) RET(status)\n"
        "    result = handler_NtAllocateVirtualMemory(arg1, &h_base_addr, arg3, &h_region_sz,\n"
        "        STACK(1), STACK(2));\n"
        "    if (p_base)   *(uint64_t *)p_base = h_base_addr;\n"
        "    if (p_region) *(uint64_t *)p_region = h_region_sz;",

    "NtFreeVirtualMemory":
        "uint64_t h_base_addr = 0;\n"
        "    uint64_t h_region_sz = 0;\n"
        "    void *p_base = NULL;\n"
        "    void *p_region = NULL;\n"
        "    int status = read_guest_ptr(arg2, &h_base_addr, &p_base, \"base_address\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(arg3, &h_region_sz, &p_region, \"region_size\");\n"
        "    if (status != 0) RET(status)\n"
        "    result = handler_NtFreeVirtualMemory(arg1, &h_base_addr, &h_region_sz, arg4);\n"
        "    if (p_base)   *(uint64_t *)p_base = h_base_addr;\n"
        "    if (p_region) *(uint64_t *)p_region = h_region_sz;",

    "NtMapViewOfSection":
        "uint64_t h_base_addr = 0;\n"
        "    uint64_t h_section_off = STACK(1);\n"
        "    uint64_t h_view_sz = STACK(2);\n"
        "    void *p_base = NULL;\n"
        "    void *p_offset = NULL;\n"
        "    void *p_vsz = NULL;\n"
        "    int status = read_guest_ptr(arg3, &h_base_addr, &p_base, \"base_address\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(h_section_off, &h_section_off, &p_offset, \"section_offset\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(h_view_sz, &h_view_sz, &p_vsz, \"view_size\");\n"
        "    if (status != 0) RET(status)\n"
        "    result = handler_NtMapViewOfSection(arg1, arg2, &h_base_addr, arg4,\n"
        "        STACK(1), &h_section_off, &h_view_sz, STACK(4), STACK(5), STACK(6));\n"
        "    if (p_base)   *(uint64_t *)p_base = h_base_addr;\n"
        "    if (p_offset) *(uint64_t *)p_offset = h_section_off;\n"
        "    if (p_vsz)    *(uint64_t *)p_vsz = h_view_sz;",

    "NtReadFile":
        "uint64_t h_buffer = STACK(1);\n"
        "    uint64_t h_bytes_read = STACK(4);\n"
        "    int status = read_guest_ptr(h_buffer, NULL, NULL, \"buffer\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(h_bytes_read, NULL, NULL, \"bytes_read\");\n"
        "    if (status != 0) RET(status)\n"
        "    result = handler_NtReadFile(arg1, arg2, arg3, arg4, h_buffer,\n"
        "        STACK(2), STACK(3), h_bytes_read);",

    "NtWriteFile":
        "uint64_t h_buffer = STACK(1);\n"
        "    uint64_t h_bytes_written = STACK(4);\n"
        "    int status = read_guest_ptr(h_buffer, NULL, NULL, \"buffer\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(h_bytes_written, NULL, NULL, \"bytes_written\");\n"
        "    if (status != 0) RET(status)\n"
        "    result = handler_NtWriteFile(arg1, arg2, arg3, arg4, h_buffer,\n"
        "        STACK(2), STACK(3), h_bytes_written);",

    "NtCreateSection":
        "uint64_t h_handle = 0;\n"
        "    uint64_t h_max_sz = 0;\n"
        "    void *p_handle = NULL;\n"
        "    void *p_max = NULL;\n"
        "    int status = read_guest_ptr(arg1, &h_handle, &p_handle, \"section_handle\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(arg4, &h_max_sz, &p_max, \"maximum_size\");\n"
        "    if (status != 0) RET(status)\n"
        "    result = handler_NtCreateSection(&h_handle, arg2, arg3, &h_max_sz,\n"
        "        STACK(1), STACK(2), STACK(3));\n"
        "    if (p_handle) *(uint64_t *)p_handle = h_handle;\n"
        "    if (p_max)    *(uint64_t *)p_max = h_max_sz;",

    "NtOpenFile":
        "uint64_t h_handle = 0;\n"
        "    void *p_handle = NULL;\n"
        "    int status = read_guest_ptr(arg1, &h_handle, &p_handle, \"file_handle\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(arg3, NULL, NULL, \"object_attributes\");\n"
        "    if (status != 0) RET(status)\n"
        "    status = read_guest_ptr(arg4, NULL, NULL, \"io_status_block\");\n"
        "    if (status != 0) RET(status)\n"
        "    result = handler_NtOpenFile(&h_handle, arg2, arg3, arg4,\n"
        "        STACK(1), STACK(2));\n"
        "    if (p_handle) *(uint64_t *)p_handle = h_handle;",
}


def parse_def():
    """Parse nt_syscalls.def and return list of syscall dicts."""
    syscalls = []
    with open(DEF_FILE) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 4:
                continue
            syscalls.append({
                "num": parts[0],
                "name": parts[1],
                "handler": parts[2],
                "pattern": parts[3],
            })
    return syscalls


def apply_substitutions(body, stack_fn, ret_fn, dispatch_ptr_fn, write_back_fn):
    """Replace template placeholders with dispatcher-specific code."""
    def repl_stack(m):
        return stack_fn(m.group(1))
    result = re.sub(r'STACK\((\d+)\)', repl_stack, body)

    def repl_ret(m):
        return ret_fn(m.group(1))
    result = re.sub(r'RET\((\w+)\)', repl_ret, result)

    def repl_dispatch_ptr(m):
        return dispatch_ptr_fn(m.group(1), m.group(2), m.group(3), m.group(4))
    result = re.sub(r'DISPATCH_PTR\((\w+),\s*(\w+),\s*(\w+),\s*(\w+)\)',
                    repl_dispatch_ptr, result)

    def repl_write_back(m):
        return write_back_fn(m.group(1), m.group(2))
    result = re.sub(r'WRITE_BACK\((p_\w+),\s*(\w+)\)', repl_write_back, result)

    return result


def gen_case(entry, body, variant):
    """Wrap a template body into a switch case block."""
    # Build dispatcher-specific substitution functions
    if variant == "c":
        stack_fn = lambda n: "read_guest_stack(%s)" % n
        ret_fn = lambda e: "return (uint64_t)%s;" % e
        dispatch_ptr_fn = lambda arg, var, short, name: (
            "if (dispatch_ptr_inout(%s, &%s, &p_%s, \"%s\", &result) != 0) break;" %
            (arg, var, short, name))
        write_back_fn = lambda ptr, var: "if (%s) *(uint64_t *)%s = %s;" % (ptr, ptr, var)
    else:
        stack_fn = lambda n: "read_guest_stack_ctx(ctx, %s)" % n
        ret_fn = lambda e: "return %s;" % e
        dispatch_ptr_fn = lambda arg, var, short, name: (
            "if (dispatch_ptr_inout(%s, &%s, &p_%s, \"%s\", &result) != 0) break;" %
            (arg, var, short, name))
        write_back_fn = lambda ptr, var: "if (%s) *(uint64_t *)%s = %s;" % (ptr, ptr, var)

    body = apply_substitutions(body, stack_fn, ret_fn, dispatch_ptr_fn, write_back_fn)

    # Wrap in case block
    lines = [
        "    case %s: /* %s */" % (entry["num"], entry["name"]),
        "    {",
    ]
    for line in body.split("\n"):
        lines.append("        " + line)
    lines.append("        break;")
    lines.append("    }")
    return "\n".join(lines)


def gen_default(variant):
    """Generate the default case."""
    var_name = "nr" if variant == "c" else "syscall_number"
    return """    default:
    {
        char buf[39];
        format_err_unhandled_syscall(buf, %s);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
    }
    result = STATUS_NOT_IMPLEMENTED;
    break;""" % var_name


def gen_switch_body(syscalls, variant):
    """Generate the complete switch body for one dispatcher variant."""
    cases = []
    for entry in syscalls:
        name = entry["name"]
        if name not in TEMPLATES:
            raise ValueError("Missing template for: %s" % name)
        cases.append(gen_case(entry, TEMPLATES[name], variant))
    cases.append("")
    cases.append(gen_default(variant))
    return "\n".join(cases)


def generate_output():
    """Generate the full dispatcher_generated.c file."""
    syscalls = parse_def()

    c_body = gen_switch_body(syscalls, "c")
    legacy_body = gen_switch_body(syscalls, "legacy")

    header = (
        "/*\n"
        " * dispatcher_generated.c — Auto-generated from include/nt_syscalls.def\n"
        " * DO NOT EDIT BY HAND — run scripts/gen_dispatcher.py --generate\n"
        " * Contains the switch bodies for both dispatcher entry points.\n"
        " * Included from dispatcher.c via #define + #include.\n"
        " */\n"
        "\n"
        "#ifndef DISPATCHER_GENERATED_C\n"
        "#define DISPATCHER_GENERATED_C\n"
        "\n"
        "#if defined(DISPATCHER_C_BODY)\n"
        "switch (nr) {\n"
    )
    footer_c = (
        "}\n"
        "#elif defined(DISPATCHER_LEGACY_BODY)\n"
        "switch (nt_nr) {\n"
    )
    footer_legacy = (
        "}\n"
        "#else\n"
        '#error "Define DISPATCHER_C_BODY or DISPATCHER_LEGACY_BODY before including this file"\n'
        "#endif\n"
        "\n"
        "#endif /* DISPATCHER_GENERATED_C */\n"
    )

    return header + c_body + "\n" + footer_c + legacy_body + "\n" + footer_legacy


def main():
    if len(sys.argv) > 1:
        mode = sys.argv[1]
    else:
        mode = "--verify"

    if mode == "--help":
        print(__doc__)
        return

    if mode == "--generate":
        output = generate_output()
        with open(OUT_FILE, "w") as f:
            f.write(output)
        print("Generated: %s" % OUT_FILE)
        print("Syscall count: %d" % len(parse_def()))
        return

    if mode == "--verify":
        syscalls = parse_def()
        print("=== Verifying dispatcher generator (%d syscalls) ===" % len(syscalls))
        # Check all syscalls have templates
        missing = [e["name"] for e in syscalls if e["name"] not in TEMPLATES]
        if missing:
            print("FAIL: Missing templates: %s" % ", ".join(missing))
            return
        print("OK: All %d syscalls have templates" % len(syscalls))
        # Try generating both variants
        try:
            c_body = gen_switch_body(syscalls, "c")
            legacy_body = gen_switch_body(syscalls, "legacy")
            print("OK: c_dispatcher body generated (%d chars)" % len(c_body))
            print("OK: legacy_dispatcher body generated (%d chars)" % len(legacy_body))
        except Exception as e:
            print("FAIL: %s" % e)
            import traceback
            traceback.print_exc()
            return
        print("\nTo generate: python3 scripts/gen_dispatcher.py --generate")
        return

    print("Unknown mode: %s" % mode)


if __name__ == "__main__":
    main()
