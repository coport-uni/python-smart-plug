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

---

## Verify device_list.md plugs are recognized via secure.env creds

### Background
User request (2026-06-18): `source secure.env` to load TP-Link cloud
credentials, then confirm the two `tapo p110m` plugs listed in
`device_list.md` are recognized over the network.

### Decisions
- Read-only verification only (no config changes, no toggling state).
- Used the working CLI at `/opt/conda/envs/smartplug/bin/kasa` (v0.10.2);
  the repo no longer ships an inline `kasa` package (now an `external/`
  submodule) and `/tmp/kasa-venv/bin/kasa` is broken.
- Loaded creds via `set -a; source secure.env; set +a` in the same shell
  invocation as `kasa` (Bash tool does not persist env between calls).
- Confirmed each device by matching MAC + model against `device_list.md`.

### Results
- plug1 192.168.1.239 — P110M, MAC 18:69:45:71:05:2F ✓, state OFF, HW 1.0 (KR),
  FW 1.2.2, cloud_connection True. Recognized.
- plug2 192.168.1.79  — P110M, MAC 18:69:45:71:02:7C ✓, state ON, HW 1.0 (KR),
  FW 1.2.2, cloud_connection True. Recognized.
- Both MACs/models match `device_list.md` exactly; authenticated state read
  succeeded, so `secure.env` credentials are valid.

### Work items
- [x] Locate working `kasa` CLI and load `secure.env` creds
- [x] Confirm TCP reachability to both plug IPs
- [x] Query `kasa --host <ip> state` for both plugs
- [x] Match MAC/model against `device_list.md`
- [x] Append this ToDo entry
- [x] Create GitHub issue on coport-uni/python-smart-plug (#3) — user
  enabled issues (`hasIssuesEnabled: true`), then issue created:
  https://github.com/coport-uni/python-smart-plug/issues/3

---

## Write main.py to read serial + firmware from device_list.md devices

### Background
User request (2026-06-18): write a `main.py` that loads the device serial
and firmware information for the devices listed in `device_list.md`
(two Tapo P110M plugs, given by MAC + IP). User also instructed to use the
already-created conda env `smartplug` (which has `kasa 0.10.2` installed).

### Decisions
- Run target: conda env `smartplug` (`/opt/conda/envs/smartplug`,
  kasa 0.10.2). No new pip installs; `import kasa` resolves there.
- Tapo P110M uses the SMART protocol -> requires TP-Link cloud credentials.
  Read them from env vars `KASA_USERNAME` / `KASA_PASSWORD` (already in
  `secure.env`, the same names the kasa CLI uses). Never hardcode secrets.
- Parse `device_list.md` (CSV-style: devicetype, name, mac, ip) instead of
  hardcoding hosts, so the list stays the single source of truth.
- Connect per host via `Discover.discover_single(host, credentials=...)`
  then `await dev.update()`; read serial + firmware from the API
  (exact properties confirmed by reading installed kasa source).
- Ground truth from prior verification: both plugs report FW 1.2.2, HW 1.0.

### Work items
- [x] Research installed kasa 0.10.2 API (connect, serial, firmware, creds)
- [x] Write `main.py` (parse list, connect, print serial + firmware)
- [x] Run against the two plugs via conda env `smartplug`; verify output
      (both P110M: FW 1.2.2 Build 240422 Rel.183947, MACs match, exit 0)
- [x] Pass `ruff check` + `ruff format --check`
- [x] Create GitHub issue on coport-uni/python-smart-plug (#4)

---

## Enable issues on the fork and file the restructure issue

### Background
User request (2026-06-18): instead of deleting and recreating the fork as a
new public repo (which needed a `delete_repo` token scope), just enable
GitHub issues on the existing fork `coport-uni/python-smart-plug`.

### Decisions
- Forks disable issues by default; enabled with
  `gh repo edit --enable-issues` (needs only the existing `repo` scope, no
  deletion). Abandoned the earlier delete+recreate plan.
- Filed the previously-blocked restructure issue and linked it to PR #1
  (`Closes #2`). `gh pr edit` hit a Projects-classic GraphQL bug, so the PR
  body was patched via the REST API instead.

### Work items
- [x] Enable issues (`gh repo edit coport-uni/python-smart-plug --enable-issues`)
- [x] Verify `has_issues: true`
- [x] Create restructure issue (#2)
- [x] Link PR #1 to issue #2 (`Closes #2`, via REST API)

---

## Add on/off control, power/energy view, and on/off state to main.py

### Background
User request (2026-06-18): extend `main.py` with (1) on/off control of the
plugs, (2) a view of power/energy usage ("전력량"), and (3) a view of the
on/off state, in addition to the existing serial + firmware report.

### Decisions
- Turn `main.py` into a small argparse CLI (stdlib only), preserving the
  existing serial/firmware report:
  - `status` (default): per device show model, on/off state, current power
    (W) and energy (today / this month / total kWh), plus serial + firmware.
  - `on <target>` / `off <target>`: switch a plug; `target` is a device name
    from `device_list.md` (e.g. `plug1`), an IP, or `all`. Re-read and print
    the resulting state to confirm.
- API (kasa 0.10.2, confirmed by reading installed source):
  - State: `dev.is_on` (bool). Control: `await dev.turn_on()` /
    `await dev.turn_off()`; re-`update()` to observe the new state.
  - Energy: `dev.modules[Module.Energy]` -> `current_consumption` (W),
    `consumption_today` / `consumption_this_month` / `consumption_total`
    (kWh). Guard with `Module.Energy in dev.modules`.
- Verification (user choice): toggle BOTH plugs, confirm transitions, then
  restore each plug to its original state.

### Work items
- [x] Research kasa control/energy API (switch, is_on, Energy module)
- [x] Extend `main.py`: argparse CLI + status (state/power/energy) + on/off
- [x] Verify status/power view (read-only) against both plugs
- [x] Verify on/off: toggle both plugs, confirm, restore original state
      (via `claude_test/verify_control_energy.py`; PASS: plug1 0->92.8 W,
      plug2 0->44.4 W when ON; both restored to OFF)
- [x] Pass `ruff check` + `ruff format --check`
- [x] Create GitHub issue on coport-uni/python-smart-plug (#5)

---

## Refactor: extract SmartPlugController class; keep main.py thin

### Background
User request (2026-06-18): `main.py` has grown too complex. Keep only the
intuitive features (device info read, on/off control, power/energy) in
`main.py`, and move the logic into a `SmartPlugController` class in a new
`smartplugcontroller.py`. `main.py` should import and use that class.

### Decisions
- New `smartplugcontroller.py` holds the `SmartPlugController` class, the data
  classes (`DeviceEntry`, `EnergyReading`, `DeviceReport`, `SwitchResult`),
  and a `ControllerError`. `from_files()` loads credentials + device list
  (defaults resolve beside the module). Methods: `read` / `read_all`,
  `switch` / `switch_many`, `resolve_targets`; static `read_energy` /
  `read_firmware`. Connection/parse/credential details live here.
- `main.py` becomes a thin argparse CLI plus output formatting that delegates
  to the controller. Pure refactor -- no behavior change.
- Update `claude_test/verify_control_energy.py` to use the controller.

### Work items
- [x] Write `smartplugcontroller.py` (class + data classes + helpers)
- [x] Slim `main.py` to CLI + formatting, importing the controller
- [x] Update `verify_control_energy.py` to use `SmartPlugController`
- [x] Re-verify behavior unchanged: `status` + on/off (run both)
      (status OK; on/off plug1 OFF->ON->OFF; unknown target -> clean error,
      exit 2; power-rise PASS plug1 0->91.2 W, plug2 0->41.6 W, restored)
- [x] Pass `ruff check` + `ruff format --check`
- [x] Create GitHub issue on coport-uni/python-smart-plug (#7)

---

## Consolidate device logic into one self-contained SmartPlugController

### Background
User request (2026-06-18): consolidate into "one smartplugcontroller". Chosen
option (via clarifying question): fold the module-level helper functions into
the `SmartPlugController` class so it is a single self-contained class, while
keeping `main.py` as the thin CLI (preserve the import separation).

### Decisions
- Move `load_secret_env` / `read_credentials` / `parse_device_list` into
  `SmartPlugController` as private helpers (`_load_secret_env` staticmethod,
  `_read_credentials` classmethod, `_parse_device_list` staticmethod).
- Move the config constants (`username_env`, `password_env`, default file
  paths) into the class as class attributes; `from_files` uses them.
- Keep the data classes (`DeviceEntry`, `EnergyReading`, `DeviceReport`,
  `SwitchResult`) and `ControllerError` at module level -- `main.py` imports
  them. No public API change, so `main.py` and the verify script are
  unaffected.

### Work items
- [x] Rewrite `smartplugcontroller.py`: fold helpers + constants into the class
- [x] Confirm `main.py` and verify script still work unchanged
      (imports intact; no public API change)
- [x] Re-verify on hardware: `status` + on/off (restore state)
      (status OK; on/off plug1 OFF->ON->OFF; error path exit 2; power-rise
      PASS plug1 0->91.5 W, plug2 0->41.8 W, restored)
- [x] Pass `ruff check` + `ruff format --check`
- [x] Create GitHub issue on coport-uni/python-smart-plug (#8)

---

## Bundle all issues into PR #1 and merge to master

### Background
User request (2026-06-18): link all open issues to the restructure PR and
merge it.

### Decisions
- Linked both open issues to PR #1 via the body (`Closes #2`, `Closes #3`)
  so the merge auto-closes them. Used the REST API for the body edit because
  `gh pr edit` hit the Projects-classic GraphQL bug.
- Merge method: merge commit (`--merge`), preserving the two conventional
  commits; deleted the remote branch on merge (`--delete-branch`).
- Synced local `master` to the merge commit by moving the branch pointer
  (no working-tree churn) and deleted the merged local branch.

### Work items
- [x] Add `Closes #2` / `Closes #3` to PR #1 body (REST API)
- [x] Merge PR #1 to `master` (merge commit `dc6e4b9`), delete remote branch
- [x] Confirm issues #2 and #3 are CLOSED
- [x] Fast-forward local `master` to `dc6e4b9`; delete local branch
- [x] `git submodule update --init --recursive`

---

## Build a FastAPI server to control the plugs over HTTP (port 17046)

### Background
User request (2026-06-18): build a FastAPI server that controls the smart
plugs, listening on port **17046**. An ESP32 (and other LAN clients) will
later connect to this server over HTTP to switch the plugs and read their
state/power. Plan approved in `/root/.claude/plans/fastapi-cached-flame.md`.

### Decisions (confirmed with the user)
- Runtime: existing conda env `smartplug` (kasa 0.10.2); add `fastapi` and
  `uvicorn` there. Bind `0.0.0.0:17046` so LAN clients can reach it.
- Auth: none (LAN-only trust); no API key for now.
- HTTP style: POST for state changes (`/on`, `/off`, `/toggle`), GET for
  reads (`/plugs`, `/plugs/{name}`, `/plugs/{name}/energy`) -- the standard
  REST convention so GET stays side-effect-free.
- Reuse the proven logic in `main.py` by extracting the shared helpers
  (`read_credentials`, `parse_device_list`, `DeviceEntry`, `read_firmware`,
  `load_secret_env`) into a new `device_common.py`; `main.py` then imports
  them (behavior unchanged).
- New `plug_server/` package: `models.py` (Pydantic response models),
  `manager.py` (`PlugManager`: per-device connection cache + `asyncio.Lock`
  + reconnect-once-on-failure), `app.py` (FastAPI app, lifespan, routes,
  exception handlers). Device names come from `device_list.md`.
- Respect the P110M emeter lag (~4-6 s after a relay change, see
  `claude_test/README.md`) when verifying power after a toggle.

### Work items
- [ ] Create GitHub issue on coport-uni/python-smart-plug
- [ ] Cut branch `feature/fastapi-plug-server` from `master`
- [ ] Install `fastapi` + `uvicorn[standard]` into conda env `smartplug`
- [ ] Extract shared helpers into `device_common.py`; refactor `main.py`
- [ ] Implement `plug_server/` (models, manager, app) on port 17046
- [ ] Add `run_server.sh` launcher
- [ ] Add `claude_test/debug_api_client.py`; update `claude_test/README.md`
- [ ] Pass `ruff check` + `ruff format --check` on all new files
- [ ] Verify end-to-end (health/list/state/on/off/toggle/energy via curl)
- [ ] Open PR -> coport-uni/python-smart-plug @ master
