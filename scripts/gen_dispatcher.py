#!/usr/bin/env python3
"""
gen_dispatcher.py — Regenerate dispatcher switch body from nt_syscalls.def

Reads include/nt_syscalls.def (new declarative format) and generates
src/syscall/dispatcher_generated.c with the DISPATCHER_C_BODY switch body.

Usage:
  python3 scripts/gen_dispatcher.py               # verify mode
  python3 scripts/gen_dispatcher.py --generate     # write dispatcher_generated.c
  python3 scripts/gen_dispatcher.py --help

Version note:
  The syscall numbers in nt_syscalls.def are version-specific (Windows 10+
  x86_64).  When regenerating for a different Windows version, ensure the
  syscall numbers in nt_syscalls.def are updated to match the target version
  first.  This generator does NOT validate syscall numbers — it assumes
  nt_syscalls.def contains correct values for the target Windows version.
"""

import sys
import os
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRIPT_DIR)
DEF_FILE = os.path.join(ROOT, "include/nt_syscalls.def")
OUT_FILE = os.path.join(ROOT, "src/syscall/dispatcher_generated.c")


# ── .def parser ─────────────────────────────────────────────────

def parse_def():
    """Parse nt_syscalls.def and return list of syscall entry dicts.

    Each entry: {"num": "0x05", "name": "NtCallbackReturn",
                 "handler": "handler_NtCallbackReturn",
                 "args": [...], "call": "handler_NtCallbackReturn()"}
    """
    syscalls = []
    lines = []

    with open(DEF_FILE) as f:
        for raw_line in f:
            line = raw_line.rstrip("\n")
            stripped = line.strip()
            if stripped == "" and lines:
                syscalls.append(parse_block(lines))
                lines = []
            elif stripped and not stripped.startswith("#"):
                lines.append(stripped)

    if lines:
        syscalls.append(parse_block(lines))

    return syscalls


def parse_block(lines):
    """Parse a single syscall block."""
    entry = {"num": "", "name": "", "handler": "", "args": [], "call": ""}
    for line in lines:
        m = re.match(r'^([0-9A-Fa-fx]+)\s+(\S+)$', line)
        if (m and not line.startswith("handler:")
                and not line.startswith("args:")
                and not line.startswith("call:")):
            entry["num"] = m.group(1)
            entry["name"] = m.group(2)
            continue
        if line.startswith("handler:"):
            entry["handler"] = line.split(":", 1)[1].strip()
        elif line.startswith("args:"):
            entry["args"] = parse_args(line.split(":", 1)[1].strip())
        elif line.startswith("call:"):
            entry["call"] = line.split(":", 1)[1].strip()
    return entry


def parse_args(args_str):
    """Parse pipe-separated arg specs. Returns list of arg dicts."""
    args = []
    for part in args_str.split("|"):
        part = part.strip()
        if not part:
            continue
        if part.startswith("ptr(wb32)"):
            # ptr(wb32) src as name ["label"] — 32-bit write-back (PBOOLEAN, PULONG)
            m = re.match(r'ptr\(wb32\)\s+(\w+)\s+as\s+(\w+)(?:\s+"([^"]*)")?', part)
            if not m:
                raise ValueError("Invalid ptr(wb32): %s" % part)
            src, name = m.group(1), m.group(2)
            label = m.group(3) if m.group(3) else name
            args.append({"type": "wb32", "src": src, "name": name, "label": label})
        elif part.startswith("ptr(wb)"):
            # ptr(wb) src as name ["label"] — 64-bit write-back
            m = re.match(r'ptr\(wb\)\s+(\w+)\s+as\s+(\w+)(?:\s+"([^"]*)")?', part)
            if not m:
                raise ValueError("Invalid ptr(wb): %s" % part)
            src, name = m.group(1), m.group(2)
            label = m.group(3) if m.group(3) else name
            args.append({"type": "wb", "src": src, "name": name, "label": label})
        elif part.startswith("ptr(ro)"):
            m = re.match(r'ptr\(ro\)\s+(\w+)\s+(\w+)\s+"([^"]*)"', part)
            if not m:
                raise ValueError("Invalid ptr(ro): %s" % part)
            args.append({
                "type": "ro", "src": m.group(1),
                "name": m.group(2), "label": m.group(3),
            })
        elif part.startswith("stack"):
            m = re.match(r'stack\s+(\d+)\s+as\s+(\w+)', part)
            if not m:
                raise ValueError("Invalid stack: %s" % part)
            args.append({"type": "stack", "index": m.group(1), "name": m.group(2)})
        else:
            raise ValueError("Unknown arg: %s" % part)
    return args


# ── Helpers ─────────────────────────────────────────────────────

def is_reg_source(src):
    """True if src is a register argument (arg1..arg4)."""
    return bool(re.match(r'^arg\d+$', src))


def find_raw_source(args):
    """Return the source of the first ptr(ro) from a register, or None."""
    for a in args:
        if a["type"] == "ro" and is_reg_source(a["src"]):
            return a["src"]
    return None


def p_name_for(local_name):
    """Given h_xxx, return p_xxx. Given bare xxx, return p_xxx."""
    if local_name.startswith("h_"):
        return "p_" + local_name[2:]
    return "p_" + local_name


# ── C code generation ───────────────────────────────────────────

def gen_decls(args):
    """Generate local variable declarations."""
    lines = []
    declared = set()

    # Stack locals first (may be referenced by ptr(wb)/ptr(ro))
    for a in args:
        if a["type"] == "stack":
            n = a["name"]  # already h_xxx
            declared.add(n)
            lines.append("        uint64_t %s = read_guest_stack(%s);" % (n, a["index"]))

    # ptr(wb) locals: skip uint64_t if already declared by stack
    for a in args:
        if a["type"] in ("wb", "wb32"):
            n = a["name"]  # already h_xxx
            pn = p_name_for(n)
            a["p_name"] = pn
            if n not in declared:
                lines.append("        uint64_t %s = 0;" % n)
                declared.add(n)
            lines.append("        void *%s = NULL;" % pn)

    return "\n".join(lines)


def gen_validation(args):
    """Generate ptr(wb) dispatch and ptr(ro) validation code."""
    lines = []
    # ptr(wb) and ptr(wb32)
    for a in args:
        if a["type"] in ("wb", "wb32"):
            n = a["name"]
            pn = p_name_for(n)
            lines.append(
                '        if (dispatch_ptr_inout(%s, &%s, &%s, "%s", &result) != 0) break;' %
                (a["src"], n, a["p_name"], a.get("label", a["name"])))
    # ptr(ro) — reuse `status` var across multiple read_guest_ptr calls
    ret = "return (uint64_t)status;"
    first_ro = True
    for a in args:
        if a["type"] == "ro":
            src = a["src"]
            if first_ro:
                lines.append('        int status = read_guest_ptr(%s, NULL, NULL, "%s");' % (src, a["label"]))
                first_ro = False
            else:
                lines.append('        status = read_guest_ptr(%s, NULL, NULL, "%s");' % (src, a["label"]))
            lines.append("        if (status != 0) %s" % ret)
    return "\n".join(lines)


def expand_call(call_template, args):
    """Expand STACK(N) and raw in the call template."""
    result = call_template
    # STACK(N)
    result = re.sub(r'STACK\((\d+)\)', lambda m: "read_guest_stack(%s)" % m.group(1), result)
    # raw → first ptr(ro) from register
    raw_src = find_raw_source(args)
    if raw_src and "raw" in result:
        result = result.replace("raw", raw_src)
    return result


def gen_writeback(args):
    """Generate write-back code for ptr(wb) and ptr(wb32) args."""
    lines = []
    for a in args:
        if a["type"] == "wb":
            n = a["name"]
            pn = p_name_for(n)
            lines.append("        if (%s) *(uint64_t *)%s = %s;" % (pn, pn, n))
        elif a["type"] == "wb32":
            n = a["name"]
            pn = p_name_for(n)
            lines.append("        if (%s) *(uint32_t *)%s = (uint32_t)%s;" % (pn, pn, n))
    return "\n".join(lines)


def gen_case(entry):
    """Generate one switch case block."""
    args = entry["args"]
    parts = ["    case %s: /* %s */" % (entry["num"], entry["name"]), "    {"]

    decls = gen_decls(args)
    if decls:
        parts.append(decls)
        parts.append("")

    val = gen_validation(args)
    if val:
        parts.append(val)
        parts.append("")

    call = expand_call(entry["call"], args)
    parts.append("        result = %s;" % call)

    wb = gen_writeback(args)
    if wb:
        parts.append(wb)

    parts.append("        break;")
    parts.append("    }")
    return "\n".join(parts)


def gen_default():
    """Generate the default case."""
    return """    default:
    {
        char buf[39];
        format_err_unhandled_syscall(buf, nr);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
    }
    result = STATUS_NOT_IMPLEMENTED;
    break;"""


def gen_switch_body(syscalls):
    """Generate all case blocks."""
    cases = []
    for entry in syscalls:
        cases.append(gen_case(entry))
    cases.append("")
    cases.append(gen_default())
    return "\n".join(cases) + "\n"


def generate_output():
    """Generate the full dispatcher_generated.c file."""
    syscalls = parse_def()

    body = gen_switch_body(syscalls)

    return (
        "/*\n"
        " * dispatcher_generated.c — Auto-generated from include/nt_syscalls.def\n"
        " * DO NOT EDIT BY HAND — run scripts/gen_dispatcher.py --generate\n"
        " * Contains the switch body for the dispatcher entry point.\n"
        " * Included from dispatcher.c via #define + #include.\n"
        " */\n"
        "\n"
        "#ifndef DISPATCHER_GENERATED_C\n"
        "#define DISPATCHER_GENERATED_C\n"
        "\n"
        "#ifndef DISPATCHER_C_BODY\n"
        '#error "Define DISPATCHER_C_BODY before including this file"\n'
        "#endif\n"
        "\n"
        "switch (nr) {\n"
        + body +
        "}\n"
        "\n"
        "#endif /* DISPATCHER_GENERATED_C */\n"
    )


# ── CLI ─────────────────────────────────────────────────────────

def main():
    if len(sys.argv) > 1:
        mode = sys.argv[1]
    else:
        mode = "--verify"

    if mode in ("--help", "-h"):
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

        errors = []
        for e in syscalls:
            if not e["num"]:
                errors.append("Missing syscall number in block for %s" % e["name"])
            if not e["handler"]:
                errors.append("Missing handler for %s" % e["name"])
            if not e["call"]:
                errors.append("Missing call for %s" % e["name"])

        if errors:
            for err in errors:
                print("FAIL: %s" % err)
            return

        print("OK: All %d syscalls have handler: and call: lines" % len(syscalls))

        try:
            body = gen_switch_body(syscalls)
            print("OK: DISPATCHER_C_BODY body generated (%d chars)" % len(body))
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
