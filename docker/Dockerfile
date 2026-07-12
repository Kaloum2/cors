# syntax=docker/dockerfile:1
# cors-engine image — sidecar :7998, monitor :7999, agent :8002 (INT-03)

FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ca-certificates gfortran git libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN if [ ! -f lib/lapack/CMakeLists.txt ]; then \
      git clone --depth 1 https://github.com/Reference-LAPACK/lapack.git lib/lapack; \
    fi

RUN mkdir -p build && cd build \
    && cmake .. \
    && cmake --build . -j"$(nproc)" --target cors-engine cors-mengine

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 libsqlite3-0 libgfortran5 libquadmath0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/cors-engine /usr/local/bin/cors-engine
COPY --from=build /src/build/cors-mengine /usr/local/bin/cors-mengine
COPY scripts/ /app/scripts/
COPY conf/ /app/conf/
COPY docker/entrypoint.sh /app/docker/entrypoint.sh

WORKDIR /app
RUN chmod +x /app/docker/entrypoint.sh /app/scripts/build.sh 2>/dev/null || true \
    && chmod +x /app/scripts/cli-sidecar.py

EXPOSE 7998 7999 8002

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
  CMD python3 -c "import socket;s=socket.create_connection(('127.0.0.1',7998),3);s.sendall(b'PING\n');d=s.recv(64);s.close();exit(0 if b'PONG' in d else 1)"

ENTRYPOINT ["/app/docker/entrypoint.sh"]
