# Docker Containerization Design

**Date:** 2026-04-28
**Status:** Approved

## Goal

Containerize the IBKR Options Analyzer dashboard for deployment on a home server (x86_64), while learning Docker fundamentals: multi-stage builds, volume persistence, port mapping, and `.dockerignore`.

## Architecture

Single multi-stage Dockerfile + `docker-compose.yml` for convenience.

### Stage 1: C++ Builder

- Base image: `gcc:13-bookworm`
- Installs CMake 3.26+ and Ninja
- Copies `CMakeLists.txt` and `src/`
- Builds in Release mode using FetchContent for all C++ deps
- Output: `/build/ibkr-options-analyzer` binary

### Stage 2: Python Runtime

- Base image: `python:3.12-slim`
- Copies `dashboard/` and installs Python deps via `pip install .`
- Copies compiled binary from Stage 1 to `/app/ibkr-options-analyzer`
- Sets `IBKR_DB_PATH=/data/data.db` and `IBKR_CLI_PATH=/app/ibkr-options-analyzer`
- Exposes port 8000
- Entrypoint: `uvicorn app.main:app --host 0.0.0.0 --port 8000`

### docker-compose.yml

- Defines single `dashboard` service
- Volume mount: `~/.ibkr-options-analyzer:/data` (SQLite persistence)
- Port mapping: `8000:8000`
- Restart policy: `unless-stopped`

## Files to Create

| File | Purpose |
|------|---------|
| `Dockerfile` | Multi-stage build definition |
| `.dockerignore` | Exclude build/, .git/, docs/ from build context |
| `docker-compose.yml` | Convenient run/stop/restart with volume and port config |

## Runtime Usage

```bash
# Build
docker compose build

# Start
docker compose up -d

# Stop
docker compose down

# View logs
docker compose logs -f

# Rebuild after code changes
docker compose up -d --build
```

## Key Docker Concepts Covered

- **Multi-stage builds:** Build C++ in a heavy image, copy only the binary to a lean Python image
- **Volume mounts:** Persist SQLite database outside the container so data survives restarts
- **Port mapping:** Expose container port 8000 to host port 8000
- **.dockerignore:** Keep build context small by excluding irrelevant files
- **Restart policy:** Auto-restart on server reboot

## Out of Scope

- HTTPS/TLS (handle at reverse proxy level if needed later)
- Multi-architecture builds (x86_64 only for now)
- CI/CD pipeline for automated image builds
- Docker secrets management (IBKR tokens passed at runtime)
