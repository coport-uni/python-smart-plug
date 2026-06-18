# Diagnostic: verify REAL on/off control on the plugs in device_list.md and
# confirm that power rises when ON (a safe load is attached to plug1/plug2),
# then restore each plug to its original state via try/finally.
#
# Run: conda run -n smartplug python claude_test/verify_control_energy.py
#
# Reuses SmartPlugController from ../smartplugcontroller.py for credentials,
# the device list, and energy reads. Toggling is driven at a low level here so
# we can insert the emeter settle delays the controller's switch() does not.

import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kasa import Discover  # noqa: E402
from smartplugcontroller import SmartPlugController  # noqa: E402

# Seconds to let the relay settle and the on-device emeter catch up before we
# read power. Measured behaviour on the P110M: after turn_on the meter stays 0
# for ~3s then jumps to the load value (~t+4s); after turn_off it holds the
# last value until ~t+6s then drops to 0. So power must be read AFTER a full
# settle in the target state, not right after the switch, or it reads stale.
SETTLE_SECONDS = 7

# Minimum power rise (W) that counts as "the load is really drawing power".
POWER_RISE_MIN_W = 1.0


def state_str(is_on):
    return "unknown" if is_on is None else ("ON" if is_on else "OFF")


def power_str(watts):
    return "n/a" if watts is None else f"{watts:.2f} W"


def power_of(dev):
    reading = SmartPlugController.read_energy(dev)
    return None if reading is None else reading.power_w


async def measure(dev, *, on):
    # Drive to the target state, wait out the emeter lag, then read power.
    await dev.set_state(on)
    await asyncio.sleep(SETTLE_SECONDS)
    await dev.update()
    return dev.is_on, power_of(dev)


async def verify_one(entry, credentials):
    dev = await Discover.discover_single(entry.ip, credentials=credentials)
    await dev.update()
    original = dev.is_on
    print(f"\n=== {entry.name} @ {entry.ip} ===")
    print(f"original state : {state_str(original)}")
    on_power = None
    off_power = None
    transitions_ok = False
    try:
        # Baseline first (OFF, fully settled), then ON, so each power read is
        # taken after the meter has caught up to that state.
        off_state, off_power = await measure(dev, on=False)
        print(
            f"OFF (settled)  : state={state_str(off_state)}, power={power_str(off_power)}"
        )

        on_state, on_power = await measure(dev, on=True)
        print(
            f"ON  (settled)  : state={state_str(on_state)}, power={power_str(on_power)}"
        )

        transitions_ok = on_state is True and off_state is False
    finally:
        # Always put the plug back the way we found it, even on error.
        await dev.set_state(original)
        await dev.update()
        print(f"restored state : {state_str(dev.is_on)} (target {state_str(original)})")
        await dev.disconnect()

    power_rises = (on_power or 0.0) - (off_power or 0.0) >= POWER_RISE_MIN_W
    return {
        "name": entry.name,
        "transitions_ok": transitions_ok,
        "power_rises": power_rises,
        "on_power": on_power,
        "off_power": off_power,
    }


async def run():
    controller = SmartPlugController.from_files()
    credentials = controller.credentials
    # Sequential, not concurrent: clearer logs and no contention while toggling.
    results = [await verify_one(entry, credentials) for entry in controller.entries]

    print("\n==== SUMMARY ====")
    all_ok = True
    for result in results:
        ok = result["transitions_ok"] and result["power_rises"]
        all_ok = all_ok and ok
        print(
            f"{result['name']}: "
            f"on/off={'OK' if result['transitions_ok'] else 'FAIL'}, "
            f"power_rises_when_on={'OK' if result['power_rises'] else 'FAIL'} "
            f"(on={result['on_power']} W, off={result['off_power']} W)"
        )
    print("RESULT:", "PASS" if all_ok else "FAIL")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(run()))
