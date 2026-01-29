from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field, field_validator, model_validator


class TrimMM(BaseModel):
    left: float = Field(ge=0)
    right: float = Field(ge=0)
    top: float = Field(ge=0)
    bottom: float = Field(ge=0)


Alignment = Literal["center", "bottom_left", "bottom_right", "top_left", "top_right"]
StartPoint = Literal["bottom_left", "bottom_right", "top_left", "top_right"]


class BottomLeftParams(BaseModel):
    min_obj_distance_mm: float | None = Field(default=None, ge=0)
    epsilon_mm: float | None = Field(default=None, ge=0)


class NfpParams(BaseModel):
    rotations_deg: list[float] | None = None
    alignment: Alignment | None = None
    starting_point: StartPoint | None = None
    accuracy: float | None = Field(default=None, ge=0, le=1)
    explore_holes: bool | None = None
    parallel: bool | None = None

    @field_validator("rotations_deg")
    @classmethod
    def validate_rotations(cls, value: list[float] | None) -> list[float] | None:
        if value is None:
            return value
        if len(value) == 0:
            raise ValueError("rotations_deg must not be empty")
        for angle in value:
            if angle < 0 or angle >= 360:
                raise ValueError("rotations_deg values must be in [0, 360)")
        return value


class Params(BaseModel):
    spacing_mm: float = Field(ge=0)
    trim_mm: TrimMM
    time_limit_ms: int | None = Field(default=None, ge=100)
    restarts: int | None = Field(default=None, ge=1)
    objective: Literal["min_waste", "min_sheets"] = "min_waste"
    seed: int | None = None
    placer: Literal["bottom_left", "nfp"] = "bottom_left"
    selector: Literal["first_fit", "filler", "djd_heuristic"] = "first_fit"
    bottom_left: BottomLeftParams | None = None
    nfp: NfpParams | None = None

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

    @model_validator(mode="after")
    def validate_placer_params(self) -> "Params":
        if self.placer == "bottom_left" and self.nfp is not None:
            raise ValueError("nfp params provided but placer=bottom_left")
        if self.placer == "nfp" and self.bottom_left is not None:
            raise ValueError("bottom_left params provided but placer=nfp")
        return self


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
    rotation: Literal["forbid", "allow_90"] = "allow_90"
    pattern_direction: Literal["none", "along_width", "along_height"] = "none"


class OptimizeRequest(BaseModel):
    units: Literal["mm"]
    params: Params
    stock: list[Stock] = Field(min_length=1)
    items: list[Item] = Field(min_length=1)
