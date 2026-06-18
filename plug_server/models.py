"""Pydantic response models for the plug-control API.

These models define the JSON the server returns, kept explicit and stable so a
simple client (such as an ESP32) can parse the responses without surprises.
Each model maps from the controller's plain data objects via a ``from_*``
constructor, which keeps the route handlers thin.
"""

from __future__ import annotations

from pydantic import BaseModel

from smartplugcontroller import DeviceEntry, DeviceReport, SwitchResult


class HealthResponse(BaseModel):
    """Liveness payload for the ``/health`` endpoint.

    Attributes:
        status: Always ``"ok"`` while the server is serving.
        device_count: The number of plugs configured from ``device_list.md``.
    """

    status: str
    device_count: int


class PlugInfo(BaseModel):
    """Static identity of a configured plug, taken from ``device_list.md``.

    Attributes:
        name: The human-friendly label used to address the plug in the API.
        device_type: The device family label, e.g. ``"tapo p110m"``.
        ip: The IPv4 address used to reach the plug.
        mac: The MAC address as written in the device list.
    """

    name: str
    device_type: str
    ip: str
    mac: str

    @classmethod
    def from_entry(cls, entry: DeviceEntry) -> PlugInfo:
        """Build a :class:`PlugInfo` from a device-list entry."""
        return cls(
            name=entry.name,
            device_type=entry.device_type,
            ip=entry.ip,
            mac=entry.mac,
        )


class PlugState(BaseModel):
    """Live on/off state and identity of a single plug.

    Attributes:
        name: The plug's device-list name.
        is_on: ``True`` if the plug is on, ``False`` if off.
        model: The reported model name.
        serial: The device unique id (``device_id``).
        firmware: The running firmware version.
        mac: The MAC address reported by the device (colon form).
    """

    name: str
    is_on: bool | None
    model: str | None
    serial: str | None
    firmware: str | None
    mac: str | None

    @classmethod
    def from_report(cls, report: DeviceReport) -> PlugState:
        """Build a :class:`PlugState` from a successful device report."""
        return cls(
            name=report.entry.name,
            is_on=report.is_on,
            model=report.model,
            serial=report.serial,
            firmware=report.firmware,
            mac=report.mac,
        )


class EnergyResponse(BaseModel):
    """Power and energy snapshot for a single plug.

    A value is ``null`` when the device does not report that measurement (for
    example, the P110M does not expose a lifetime total).

    Attributes:
        name: The plug's device-list name.
        power_w: Instantaneous power draw, in watts.
        today_kwh: Energy used so far today, in kilowatt-hours.
        month_kwh: Energy used so far this month, in kilowatt-hours.
        voltage_v: Line voltage, in volts.
        current_a: Line current, in amperes.
    """

    name: str
    power_w: float | None
    today_kwh: float | None
    month_kwh: float | None
    voltage_v: float | None
    current_a: float | None

    @classmethod
    def from_report(cls, report: DeviceReport) -> EnergyResponse:
        """Build an :class:`EnergyResponse` from a report that has energy data.

        Missing energy collapses to all-``None`` values; callers that need a
        meter should check ``report.energy`` before responding.
        """
        energy = report.energy
        return cls(
            name=report.entry.name,
            power_w=energy.power_w if energy else None,
            today_kwh=energy.today_kwh if energy else None,
            month_kwh=energy.month_kwh if energy else None,
            voltage_v=energy.voltage_v if energy else None,
            current_a=energy.current_a if energy else None,
        )


class ActionResult(BaseModel):
    """Outcome of an on/off/toggle action on a plug.

    Attributes:
        name: The plug's device-list name.
        requested: The action requested -- ``"on"``, ``"off"``, or ``"toggle"``.
        before: The on/off state observed before the action.
        is_on: The on/off state observed after the action.
    """

    name: str
    requested: str
    before: bool | None
    is_on: bool | None

    @classmethod
    def from_switch(cls, result: SwitchResult, requested: str) -> ActionResult:
        """Build an :class:`ActionResult` from a switch result."""
        return cls(
            name=result.entry.name,
            requested=requested,
            before=result.before,
            is_on=result.after,
        )
