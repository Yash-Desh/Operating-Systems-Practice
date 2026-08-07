# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A personal study repository for Operating Systems (UW-Madison CS537), not a software project. It holds course PDFs, hand-written study/interview-prep notes, and small C demo programs written to verify concepts empirically. Most work here is **authoring and refining notes**, occasionally backed by a runnable C demo.

## Layout

- `OSTEP/` — the OSTEP textbook chapters as PDFs, grouped into the four pillars: `1_Virtualization_CPU_3-11`, `2_Virtualization_Memory_12-24`, `3_Concurrency_25-34`, `4_Persistence_35-46`. Filenames are prefixed with the book's chapter number.
- `interview_prep/` — long-form markdown study notes, one topic per file (snake_case names).
- `demos/` — small self-contained C programs that prove a concept from the notes. Build artifacts (`a.out`, `*.out`, `*.exe`) are gitignored.
- Root PDFs — Silberschatz *Operating System Concepts* 10th ed. and the xv6 manual, referenced by the notes.
- `README.md` — includes an "Open Topics to Explore Deeper" list the user maintains; new topics get appended as numbered items.

## Building and running C demos

No build system — compile directly. Each demo's build/run commands are in a header comment at the top of the file. Concurrency demos need `-pthread`:

```bash
gcc -Wall -o demos/thread_stack_sharing demos/thread_stack_sharing.c -pthread
./demos/thread_stack_sharing
```

## Conventions for notes and demos

These are established by the existing files — match them when creating or editing content.

- **Header block.** Every note and demo starts with author (Yash Deshpande), date (`DD-MM-YYYY`), and the LLM model used (e.g. `Claude (Opus 4.8)`). Demos carry this as a C comment plus a `Build:`/`Run:` line.
- **Cross-references.** Notes link to each other with Obsidian-style `[[note_filename_without_extension]]` wikilinks, and collect them under a "Related notes" section.
- **Citations with page offsets.** Notes cite sources precisely and record the mapping between printed and PDF page numbers. Always mention both page numbers printed & pdf.
- **Explanatory style.** Notes favor comparison tables, an "interview soundbite" that compresses the idea, and empirical backing (a `demos/` program) where a claim can be demonstrated in code.
