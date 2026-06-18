"""Command-line interface for the Tapo smart plugs in ``device_list.md``.

This is a thin front end over :class:`smartplugcontroller.SmartPlugController`:
the :class:`SmartPlugCli` class here only parses arguments and formats output.
All device logic -- connecting, authenticating, reading status/energy,
switching -- lives in the controller.

Commands::

    conda run -n smartplug python main.py                # status (default)
    conda run -n smartplug python main.py status
    conda run -n smartplug python main.py on  plug1      # by name, IP, or 'all'
    conda run -n smartplug python main.py off all

``status`` shows, per device, the on/off state, current power and energy rate,
plus the serial number and firmware. ``on``/``off`` switch a plug
and re-read it to confirm the resulting state.
"""

from __future__ import annotations

import argparse
import asyncio
import sys

from smartplugcontroller import (
    ControllerError,
    DeviceEntry,
    DeviceReport,
    SmartPlugController,
    SwitchResult,
)

# Decorators used in this module. A decorator is the ``@name`` line written
# just above a function or method ("external" annotation): Python passes the
# function below it to ``name`` and replaces it with whatever ``name`` returns.
#   @staticmethod -- a method that takes neither ``self`` nor ``cls``; it is a
#       plain function grouped under the class. Used for the pure helpers here
#       (the ``_format_*`` formatters and ``_build_parser``).
#   @classmethod  -- a method that receives the class as its first argument
#       ``cls`` instead of an instance. Used by the formatters that call their
#       sibling helpers via ``cls._format_*``.


class SmartPlugCli:
    """Command-line front end for :class:`SmartPlugController`.

    Parses arguments, formats device reports for the terminal, and dispatches
    each command to the controller. Holds no device logic itself.
    """

    @staticmethod
    def _format_measure(value: float | None, unit: str, places: int = 3) -> str:
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

    @staticmethod
    def _format_state(is_on: bool | None) -> str:
        """Render an on/off state as ``ON``, ``OFF``, or ``unknown``."""
        if is_on is None:
            return "unknown"
        return "ON" if is_on else "OFF"

    @classmethod
    def _format_report(cls, report: DeviceReport) -> str:
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
        lines = [header, f"  state:              {cls._format_state(report.is_on)}"]
        energy = report.energy
        if energy is None:
            lines.append("  power/energy:       not reported by this device")
        else:
            lines.append(
                f"  power (now):        {cls._format_measure(energy.power_w, 'W', 1)}"
            )
            lines.append(
                f"  energy (today):     {cls._format_measure(energy.today_kwh, 'kWh')}"
            )
            lines.append(
                f"  energy (month):     {cls._format_measure(energy.month_kwh, 'kWh')}"
            )
            if energy.voltage_v is not None or energy.current_a is not None:
                volt = cls._format_measure(energy.voltage_v, "V", 1)
                amp = cls._format_measure(energy.current_a, "A")
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

    @classmethod
    def _format_switch(cls, result: SwitchResult) -> str:
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
        before = cls._format_state(result.before)
        after = cls._format_state(result.after)
        return f"{header}: {before} -> {after}"

    @staticmethod
    def _build_parser() -> argparse.ArgumentParser:
        """Build the command-line parser for the status/on/off/demo commands."""
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
        subcommands.add_parser(
            "demo",
            help="Demo the non-CLI functions (status, then toggle plug1 + restore).",
        )
        return parser

    def _run_status(self, controller: SmartPlugController) -> int:
        """Read every device and print a status report.

        Args:
            controller: The controller to read from.

        Returns:
            ``0`` if every device was read successfully, otherwise ``1``.
        """
        entries = controller.entries
        print(f"Reading {len(entries)} device(s) ...\n")
        reports = asyncio.run(controller.read_all())
        print("\n\n".join(self._format_report(report) for report in reports))
        successes = sum(report.ok for report in reports)
        print(f"\nRead {successes}/{len(reports)} device(s) successfully.")
        return 0 if successes == len(reports) else 1

    def _run_switch(
        self, controller: SmartPlugController, target: str, *, turn_on: bool
    ) -> int:
        """Switch the targeted devices and print the resulting states.

        Args:
            controller: The controller to operate.
            target: The device name, IP, or ``"all"`` to switch.
            turn_on: ``True`` to turn on, ``False`` to turn off.

        Returns:
            ``0`` if every targeted device switched successfully, else ``1``.

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
        print("\n".join(self._format_switch(result) for result in results))
        successes = sum(result.ok for result in results)
        print(f"\nSwitched {successes}/{len(results)} device(s) successfully.")
        return 0 if successes == len(results) else 1

    def run(self, argv: list[str] | None = None) -> int:
        """Parse arguments and run the requested command.

        Args:
            argv: The argument list, defaulting to ``sys.argv`` when ``None``.

        Returns:
            The process exit code.
        """
        args = self._build_parser().parse_args(argv)
        try:
            if args.command == "demo":
                main()
                return 0
            controller = SmartPlugController.from_files()
            if args.command in ("on", "off"):
                return self._run_switch(
                    controller, args.target, turn_on=args.command == "on"
                )
            return self._run_status(controller)
        except ControllerError as error:
            print(f"error: {error}", file=sys.stderr)
            return 2


async def _read_targets(
    controller: SmartPlugController, targets: list[DeviceEntry]
) -> list[DeviceReport]:
    """Read the given devices concurrently."""
    return await asyncio.gather(*(controller.read(entry) for entry in targets))


def status(
    target: str = "all", *, controller: SmartPlugController | None = None
) -> list[DeviceReport]:
    """Read plug status without the CLI.

    A synchronous convenience wrapper around :class:`SmartPlugController` for
    use from a REPL or another script.

    Args:
        target: A device name (e.g. ``"plug1"``), an IP address, or ``"all"``.
        controller: An existing controller to reuse; one is built from
            ``device_list.md`` / ``secure.env`` when ``None``.

    Returns:
        One :class:`DeviceReport` per matched device, in list order; empty if
        ``target`` matches no device. Inspect e.g. ``report.is_on`` or
        ``report.energy.power_w``.
    """
    controller = controller or SmartPlugController.from_files()
    targets = controller.resolve_targets(target)
    return asyncio.run(_read_targets(controller, targets))


def turn_on(
    target: str, *, controller: SmartPlugController | None = None
) -> list[SwitchResult]:
    """Turn one or more plugs on without the CLI.

    Args:
        target: A device name (e.g. ``"plug1"``), an IP address, or ``"all"``.
        controller: An existing controller to reuse; one is built from files
            when ``None``.

    Returns:
        One :class:`SwitchResult` per matched device; empty if none matched.
    """
    return _set_power(target, turn_on=True, controller=controller)


def turn_off(
    target: str, *, controller: SmartPlugController | None = None
) -> list[SwitchResult]:
    """Turn one or more plugs off without the CLI.

    Args:
        target: A device name (e.g. ``"plug1"``), an IP address, or ``"all"``.
        controller: An existing controller to reuse; one is built from files
            when ``None``.

    Returns:
        One :class:`SwitchResult` per matched device; empty if none matched.
    """
    return _set_power(target, turn_on=False, controller=controller)


def _set_power(
    target: str, *, turn_on: bool, controller: SmartPlugController | None
) -> list[SwitchResult]:
    """Resolve ``target`` and switch the matched devices on or off."""
    controller = controller or SmartPlugController.from_files()
    targets = controller.resolve_targets(target)
    return asyncio.run(controller.switch_many(targets, turn_on=turn_on))


def main() -> None:
    """Demonstrate the non-CLI functions: read status, then switch a plug.

    Invoked by ``python main.py demo``. It reads every plug with
    :func:`status`, then exercises :func:`turn_on` and :func:`turn_off` on
    ``plug1``, finally restoring ``plug1`` to the state it started in.
    """
    print("== status(): read every plug ==")
    for report in status():
        if not report.ok:
            print(f"  {report.entry.name}: ERROR {report.error}")
            continue
        power = report.energy.power_w if report.energy else None
        print(
            f"  {report.entry.name}: on={report.is_on} "
            f"power={power} W fw={report.firmware}"
        )

    demo_plug = "plug1"
    was_on = status(demo_plug)[0].is_on
    print(f"\n== turn_on({demo_plug!r}) then turn_off({demo_plug!r}) ==")
    for result in turn_on(demo_plug):
        print(f"  on : {result.entry.name} {result.before} -> {result.after}")
    for result in turn_off(demo_plug):
        print(f"  off: {result.entry.name} {result.before} -> {result.after}")

    # Put the plug back the way the demo found it.
    state = "ON" if was_on else "OFF"
    if was_on:
        turn_on(demo_plug)
    print(f"\nrestored {demo_plug} to its original state ({state}).")


if __name__ == "__main__":
    sys.exit(SmartPlugCli().run())
