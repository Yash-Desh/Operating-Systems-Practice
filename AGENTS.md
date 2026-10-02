# Repo conventions

Personal study repo for UW–Madison CS537 (OS) and CS736 (Advanced OS):
lecture slides, OSTEP/xv6 material, reference books, hand-written interview
notes, small C demos, and course reading papers.

## Git commits

- Do **not** append LLM/AI attribution trailers to commit messages. No
  `Co-Authored-By: Claude ...`, no `Generated with Claude Code`, no
  equivalent trailer naming any model or tool.
- Commit messages should read as if written by the repo author: a concise
  subject line plus body bullets describing the change.

## Layout

Top-level directories are prefixed with a reading-order number
(`1_cs537_lecture_slides/` … `8_cs736_papers/`). Keep that convention when
adding a new top-level directory.

## Ignored files

Downloaded archives (`*.zip`), compiled binaries under `7_demos/`, and
Windows/WSL junk files (`*:Zone.Identifier`, etc.) are gitignored. Extract
archives into a tracked directory rather than committing the archive.
