from __future__ import annotations

from .packing import SheetSolution


def render_svg(solutions: list[SheetSolution]) -> str:
    if not solutions:
        return _empty_svg()

    sheet_gap = 20.0
    total_height = 0.0
    max_width = 0.0
    for sheet in solutions:
        max_width = max(max_width, sheet.width_mm)
        total_height += sheet.height_mm + sheet_gap
    total_height = max(total_height - sheet_gap, 0.0)

    parts = [
        f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{max_width}mm\" height=\"{total_height}mm\" viewBox=\"0 0 {max_width} {total_height}\">",
        "<rect x=\"0\" y=\"0\" width=\"100%\" height=\"100%\" fill=\"white\"/>",
    ]

    y_offset = 0.0
    for sheet in solutions:
        origin_x = sheet.trim_mm.left
        origin_y = sheet.trim_mm.top
        group_x = 0.0 + origin_x
        group_y = y_offset + origin_y

        parts.append(f"<g transform=\"translate({group_x},{group_y})\">")
        parts.append(
            f"<rect x=\"{-origin_x}\" y=\"{-origin_y}\" width=\"{sheet.width_mm}\" height=\"{sheet.height_mm}\" fill=\"none\" stroke=\"#000\" stroke-width=\"1\"/>")

        for placement in sheet.placements:
            parts.append(
                f"<rect x=\"{placement.x_mm}\" y=\"{placement.y_mm}\" width=\"{placement.width_mm}\" height=\"{placement.height_mm}\" fill=\"#d9e6f2\" stroke=\"#333\" stroke-width=\"0.5\"/>")
            label_x = placement.x_mm + 2
            label_y = placement.y_mm + 12
            parts.append(
                f"<text x=\"{label_x}\" y=\"{label_y}\" font-size=\"10\" fill=\"#111\">{placement.item_id}</text>")

        parts.append("</g>")
        y_offset += sheet.height_mm + sheet_gap

    parts.append("</svg>")
    return "".join(parts)


def _empty_svg() -> str:
    return (
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\" viewBox=\"0 0 10 10\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"white\"/>"
        "</svg>"
    )
