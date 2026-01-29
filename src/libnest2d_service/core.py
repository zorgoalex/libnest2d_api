from __future__ import annotations

from typing import Any

from .config import Settings
from .errors import ServiceError
from .models import OptimizeRequest

try:
    import libnest2d_core
except Exception:  # pragma: no cover - optional native extension
    libnest2d_core = None


def try_optimize(req: OptimizeRequest, settings: Settings) -> dict[str, Any] | None:
    if libnest2d_core is None:
        return None

    payload = req.model_dump()
    payload["params"]["time_limit_ms"] = req.params.time_limit_ms or settings.default_time_limit_ms
    payload["params"]["restarts"] = req.params.restarts or settings.default_restarts

    try:
        return libnest2d_core.optimize(payload)
    except Exception as exc:
        if hasattr(libnest2d_core, "TimeoutError") and isinstance(exc, libnest2d_core.TimeoutError):
            raise ServiceError(code="TIMEOUT", message="optimization exceeded time limit") from exc
        if hasattr(libnest2d_core, "PlacementError") and isinstance(exc, libnest2d_core.PlacementError):
            raise ServiceError(code="CONSTRAINT_ERROR", message=str(exc)) from exc
        raise ServiceError(code="INTERNAL", message="core optimization failed", details={"error": str(exc)}) from exc
