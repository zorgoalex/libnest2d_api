from __future__ import annotations

from dataclasses import dataclass

from .models import Item, Stock, TrimMM


@dataclass
class Placement:
    item_id: str
    instance: int
    x_mm: float
    y_mm: float
    width_mm: float
    height_mm: float
    rotated: bool
    pattern_direction: str


@dataclass
class SheetSolution:
    stock_id: str
    index: int
    width_mm: float
    height_mm: float
    trim_mm: TrimMM
    placements: list[Placement]


@dataclass
class _SheetCursor:
    stock: Stock
    index: int
    x: float
    y: float
    row_h: float
    placements: list[Placement]


def pack_shelves(
    items: list[Item],
    stock_list: list[Stock],
    trim_mm: TrimMM,
    spacing_mm: float,
) -> list[SheetSolution]:
    sheets = _expand_sheets(stock_list)
    if not sheets:
        return []

    instance_map: dict[str, int] = {}
    solutions: list[SheetSolution] = []
    sheet_idx = 0
    cursor = _new_cursor(sheets[sheet_idx])

    for item in items:
        placed = _place_instance(cursor, item, instance_map, trim_mm, spacing_mm)
        if not placed:
            if cursor.placements:
                solutions.append(_finalize_sheet(cursor, trim_mm))
            sheet_idx += 1
            if sheet_idx >= len(sheets):
                raise ValueError("insufficient stock to place all items")
            cursor = _new_cursor(sheets[sheet_idx])
            if not _place_instance(cursor, item, instance_map, trim_mm, spacing_mm):
                raise ValueError("item does not fit on empty sheet")

    if cursor.placements:
        solutions.append(_finalize_sheet(cursor, trim_mm))

    return solutions


def _expand_sheets(stock_list: list[Stock]) -> list[tuple[Stock, int]]:
    sheets: list[tuple[Stock, int]] = []
    for stock in stock_list:
        for idx in range(stock.qty):
            sheets.append((stock, idx))
    return sheets


def _new_cursor(sheet: tuple[Stock, int]) -> _SheetCursor:
    stock, index = sheet
    return _SheetCursor(stock=stock, index=index, x=0.0, y=0.0, row_h=0.0, placements=[])


def _place_instance(
    cursor: _SheetCursor,
    item: Item,
    instance_map: dict[str, int],
    trim_mm: TrimMM,
    spacing_mm: float,
) -> bool:
    usable_w = cursor.stock.width_mm - trim_mm.left - trim_mm.right
    usable_h = cursor.stock.height_mm - trim_mm.top - trim_mm.bottom

    options = [(item.width_mm, item.height_mm, False)]
    if item.rotation == "allow_90" and item.width_mm != item.height_mm:
        options.append((item.height_mm, item.width_mm, True))

    for width, height, rotated in options:
        if _fits_here(cursor, width, height, usable_w, usable_h):
            _commit_placement(
                cursor,
                item,
                instance_map,
                width,
                height,
                rotated,
                spacing_mm,
            )
            return True

    if cursor.row_h > 0:
        cursor.x = 0.0
        cursor.y += cursor.row_h + spacing_mm
        cursor.row_h = 0.0

        for width, height, rotated in options:
            if _fits_here(cursor, width, height, usable_w, usable_h):
                _commit_placement(
                    cursor,
                    item,
                    instance_map,
                    width,
                    height,
                    rotated,
                    spacing_mm,
                )
                return True

    return False


def _fits_here(cursor: _SheetCursor, w: float, h: float, max_w: float, max_h: float) -> bool:
    return cursor.x + w <= max_w and cursor.y + h <= max_h


def _commit_placement(
    cursor: _SheetCursor,
    item: Item,
    instance_map: dict[str, int],
    width: float,
    height: float,
    rotated: bool,
    spacing_mm: float,
) -> None:
    instance_map[item.id] = instance_map.get(item.id, 0) + 1
    cursor.placements.append(
        Placement(
            item_id=item.id,
            instance=instance_map[item.id],
            x_mm=cursor.x,
            y_mm=cursor.y,
            width_mm=width,
            height_mm=height,
            rotated=rotated,
            pattern_direction=item.pattern_direction,
        )
    )
    cursor.x += width + spacing_mm
    if height > cursor.row_h:
        cursor.row_h = height


def _finalize_sheet(cursor: _SheetCursor, trim_mm: TrimMM) -> SheetSolution:
    return SheetSolution(
        stock_id=cursor.stock.id,
        index=cursor.index,
        width_mm=cursor.stock.width_mm,
        height_mm=cursor.stock.height_mm,
        trim_mm=trim_mm,
        placements=cursor.placements,
    )
