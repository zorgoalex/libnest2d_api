from __future__ import annotations

import asyncio
from typing import Any

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.exceptions import RequestValidationError
from anyio import to_thread

from .config import DEFAULT_SETTINGS
from .errors import ServiceError
from .models import OptimizeRequest
from .optimizer import optimize
from .svg import render_svg
from .validation import validate_request
from .version import CORE_VERSION, SERVICE_VERSION, WRAPPER_VERSION

app = FastAPI(title="libnest2d-service", version=SERVICE_VERSION)
settings = DEFAULT_SETTINGS
semaphore = asyncio.Semaphore(settings.max_concurrent_jobs)


@app.middleware("http")
async def limit_body_size(request: Request, call_next):
    body = await request.body()
    if len(body) > settings.max_body_bytes:
        return JSONResponse(
            status_code=413,
            content={
                "status": "error",
                "error_code": "CONSTRAINT_ERROR",
                "message": "request body too large",
                "details": {"max_body_bytes": settings.max_body_bytes},
            },
        )
    request._body = body
    return await call_next(request)


@app.exception_handler(ServiceError)
async def handle_service_error(_request: Request, exc: ServiceError):
    status_map = {
        "VALIDATION_ERROR": 422,
        "CONSTRAINT_ERROR": 400,
        "TIMEOUT": 408,
        "INTERNAL": 500,
    }
    return JSONResponse(
        status_code=status_map.get(exc.code, 500),
        content=exc.to_dict(),
    )


@app.exception_handler(RequestValidationError)
async def handle_validation_error(_request: Request, exc: RequestValidationError):
    return JSONResponse(
        status_code=422,
        content={
            "status": "error",
            "error_code": "VALIDATION_ERROR",
            "message": "request validation failed",
            "details": {"errors": exc.errors()},
        },
    )


@app.get("/health/live")
async def health_live() -> dict:
    return {"status": "ok"}


@app.get("/health/ready")
async def health_ready() -> dict:
    return {"status": "ok"}


@app.get("/version")
async def version() -> dict:
    return {
        "service": SERVICE_VERSION,
        "core": CORE_VERSION,
        "wrapper": WRAPPER_VERSION,
    }


@app.post("/v1/optimize")
async def optimize_endpoint(request: OptimizeRequest) -> dict[str, Any]:
    validate_request(request, settings)

    async with semaphore:
        result = await to_thread.run_sync(optimize, request, settings)

    svg = render_svg(result.solutions)

    return {
        "status": "ok",
        "summary": result.summary,
        "solutions": [_serialize_solution(s) for s in result.solutions],
        "artifacts": {"svg": svg},
    }


def _serialize_solution(solution) -> dict[str, Any]:
    return {
        "stock_id": solution.stock_id,
        "index": solution.index,
        "width_mm": solution.width_mm,
        "height_mm": solution.height_mm,
        "trim_mm": solution.trim_mm.model_dump(),
        "placements": [
            {
                "item_id": placement.item_id,
                "instance": placement.instance,
                "x_mm": placement.x_mm,
                "y_mm": placement.y_mm,
                "width_mm": placement.width_mm,
                "height_mm": placement.height_mm,
                "rotated": placement.rotated,
                "pattern_direction": placement.pattern_direction,
            }
            for placement in solution.placements
        ],
    }
