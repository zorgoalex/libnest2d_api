from __future__ import annotations

from dataclasses import dataclass


@dataclass
class ServiceError(Exception):
    code: str
    message: str
    details: dict | None = None

    def to_dict(self) -> dict:
        payload = {
            "status": "error",
            "error_code": self.code,
            "message": self.message,
        }
        if self.details is not None:
            payload["details"] = self.details
        return payload
