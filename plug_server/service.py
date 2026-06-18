"""Service layer that adapts ``SmartPlugController`` to the HTTP API.

Wraps a :class:`smartplugcontroller.SmartPlugController` with three things the
controller does not provide on its own:

* a name index, so plugs are addressed by their ``device_list.md`` name;
* a per-plug lock, so concurrent requests for the same plug do not open
  competing sessions to it (different plugs still run concurrently); and
* a single retry on transient device failures (a Tapo session can expire, and
  each controller call reconnects from scratch).

Failures are raised as typed errors that the FastAPI layer maps to HTTP status
codes, keeping the route handlers free of error-translation logic.
"""

from __future__ import annotations

import asyncio

from smartplugcontroller import (
    DeviceEntry,
    DeviceReport,
    SmartPlugController,
    SwitchResult,
)


class UnknownPlugError(LookupError):
    """Raised when a request names a plug that is not in the device list."""


class EnergyUnsupportedError(RuntimeError):
    """Raised when energy data is requested from a plug that has no meter."""


class PlugDeviceError(RuntimeError):
    """Raised when a device operation fails (unreachable, auth, or timeout)."""


class PlugService:
    """Read and switch plugs addressed by their ``device_list.md`` name."""

    def __init__(self, controller: SmartPlugController) -> None:
        """Index the controller's devices by name and create their locks.

        Args:
            controller: The controller that owns the device logic.
        """
        self._controller = controller
        self._by_name: dict[str, DeviceEntry] = {
            entry.name: entry for entry in controller.entries
        }
        self._locks: dict[str, asyncio.Lock] = {
            name: asyncio.Lock() for name in self._by_name
        }

    @property
    def device_count(self) -> int:
        """Return the number of configured plugs."""
        return len(self._by_name)

    def list_entries(self) -> list[DeviceEntry]:
        """Return the configured plugs, in device-list order."""
        return self._controller.entries

    def _entry(self, name: str) -> DeviceEntry:
        """Return the entry for ``name`` or raise :class:`UnknownPlugError`."""
        try:
            return self._by_name[name]
        except KeyError:
            raise UnknownPlugError(name) from None

    async def get_state(self, name: str) -> DeviceReport:
        """Read the current status of one plug.

        Args:
            name: The device-list name of the plug.

        Returns:
            The successful device report.

        Raises:
            UnknownPlugError: If ``name`` is not configured.
            PlugDeviceError: If the device could not be read.
        """
        entry = self._entry(name)
        async with self._locks[name]:
            report = await self._read(entry)
        if not report.ok:
            raise PlugDeviceError(report.error or "unknown error")
        return report

    async def get_energy(self, name: str) -> DeviceReport:
        """Read one plug's status and require it to expose energy data.

        Args:
            name: The device-list name of the plug.

        Returns:
            A successful report whose ``energy`` is present.

        Raises:
            UnknownPlugError: If ``name`` is not configured.
            PlugDeviceError: If the device could not be read.
            EnergyUnsupportedError: If the plug has no energy meter.
        """
        report = await self.get_state(name)
        if report.energy is None:
            raise EnergyUnsupportedError(name)
        return report

    async def set_state(self, name: str, *, turn_on: bool) -> SwitchResult:
        """Switch one plug to an absolute on/off state.

        Args:
            name: The device-list name of the plug.
            turn_on: ``True`` to turn the plug on, ``False`` to turn it off.

        Returns:
            The successful switch result.

        Raises:
            UnknownPlugError: If ``name`` is not configured.
            PlugDeviceError: If the switch failed.
        """
        entry = self._entry(name)
        async with self._locks[name]:
            result = await self._switch(entry, turn_on=turn_on)
        if not result.ok:
            raise PlugDeviceError(result.error or "unknown error")
        return result

    async def toggle(self, name: str) -> SwitchResult:
        """Flip one plug to the opposite of its current state.

        The read and the switch are held under the same lock so the toggle is
        atomic with respect to other requests for the same plug.

        Args:
            name: The device-list name of the plug.

        Returns:
            The successful switch result.

        Raises:
            UnknownPlugError: If ``name`` is not configured.
            PlugDeviceError: If the read or the switch failed.
        """
        entry = self._entry(name)
        async with self._locks[name]:
            report = await self._read(entry)
            if not report.ok:
                raise PlugDeviceError(report.error or "unknown error")
            result = await self._switch(entry, turn_on=not report.is_on)
        if not result.ok:
            raise PlugDeviceError(result.error or "unknown error")
        return result

    async def _read(self, entry: DeviceEntry) -> DeviceReport:
        """Read one device, retrying once on failure (fresh reconnect)."""
        report = await self._controller.read(entry)
        if report.ok:
            return report
        return await self._controller.read(entry)

    async def _switch(self, entry: DeviceEntry, *, turn_on: bool) -> SwitchResult:
        """Switch one device to an absolute state, retrying once on failure.

        Retrying is safe because the target state is absolute, not relative, so
        re-sending the same command is idempotent.
        """
        result = await self._controller.switch(entry, turn_on=turn_on)
        if result.ok:
            return result
        return await self._controller.switch(entry, turn_on=turn_on)
