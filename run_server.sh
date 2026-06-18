#!/bin/bash
# Launch the FastAPI plug-control server on the LAN (port 17046).
#
# Runs inside the conda env `smartplug` (which has kasa + fastapi + uvicorn),
# from the repository root so that `plug_server` and `smartplugcontroller`
# import correctly. Binds 0.0.0.0 so an ESP32 or other LAN client can connect.
# Any extra arguments are passed straight through to uvicorn (e.g. --reload).
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

# In a non-interactive shell `conda` is not a runnable command (it is a shell
# function from the profile, which `exec` cannot launch), so resolve the real
# conda executable to run the server inside the `smartplug` env.
conda_bin=""
if [[ -n "${CONDA_EXE:-}" && -x "${CONDA_EXE}" ]]; then
    conda_bin="${CONDA_EXE}"
else
    for candidate in \
        /opt/conda/bin/conda \
        "${HOME}/miniconda3/bin/conda" \
        "${HOME}/anaconda3/bin/conda"; do
        if [[ -x "${candidate}" ]]; then
            conda_bin="${candidate}"
            break
        fi
    done
fi

if [[ -z "${conda_bin}" ]]; then
    echo "run_server.sh: could not find a conda executable" >&2
    exit 1
fi

exec "${conda_bin}" run --no-capture-output -n smartplug \
    uvicorn plug_server.app:app --host 0.0.0.0 --port 17046 "$@"
