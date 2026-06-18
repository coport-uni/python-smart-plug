"""FastAPI application exposing the smart plugs over HTTP.

Run it on the LAN so other devices -- for example an ESP32 -- can switch the
plugs and read their state and power. State-changing actions use POST; reads use
GET, so a stray GET can never toggle a plug. There is no authentication: deploy
it only on a trusted local network.

Run inside the project's conda environment::

    conda run -n smartplug \\
        uvicorn plug_server.app:app --host 0.0.0.0 --port 17046

or simply ``python -m plug_server.app`` (also binds 0.0.0.0:17046).
"""

from __future__ import annotations

from contextlib import asynccontextmanager
from typing import TYPE_CHECKING

from fastapi import Depends, FastAPI, Request
from fastapi.responses import JSONResponse

from smartplugcontroller import SmartPlugController

from .models import (
    ActionResult,
    EnergyResponse,
    HealthResponse,
    PlugInfo,
    PlugState,
)
from .service import (
    EnergyUnsupportedError,
    PlugDeviceError,
    PlugService,
    UnknownPlugError,
)

if TYPE_CHECKING:
    from collections.abc import AsyncIterator

# The port the ESP32 and other LAN clients connect to.
default_port = 17046


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncIterator[None]:
    """Build the plug service once at startup from the on-disk config.

    A configuration problem (missing credentials or device list) raises here
    and prevents the server from starting, which is the desired fail-fast
    behavior.
    """
    controller = SmartPlugController.from_files()
    app.state.service = PlugService(controller)
    yield


app = FastAPI(
    title="Smart Plug Control",
    description="Control the Tapo plugs in device_list.md over HTTP.",
    lifespan=lifespan,
)


def get_service(request: Request) -> PlugService:
    """Return the shared :class:`PlugService` built at startup."""
    return request.app.state.service


@app.exception_handler(UnknownPlugError)
async def handle_unknown_plug(request: Request, exc: UnknownPlugError) -> JSONResponse:
    """Map an unknown plug name to ``404 Not Found``."""
    return JSONResponse(status_code=404, content={"detail": f"Unknown plug: {exc}"})


@app.exception_handler(EnergyUnsupportedError)
async def handle_energy_unsupported(
    request: Request, exc: EnergyUnsupportedError
) -> JSONResponse:
    """Map a meterless plug to ``422 Unprocessable Entity``."""
    return JSONResponse(
        status_code=422,
        content={"detail": f"Plug has no energy meter: {exc}"},
    )


@app.exception_handler(PlugDeviceError)
async def handle_device_error(request: Request, exc: PlugDeviceError) -> JSONResponse:
    """Map a failed device operation to ``502 Bad Gateway``."""
    return JSONResponse(status_code=502, content={"detail": str(exc)})


@app.get("/health")
async def health(service: PlugService = Depends(get_service)) -> HealthResponse:
    """Report liveness and how many plugs are configured."""
    return HealthResponse(status="ok", device_count=service.device_count)


@app.get("/plugs")
async def list_plugs(service: PlugService = Depends(get_service)) -> list[PlugInfo]:
    """List the configured plugs without contacting the devices."""
    return [PlugInfo.from_entry(entry) for entry in service.list_entries()]


@app.get("/plugs/{name}")
async def get_plug(name: str, service: PlugService = Depends(get_service)) -> PlugState:
    """Return the live on/off state of one plug."""
    report = await service.get_state(name)
    return PlugState.from_report(report)


@app.post("/plugs/{name}/on")
async def turn_on(
    name: str, service: PlugService = Depends(get_service)
) -> ActionResult:
    """Turn one plug on."""
    result = await service.set_state(name, turn_on=True)
    return ActionResult.from_switch(result, requested="on")


@app.post("/plugs/{name}/off")
async def turn_off(
    name: str, service: PlugService = Depends(get_service)
) -> ActionResult:
    """Turn one plug off."""
    result = await service.set_state(name, turn_on=False)
    return ActionResult.from_switch(result, requested="off")


@app.post("/plugs/{name}/toggle")
async def toggle(
    name: str, service: PlugService = Depends(get_service)
) -> ActionResult:
    """Flip one plug to the opposite of its current state."""
    result = await service.toggle(name)
    return ActionResult.from_switch(result, requested="toggle")


@app.get("/plugs/{name}/energy")
async def get_energy(
    name: str, service: PlugService = Depends(get_service)
) -> EnergyResponse:
    """Return one plug's power and energy reading."""
    report = await service.get_energy(name)
    return EnergyResponse.from_report(report)


def main() -> None:
    """Run the API with uvicorn on all interfaces, port 17046."""
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=default_port)


if __name__ == "__main__":
    main()
