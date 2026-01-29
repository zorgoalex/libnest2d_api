from __future__ import annotations

import random
import time
from dataclasses import dataclass

from .config import Settings
from .errors import ServiceError
from .models import OptimizeRequest
from .packing import SheetSolution, pack_shelves


@dataclass
class OptimizationResult:
    summary: dict
    solutions: list[SheetSolution]


def optimize(req: OptimizeRequest, settings: Settings) -> OptimizationResult:
    time_limit_ms = req.params.time_limit_ms or settings.default_time_limit_ms
    restarts = req.params.restarts or settings.default_restarts

    used_seed = req.params.seed if req.params.seed is not None else _generate_seed()
    restarts = _adjust_restarts(time_limit_ms, restarts)

    start_ts = time.monotonic()
    best_result: OptimizationResult | None = None

    for i in range(restarts):
        _ensure_time(start_ts, time_limit_ms)
        run_seed = splitmix64(used_seed + i)
        ordered_items = _shuffle_items(req, run_seed)
        try:
            solutions = pack_shelves(
                ordered_items,
                list(req.stock),
                req.params.trim_mm,
                req.params.spacing_mm,
            )
        except ValueError as exc:
            continue

        elapsed_ms = int((time.monotonic() - start_ts) * 1000)
        summary = _build_summary(
            req=req,
            solutions=solutions,
            elapsed_ms=elapsed_ms,
            restarts_used=i + 1,
            used_seed=used_seed,
        )
        candidate = OptimizationResult(summary=summary, solutions=solutions)
        if best_result is None:
            best_result = candidate
        else:
            if _is_better(req.params.objective, candidate, best_result):
                best_result = candidate

    if best_result is None:
        raise ServiceError(
            code="CONSTRAINT_ERROR",
            message="unable to place all items with available stock",
        )

    _ensure_time(start_ts, time_limit_ms)
    return best_result


def _shuffle_items(req: OptimizeRequest, seed: int) -> list:
    instances = []
    for item in req.items:
        for _ in range(item.qty):
            instances.append(
                item.model_copy(update={\"qty\": 1})
            )
    rng = random.Random(seed)
    rng.shuffle(instances)
    return instances


def _adjust_restarts(time_limit_ms: int, requested: int) -> int:
    max_restarts = max(1, time_limit_ms // 80)
    return min(requested, max_restarts)


def _generate_seed() -> int:
    return int(time.time_ns() // 1_000_000)


def splitmix64(value: int) -> int:
    z = (value + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9 & 0xFFFFFFFFFFFFFFFF
    z = (z ^ (z >> 27)) * 0x94D049BB133111EB & 0xFFFFFFFFFFFFFFFF
    return z ^ (z >> 31)


def _ensure_time(start_ts: float, time_limit_ms: int) -> None:
    elapsed_ms = (time.monotonic() - start_ts) * 1000
    if elapsed_ms > time_limit_ms:
        raise ServiceError(
            code="TIMEOUT",
            message="optimization exceeded time limit",
        )


def _build_summary(
    *,
    req: OptimizeRequest,
    solutions: list[SheetSolution],
    elapsed_ms: int,
    restarts_used: int,
    used_seed: int,
) -> dict:
    total_sheet_area = 0.0
    total_item_area = 0.0

    for sheet in solutions:
        total_sheet_area += sheet.width_mm * sheet.height_mm
        for placement in sheet.placements:
            total_item_area += placement.width_mm * placement.height_mm

    waste_area = max(0.0, total_sheet_area - total_item_area)
    waste_percent = (waste_area / total_sheet_area * 100.0) if total_sheet_area > 0 else 0.0

    return {
        "objective": req.params.objective,
        "used_stock_count": len(solutions),
        "total_waste_area_mm2": waste_area,
        "waste_percent": waste_percent,
        "time_ms": elapsed_ms,
        "restarts_used": restarts_used,
        "seed": used_seed,
    }


def _is_better(objective: str, candidate: OptimizationResult, best: OptimizationResult) -> bool:
    c_summary = candidate.summary
    b_summary = best.summary

    if objective == "min_sheets":
        if c_summary["used_stock_count"] != b_summary["used_stock_count"]:
            return c_summary["used_stock_count"] < b_summary["used_stock_count"]
        return c_summary["total_waste_area_mm2"] < b_summary["total_waste_area_mm2"]

    if c_summary["total_waste_area_mm2"] != b_summary["total_waste_area_mm2"]:
        return c_summary["total_waste_area_mm2"] < b_summary["total_waste_area_mm2"]
    return c_summary["used_stock_count"] < b_summary["used_stock_count"]


def _from_core(result: dict) -> OptimizationResult:
    summary = result.get("summary")
    solutions_data = result.get("solutions", [])
    if summary is None:
        raise ServiceError(code="INTERNAL", message="core result missing summary")

    solutions: list[SheetSolution] = []
    for solution in solutions_data:
        trim_raw = solution.get("trim_mm", {})
        trim = TrimMM(**trim_raw)
        placements = [
            Placement(
                item_id=placement["item_id"],
                instance=placement["instance"],
                x_mm=placement["x_mm"],
                y_mm=placement["y_mm"],
                width_mm=placement["width_mm"],
                height_mm=placement["height_mm"],
                rotated=placement["rotated"],
                pattern_direction=placement["pattern_direction"],
            )
            for placement in solution.get("placements", [])
        ]
        solutions.append(
            SheetSolution(
                stock_id=solution["stock_id"],
                index=solution["index"],
                width_mm=solution["width_mm"],
                height_mm=solution["height_mm"],
                trim_mm=trim,
                placements=placements,
            )
        )

    return OptimizationResult(summary=summary, solutions=solutions)
