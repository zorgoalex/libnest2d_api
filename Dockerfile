FROM python:3.11-slim AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY requirements.txt /app/requirements.txt
RUN pip install --upgrade pip \
    && pip wheel --no-cache-dir -r /app/requirements.txt -w /wheels

COPY src /app/src

FROM python:3.11-slim AS runtime

ENV PORT=8080 \
    LOG_LEVEL=info \
    MAX_BODY_BYTES=5242880 \
    MAX_INSTANCES=5000 \
    DEFAULT_TIME_LIMIT_MS=300 \
    DEFAULT_RESTARTS=10 \
    MAX_CONCURRENT_JOBS=1

WORKDIR /app
COPY --from=builder /wheels /wheels
RUN pip install --no-cache-dir /wheels/* \
    && rm -rf /wheels

COPY src /app/src
ENV PYTHONPATH=/app/src

HEALTHCHECK --interval=10s --timeout=2s --retries=3 CMD-SHELL python -c "import os, urllib.request; urllib.request.urlopen(f'http://127.0.0.1:{os.getenv(\"PORT\", \"8080\")}/health/live')"

CMD ["sh", "-c", "uvicorn libnest2d_service.app:app --host 0.0.0.0 --port ${PORT}"]
