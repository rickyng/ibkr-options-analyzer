# Stage 1: Build C++ binary
FROM gcc:13-bookworm AS builder

# Install CMake 3.26+ (Debian Bookworm only has 3.25)
RUN apt-get update && apt-get install -y --no-install-recommends \
    wget \
    && wget -qO /tmp/cmake.sh https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-$(uname -m).sh \
    && sh /tmp/cmake.sh --prefix=/usr/local --skip-license \
    && rm /tmp/cmake.sh \
    && apt-get purge -y wget \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy C++ source and CMake config
COPY CMakeLists.txt CMakePresets.json ./
COPY src/ ./src/

# Build in Release mode (FetchContent downloads + compile)
RUN cmake --preset release && cmake --build build/release

# Stage 2: Python runtime
FROM python:3.12-slim AS runtime

WORKDIR /app

# Install runtime dependencies for C++ binary (brotli for cpp-httplib)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libbrotli1 \
    && rm -rf /var/lib/apt/lists/*

# Copy dashboard and install dependencies (hatchling needs app/ at install time)
COPY dashboard/ ./dashboard/
RUN pip install --no-cache-dir ./dashboard

# Copy compiled C++ binary from builder stage
COPY --from=builder /build/build/release/ibkr-options-analyzer ./ibkr-options-analyzer

# Run as non-root user
RUN useradd --create-home appuser && mkdir -p /data && chown appuser:appuser /data
USER appuser

# Environment variables for config
ENV IBKR_DB_PATH=/data/data.db
ENV IBKR_CLI_PATH=/app/ibkr-options-analyzer

# Expose port
EXPOSE 8001

# Run the API server
CMD ["uvicorn", "dashboard.app.main:app", "--host", "0.0.0.0", "--port", "8001"]
