from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field, field_validator


class TrimMM(BaseModel):
    left: float = Field(ge=0)
    right: float = Field(ge=0)
    top: float = Field(ge=0)
    bottom: float = Field(ge=0)


class Params(BaseModel):
    spacing_mm: float = Field(ge=0)
    trim_mm: TrimMM
    time_limit_ms: int | None = Field(default=None, ge=100)
    restarts: int | None = Field(default=None, ge=1)
    objective: Literal["min_waste", "min_sheets"] = "min_waste"
    seed: int | None = None

    @field_validator("seed")
    @classmethod
    def validate_seed(cls, value: int | None) -> int | None:
        if value is None:
            return value
        min_i64 = -(2**63)
        max_i64 = 2**63 - 1
        if not (min_i64 <= value <= max_i64):
            raise ValueError("seed must fit in signed 64-bit range")
        return value


class Stock(BaseModel):
    id: str
    width_mm: float = Field(gt=0)
    height_mm: float = Field(gt=0)
    qty: int = Field(ge=1)


class Item(BaseModel):
    id: str
    width_mm: float = Field(gt=0)
    height_mm: float = Field(gt=0)
    qty: int = Field(ge=1)
    rotation: Literal["forbid", "allow_90"]
    pattern_direction: Literal["none", "along_width", "along_height"]


class OptimizeRequest(BaseModel):
    units: Literal["mm"]
    params: Params
    stock: list[Stock] = Field(min_length=1)
    items: list[Item] = Field(min_length=1)
