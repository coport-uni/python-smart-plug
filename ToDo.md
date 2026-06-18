# ToDo

Append-only command history for Claude Code sessions on python-kasa.
See `CLAUDE.md` §4 (Task Management). Do not rewrite or reorder existing
entries; only append new tasks and flip `- [ ]` to `- [x]` as work
completes.

---

## Apply CommonClaude conventions and hooks to python-kasa

### Background
User request (2026-06-18): apply the conventions in
`CommonClaude/CLAUDE.md` and the Claude Code hooks from
`CommonClaude/.claude/` across the python-kasa repository.

### Decisions
- Copy `CommonClaude/CLAUDE.md` to the repo root largely verbatim, adding a
  "Project Overrides" block for the facts that differ in this repo:
  88-column line length (Ruff default, not 80), `master` trunk (not
  `main`), pre-existing `.gitignore` and `.pre-commit-config.yaml`, `uv`
  package manager, and the project's configured Ruff rule set.
- Install the five hook scripts and `settings.json` under `.claude/`
  verbatim (they are already Python-flavored / language-neutral).
- Do not alter `pyproject.toml`, `.gitignore`, or `.pre-commit-config.yaml`
  — extend, never replace.

### Work items
- [x] Read `CommonClaude/CLAUDE.md`, hooks, and `settings.json`
- [x] Verify toolchain (`jq`, `ruff`, `gh`, git remote, effective 88-col)
- [x] Create `.claude/hooks/` and copy the five hook scripts (chmod +x)
- [x] Copy `.claude/settings.json`
- [x] Write root `CLAUDE.md` (verbatim + Project Overrides + §18 hooks doc)
- [x] Create `claude_test/README.md` index
- [x] Create this `ToDo.md`
- [x] Verify each hook fires correctly on matching/non-matching input
- [ ] (Optional, per §4) Create GitHub issue and PR for this change

---

## Extract python-kasa and CommonClaude into external/ submodules

### Background
User request (2026-06-18): convert the inline python-kasa source and the
nested CommonClaude clone into git submodules collected under one folder.

### Decisions
- Parent repo: reuse existing `coport-uni/python-smart-plug` (user choice).
- `python-kasa` submodule points to upstream `python-kasa/python-kasa`
  (user choice), pinned to `76d9f68` — the exact commit previously inlined,
  so no behavior change.
- `CommonClaude` submodule points to `coport-uni/CommonClaude` @ `e1fa139`;
  registered the existing local clone (no re-clone) so its uncommitted
  working-tree deletions were preserved.
- Layout: both submodules collected under `external/` (user changed from an
  initial "separate" choice to "both in external/").
- Added `.gitignore` to keep `secure.env` (credentials) and build artifacts
  out of git.
- Work done on branch `restructure-submodules`; not pushed.

### Work items
- [x] Verify remotes reachable; confirm parent HEAD == upstream master
- [x] Create branch `restructure-submodules`
- [x] `git rm` 526 inlined python-kasa files; clean build artifacts
- [x] Add `external/python-kasa` submodule (upstream, @76d9f68)
- [x] Add `external/CommonClaude` submodule (existing clone, @e1fa139)
- [x] Add `.gitignore` protecting `secure.env` + artifacts; verify ignored
- [x] Commit restructure (`7a387c4`)
- [ ] (Pending user) Push branch / merge to master
- [ ] (Pending user) Decide whether to track own files (CLAUDE.md, etc.)
- [ ] (Optional) Audit history for prior `secure.env` exposure

---

## Restrict issues/PRs to user's fork; open PR for the restructure

### Background
User request (2026-06-18): open a PR to the user's own repository
(`coport-uni/python-smart-plug`) for the submodule restructure, and add a
CLAUDE.md rule that issues and PRs target the user's fork only unless the
user explicitly names another repository.

### Decisions
- Add a new "Target Repository (Issues & PRs)" rule under §4 Task
  Management, plus a cross-reference in §12.1. Emphasize that `gh` inside an
  `external/*` submodule defaults to that submodule's upstream, so always
  pass `--repo` or run from the superproject root.
- Track `CLAUDE.md` and `ToDo.md` in the parent repo and include them in the
  PR so the rule is version-controlled.
- Create the issue and PR against `coport-uni/python-smart-plug` only.

### Work items
- [x] Add §4 Target Repository rule + §12.1 cross-reference to CLAUDE.md
- [x] Append this ToDo entry
- [x] Commit docs change (track CLAUDE.md + ToDo.md)
- [x] Push branch `restructure-submodules` to origin
- [x] Create GitHub issue on coport-uni/python-smart-plug
- [x] Open PR -> coport-uni/python-smart-plug @ master
