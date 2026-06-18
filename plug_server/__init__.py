"""FastAPI server that controls the Tapo plugs in ``device_list.md``.

The HTTP API is built on :class:`smartplugcontroller.SmartPlugController`,
which owns all device logic. This package only adapts that controller to HTTP:
``models`` defines the JSON contract, ``service`` adds name-based addressing and
per-plug locking, and ``app`` wires up the FastAPI routes.
"""
