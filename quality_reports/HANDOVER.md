
# Handover Prompt — Quality Report Implementation

Copy the template below, replace `{REPORT}` with the report file, and give it to the agent.

---

## Template

```
You are working on the my_wine PE loader codebase at /home/arzad/Bureau/my_wine/.

Your task: implement the fixes defined in the quality report at:
  quality_reports/{REPORT}.md

Before you start:
1. Read {REPORT}.md in full to understand the findings and implementation plan.
2. Read each source file mentioned in the findings so you understand the current code.
3. Work through the tasks in order (Task 1 → Task 2 → ...), respecting dependencies.

Rules:
- Make minimal, surgical changes. Do not rewrite functions unless the task explicitly says so.
- After each task, compile the project to verify no regressions.
  Build command: (ask the user or check the existing Makefile/build system)
- If a task requires changes to a file that another task also touches, coordinate the edits to avoid conflicts.
- When adding #include directives, use the existing include path conventions in the project.
- Do NOT touch musl_src/, generated files, or test files unless the report explicitly mentions them.

After completing all tasks in the report:
1. Do a final compile and verify the project still works.
2. Report back: what you changed, which files were created/modified, and any issues encountered.
```

## Usage

Replace `{REPORT}` with one of:

| Report | Focus |
|--------|-------|
| `magic_numbers.md` | **Easiest start** — replace bare literals with named constants, low risk |
| `code_smells.md` | Dedup string helpers, split long functions, clean dead code |
| `bad_designs.md` | Add accessor functions, improve error handling, fix reentrancy |
| `architectural_issues.md` | **Largest** — split god-headers, refactor import_resolve.c, extract init_loader() |
| `non_future_proof.md` | Add architecture guards, document limitations, improve error messages |

## Recommended Order

1. `magic_numbers.md` (quick wins, affects many files but simple changes)
2. `code_smells.md` (deduplication unlocks cleaner architecture work)
3. `bad_designs.md` (accessor functions, safer globals)
4. `architectural_issues.md` (big structural changes, last so they don't conflict with earlier work)
5. `non_future_proof.md` (documentation + defensive checks)
