from __future__ import annotations

import os
from dataclasses import dataclass


def _get_int(name: str, default: int) -> int:
    value = os.getenv(name)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError:
        return default


def _get_str(name: str, default: str) -> str:
    value = os.getenv(name)
    return value if value is not None else default


@dataclass(frozen=True)
class Settings:
    port: int
    log_level: str
    max_body_bytes: int
    max_instances: int
    default_time_limit_ms: int
    default_restarts: int
    max_concurrent_jobs: int


DEFAULT_SETTINGS = Settings(
    port=_get_int("PORT", 8080),
    log_level=_get_str("LOG_LEVEL", "info"),
    max_body_bytes=_get_int("MAX_BODY_BYTES", 5_242_880),
    max_instances=_get_int("MAX_INSTANCES", 5000),
    default_time_limit_ms=_get_int("DEFAULT_TIME_LIMIT_MS", 300),
    default_restarts=_get_int("DEFAULT_RESTARTS", 10),
    max_concurrent_jobs=_get_int("MAX_CONCURRENT_JOBS", 1),
)
