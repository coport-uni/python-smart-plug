# Smoke-test the plug-control API against a running server: hit every endpoint
# and print the JSON responses. Read-only by default; pass --switch to also
# exercise on/off/toggle on one plug, then restore its original state.
#
# Usage (server must already be running on the target host/port):
#   conda run -n smartplug python claude_test/debug_api_client.py
#   conda run -n smartplug python claude_test/debug_api_client.py --switch
#   ... --base http://192.168.1.50:17046 --plug plug2 --switch

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

# The P110M emeter lags a few seconds behind a relay change (see
# claude_test/README.md), so wait this long before reading power after a switch.
emeter_settle_seconds = 6


def call(method: str, url: str) -> tuple[int, object]:
    """Send one request and return (status_code, parsed_json_or_text)."""
    request = urllib.request.Request(url, method=method)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            raw = response.read().decode()
            return response.status, json.loads(raw) if raw else None
    except urllib.error.HTTPError as error:
        raw = error.read().decode()
        try:
            return error.code, json.loads(raw)
        except json.JSONDecodeError:
            return error.code, raw
    except urllib.error.URLError as error:
        print(f"  CONNECTION ERROR: {error.reason}", file=sys.stderr)
        raise SystemExit(2) from error


def show(label: str, method: str, url: str) -> object:
    """Run one call and pretty-print its outcome; return the body."""
    status, body = call(method, url)
    print(f"{label}: {method} {url}")
    print(f"  -> {status}  {json.dumps(body, ensure_ascii=False)}")
    return body


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="http://127.0.0.1:17046")
    parser.add_argument(
        "--plug", default=None, help="Plug name; default: first listed."
    )
    parser.add_argument(
        "--switch",
        action="store_true",
        help="Also toggle the plug and restore its original state.",
    )
    args = parser.parse_args()
    base = args.base.rstrip("/")

    show("health", "GET", f"{base}/health")
    plugs = show("list", "GET", f"{base}/plugs")
    if not isinstance(plugs, list) or not plugs:
        print("No plugs listed; aborting.", file=sys.stderr)
        return 1

    name = args.plug or plugs[0]["name"]
    print(f"\nUsing plug: {name}\n")

    state = show("state", "GET", f"{base}/plugs/{name}")
    show("energy", "GET", f"{base}/plugs/{name}/energy")

    # Negative check: an unknown plug must come back as 404.
    print()
    show("unknown (expect 404)", "GET", f"{base}/plugs/__nope__")

    if not args.switch:
        print("\n(read-only run; pass --switch to exercise control)")
        return 0

    original_on = state.get("is_on") if isinstance(state, dict) else None
    print(f"\nOriginal state of {name}: is_on={original_on}")

    print("\n-- toggle --")
    show("toggle", "POST", f"{base}/plugs/{name}/toggle")
    print(f"  waiting {emeter_settle_seconds}s for the emeter to settle ...")
    time.sleep(emeter_settle_seconds)
    show("state", "GET", f"{base}/plugs/{name}")
    show("energy", "GET", f"{base}/plugs/{name}/energy")

    print("\n-- restore original state --")
    action = "on" if original_on else "off"
    show(f"restore ({action})", "POST", f"{base}/plugs/{name}/{action}")
    final = show("state", "GET", f"{base}/plugs/{name}")
    final_on = final.get("is_on") if isinstance(final, dict) else None
    ok = final_on == original_on
    print(
        f"\nRestored: is_on={final_on} (original={original_on}) -> {'OK' if ok else 'MISMATCH'}"
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
