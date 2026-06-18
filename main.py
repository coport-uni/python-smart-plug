"""Command-line interface for the Tapo smart plugs in ``device_list.md``.

This is a thin front end over :class:`smartplugcontroller.SmartPlugController`:
it only parses arguments and formats output. All device logic -- connecting,
authenticating, reading status/energy, switching -- lives in the controller.

Commands::

    conda run -n smartplug python main.py                # status (default)
    conda run -n smartplug python main.py status
    conda run -n smartplug python main.py on  plug1      # by name, IP, or 'all'
    conda run -n smartplug python main.py off all

``status`` shows, per device, the on/off state, current power and energy
("전력량"), plus the serial number and firmware. ``on``/``off`` switch a plug
and re-read it to confirm the resulting state.
"""

from __future__ import annotations

import argparse
import asyncio
import sys

from smartplugcontroller import (
    ControllerError,
    DeviceReport,
    SmartPlugController,
    SwitchResult,
)


def format_measure(value: float | None, unit: str, places: int = 3) -> str:
    """Format an optional numeric measurement with its unit.

    Args:
        value: The measurement, or ``None`` if the device did not report it.
        unit: The unit suffix, e.g. ``"W"`` or ``"kWh"``.
        places: The number of decimal places to show.

    Returns:
        The formatted value, or ``"n/a"`` when ``value`` is ``None``.
    """
    if value is None:
        return "n/a"
    return f"{value:.{places}f} {unit}"


def format_state(is_on: bool | None) -> str:
    """Render an on/off state as ``ON``, ``OFF``, or ``unknown``."""
    if is_on is None:
        return "unknown"
    return "ON" if is_on else "OFF"


def format_report(report: DeviceReport) -> str:
    """Render one status report as an indented, human-readable block.

    Args:
        report: The result of reading a single device.

    Returns:
        A multi-line string describing the device or its failure.
    """
    entry = report.entry
    header = f"{entry.name} ({entry.device_type}) @ {entry.ip}"
    if not report.ok:
        return f"{header}\n  ERROR: {report.error}"
    expected_mac = entry.mac.upper().replace("-", ":")
    mac_note = (
        "matches device_list"
        if (report.mac or "").upper() == expected_mac
        else "MISMATCH with device_list"
    )
    lines = [header, f"  state:              {format_state(report.is_on)}"]
    energy = report.energy
    if energy is None:
        lines.append("  power/energy:       not reported by this device")
    else:
        lines.append(f"  power (now):        {format_measure(energy.power_w, 'W', 1)}")
        lines.append(f"  energy (today):     {format_measure(energy.today_kwh, 'kWh')}")
        lines.append(f"  energy (month):     {format_measure(energy.month_kwh, 'kWh')}")
        if energy.voltage_v is not None or energy.current_a is not None:
            volt = format_measure(energy.voltage_v, "V", 1)
            amp = format_measure(energy.current_a, "A")
            lines.append(f"  voltage / current:  {volt} / {amp}")
    lines.extend(
        [
            f"  serial (device_id): {report.serial}",
            f"  firmware:           {report.firmware}",
            f"  model:              {report.model}",
            f"  mac:                {report.mac}  ({mac_note})",
        ]
    )
    return "\n".join(lines)


def format_switch(result: SwitchResult) -> str:
    """Render one switch result as a single line.

    Args:
        result: The outcome of switching a single device.

    Returns:
        A one-line summary of the transition or the failure.
    """
    entry = result.entry
    header = f"{entry.name} ({entry.device_type}) @ {entry.ip}"
    if not result.ok:
        return f"{header}: ERROR: {result.error}"
    return f"{header}: {format_state(result.before)} -> {format_state(result.after)}"


def build_parser() -> argparse.ArgumentParser:
    """Build the command-line parser for the status/on/off commands."""
    parser = argparse.ArgumentParser(
        description="Inspect and control the smart plugs in device_list.md."
    )
    subcommands = parser.add_subparsers(dest="command")
    subcommands.add_parser(
        "status", help="Show state, power/energy, serial and firmware (default)."
    )
    for action, verb in (("on", "Turn on"), ("off", "Turn off")):
        control = subcommands.add_parser(action, help=f"{verb} one or more plugs.")
        control.add_argument(
            "target", help="Device name (e.g. plug1), IP address, or 'all'."
        )
    return parser


def run_status(controller: SmartPlugController) -> int:
    """Read every device and print a status report.

    Args:
        controller: The controller to read from.

    Returns:
        ``0`` if every device was read successfully, otherwise ``1``.
    """
    entries = controller.entries
    print(f"Reading {len(entries)} device(s) ...\n")
    reports = asyncio.run(controller.read_all())
    print("\n\n".join(format_report(report) for report in reports))
    successes = sum(report.ok for report in reports)
    print(f"\nRead {successes}/{len(reports)} device(s) successfully.")
    return 0 if successes == len(reports) else 1


def run_switch(controller: SmartPlugController, target: str, *, turn_on: bool) -> int:
    """Switch the targeted devices and print the resulting states.

    Args:
        controller: The controller to operate.
        target: The device name, IP, or ``"all"`` to switch.
        turn_on: ``True`` to turn on, ``False`` to turn off.

    Returns:
        ``0`` if every targeted device switched successfully, otherwise ``1``.

    Raises:
        ControllerError: If ``target`` matches no device in the list.
    """
    targets = controller.resolve_targets(target)
    if not targets:
        names = ", ".join(entry.name for entry in controller.entries)
        raise ControllerError(f"No device matches '{target}'. Known: {names}, all.")
    verb = "on" if turn_on else "off"
    print(f"Turning {verb}: {', '.join(entry.name for entry in targets)} ...\n")
    results = asyncio.run(controller.switch_many(targets, turn_on=turn_on))
    print("\n".join(format_switch(result) for result in results))
    successes = sum(result.ok for result in results)
    print(f"\nSwitched {successes}/{len(results)} device(s) successfully.")
    return 0 if successes == len(results) else 1


def main(argv: list[str] | None = None) -> int:
    """Parse arguments and run the requested command.

    Args:
        argv: The argument list, defaulting to ``sys.argv`` when ``None``.

    Returns:
        The process exit code.
    """
    args = build_parser().parse_args(argv)
    try:
        controller = SmartPlugController.from_files()
        if args.command in ("on", "off"):
            return run_switch(controller, args.target, turn_on=args.command == "on")
        return run_status(controller)
    except ControllerError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
