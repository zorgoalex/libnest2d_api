from __future__ import annotations

from typing import Iterable

from .config import Settings
from .errors import ServiceError
from .models import Item, OptimizeRequest, Stock

MAX_STOCK_TYPES = 50


def validate_request(req: OptimizeRequest, settings: Settings) -> None:
    total_instances = sum(item.qty for item in req.items)
    if total_instances > settings.max_instances:
        raise ServiceError(
            code="CONSTRAINT_ERROR",
            message="sum(items.qty) exceeds MAX_INSTANCES",
            details={"max_instances": settings.max_instances, "actual": total_instances},
        )

    if len(req.stock) > MAX_STOCK_TYPES:
        raise ServiceError(
            code="CONSTRAINT_ERROR",
            message="stock length exceeds limit",
            details={"max_stock": MAX_STOCK_TYPES, "actual": len(req.stock)},
        )

    for stock in req.stock:
        _validate_trim(stock, req.params.trim_mm)

    _validate_items_fit(req.items, req.stock, req.params.trim_mm, req.params.spacing_mm)


def _validate_trim(stock: Stock, trim_mm) -> None:
    usable_w = stock.width_mm - trim_mm.left - trim_mm.right
    usable_h = stock.height_mm - trim_mm.top - trim_mm.bottom
    if usable_w <= 0 or usable_h <= 0:
        raise ServiceError(
            code="VALIDATION_ERROR",
            message="trim consumes the entire stock area",
            details={
                "stock_id": stock.id,
                "width_mm": stock.width_mm,
                "height_mm": stock.height_mm,
                "trim_mm": trim_mm.model_dump(),
            },
        )


def _validate_items_fit(
    items: Iterable[Item],
    stock_list: Iterable[Stock],
    trim_mm,
    spacing_mm: float,
) -> None:
    stocks = list(stock_list)
    for item in items:
        if not _item_fits_any_stock(item, stocks, trim_mm, spacing_mm):
            raise ServiceError(
                code="VALIDATION_ERROR",
                message="item does not fit any stock",
                details={
                    "item_id": item.id,
                    "width_mm": item.width_mm,
                    "height_mm": item.height_mm,
                    "rotation": item.rotation,
                },
            )


def _item_fits_any_stock(item: Item, stocks: list[Stock], trim_mm, spacing_mm: float) -> bool:
    del spacing_mm
    req_w = item.width_mm
    req_h = item.height_mm

    for stock in stocks:
        usable_w = stock.width_mm - trim_mm.left - trim_mm.right
        usable_h = stock.height_mm - trim_mm.top - trim_mm.bottom
        if _fits(req_w, req_h, usable_w, usable_h):
            return True
        if item.rotation == "allow_90" and _fits(req_h, req_w, usable_w, usable_h):
            return True
    return False


def _fits(w: float, h: float, max_w: float, max_h: float) -> bool:
    return w <= max_w and h <= max_h
