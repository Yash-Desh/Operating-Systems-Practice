# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A personal study repository for Operating Systems (UW-Madison CS537), not a software project. It holds course PDFs, hand-written study/interview-prep notes, and small C demo programs written to verify concepts empirically. Most work here is **authoring and refining notes**, occasionally backed by a runnable C demo.

## Layout

Top-level directories are numbered to give them a stable reading order.

- `1_cs537_lecture_slides/` — course slides (`slides/`) and annotated notes (`notes/`) as PDFs.
- `2_OSTEP/` — the OSTEP textbook chapters as PDFs, grouped into the four pillars: `1_Virtualization_CPU_3-11`, `2_Virtualization_Memory_12-24`, `3_Concurrency_25-34`, `4_Persistence_35-46`. Filenames are prefixed with the book's chapter number.
- `3_xv6/` — the xv6 manual (`xv6-manual.pdf`).
- `4_OS_Books/` — Silberschatz *Operating System Concepts* 10th ed.
- `5_ldd3_pdf/` — *Linux Device Drivers* 3rd ed., one PDF per chapter (`ch01.pdf` … `ch18.pdf`).
- `6_interview_prep/` — long-form markdown study notes, one topic per file. Names are snake_case with a numeric prefix giving reading order (`1_os_boot_to_first_process.md`, `2_init_the_first_user_process.md`, …); wikilinks between notes include that prefix.
- `7_demos/` — small self-contained C programs that prove a concept from the notes. Build artifacts (`a.out`, `*.out`, `*.exe`, and the extensionless compiled binaries) are gitignored.
- `README.md` — includes an "Open Topics to Explore Deeper" list the user maintains; new topics get appended as numbered items.

## Building and running C demos

No build system — compile directly. Each demo's build/run commands are in a header comment at the top of the file. Concurrency demos need `-pthread`:

```bash
gcc -Wall -o 7_demos/thread_stack_sharing 7_demos/thread_stack_sharing.c -pthread
./7_demos/thread_stack_sharing
```

## Conventions for notes and demos

These are established by the existing files — match them when creating or editing content.

- **Header block.** Every note and demo starts with author (Yash Deshpande), date (`DD-MM-YYYY`), and the LLM model used (e.g. `Claude (Opus 4.8)`). Demos carry this as a C comment plus a `Build:`/`Run:` line.
- **Cross-references.** Notes link to each other with Obsidian-style `[[note_filename_without_extension]]` wikilinks, and collect them under a "Related notes" section.
- **Citations with page offsets.** Notes cite sources precisely and record the mapping between printed and PDF page numbers. Always mention both page numbers printed & pdf.
- **Explanatory style.** Notes favor comparison tables, an "interview soundbite" that compresses the idea, and empirical backing (a `demos/` program) where a claim can be demonstrated in code.
