# claude_test

Scratch area for debug, exploratory, and one-off diagnostic scripts.
These files are **not** part of the CI test suite — production-quality
tests live in `tests/`. See `CLAUDE.md` §3 (Debug File Management) and §8
(Exceptions).

## Conventions

- Name throwaway scripts with a clear prefix: `debug_*`, `scratch_*`,
  `tmp_*`, or `experiment_*`.
- Open each file with a one-line comment stating its purpose.
- When a debug script leads to a real fix, promote the relevant parts into
  a proper test under `tests/` and delete or archive the debug version.
- Add a row to the index below whenever you add a file here.

## Index

| File | Purpose | What was learned |
|------|---------|------------------|
| `verify_control_energy.py` | Toggle each plug ON→OFF via `SmartPlugController`, confirm power rises when ON (safe load attached), then restore original state. | PASS — control works and power rises (plug1 0→92.8 W, plug2 0→44.4 W). Key gotcha: the P110M emeter lags ~4–6 s after a relay change (0 W held ~3 s after ON; last value held ~5 s after OFF), so power must be read after a full settle in the target state or it reads stale. `update()` refreshes correctly; cached `current_consumption` matches a live `get_status()`. |
| `debug_api_client.py` | Smoke-test the FastAPI server (`plug_server`): hits `/health`, `/plugs`, `/plugs/{name}`, `/plugs/{name}/energy`, a 404 negative case, and (with `--switch`) toggles one plug, waits for the emeter to settle, then restores its original state. | PASS — server on port 17046 served all 7 routes. Plug control confirmed by power: plug1 ON→91.5 W (0.415 A), OFF→0.0 W; toggle flipped OFF→ON→OFF; unknown plug → 404. Same ~4–6 s emeter settle applies, so the client waits before reading power after a switch. |
