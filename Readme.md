# libnest2d-service

FastAPI service that exposes a simple 2D nesting/packing API. The service accepts sheet stock and rectangular items, runs an optimizer (native libnest2d when available, Python fallback otherwise), and returns placements plus an SVG preview.

## Features

- HTTP API for nesting/packing with validation and clear error codes.
- Optional native core (pybind11 + libnest2d) built in the Docker image.
- Deterministic runs via seed and multiple restarts.
- SVG artifact for visual inspection.

## Tech stack

- Python 3.11
- FastAPI + Uvicorn
- Optional native core via pybind11 + libnest2d (built in Docker)
- scikit-build-core (native wheel build)
- Docker (multi-stage build)

## Quick start (Docker)

Build and run:

```bash
docker build --no-cache -t libnest2d-service:dev .
docker run -d -p 8080:8080 --name libnest2d-dev libnest2d-service:dev
```

Example request using `curlimages/curl` (external request simulation):

```bash
docker run --rm --network host \
  -v "$(pwd)/assets:/data" \
  curlimages/curl:8.6.0 \
  -s -H "Content-Type: application/json" \
  -d @/data/example_request.json \
  http://127.0.0.1:8080/v1/optimize
```

Stop the container when done:

```bash
docker rm -f libnest2d-dev
```

## Health & docs

- `GET /health/live`
- `GET /health/ready`
- `GET /version`
- `GET /openapi.json`
- `GET /docs`

## Main endpoint

`POST /v1/optimize`

- Request/response are JSON.
- All dimensions are in millimeters (`mm`).
- Successful responses include `artifacts.svg`.
- Coordinate system: origin (0,0) is the top-left of the usable area (after `trim_mm`), X to the right, Y down.

### Request fields

Top-level fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `units` | string | yes | Measurement units; must be `"mm"`. |
| `params` | object | yes | Optimization parameters. |
| `stock` | array | yes | List of available sheet types. |
| `items` | array | yes | List of items to place. |

`params` fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `spacing_mm` | number | yes | Clearance between parts in mm. |
| `trim_mm` | object | yes | Unusable margins around the sheet in mm. |
| `time_limit_ms` | integer | no | Total time budget in milliseconds (minimum 100). |
| `restarts` | integer | no | Number of optimization restarts (minimum 1). |
| `objective` | string | no | `"min_waste"` or `"min_sheets"` (default: `"min_waste"`). |
| `seed` | integer | no | Deterministic seed (signed 64-bit range). |

`trim_mm` fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `left` | number | yes | Left margin in mm (>= 0). |
| `right` | number | yes | Right margin in mm (>= 0). |
| `top` | number | yes | Top margin in mm (>= 0). |
| `bottom` | number | yes | Bottom margin in mm (>= 0). |

`stock` item fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | string | yes | Stock identifier. |
| `width_mm` | number | yes | Sheet width in mm (> 0). |
| `height_mm` | number | yes | Sheet height in mm (> 0). |
| `qty` | integer | yes | Quantity of sheets (>= 1). |

`items` item fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | string | yes | Item identifier. |
| `width_mm` | number | yes | Item width in mm (> 0). |
| `height_mm` | number | yes | Item height in mm (> 0). |
| `qty` | integer | yes | Quantity of items (>= 1). |
| `rotation` | string | yes | `"forbid"` or `"allow_90"`. |
| `pattern_direction` | string | yes | `"none"`, `"along_width"`, `"along_height"`. |

### Response keys

Top-level fields (success):

| Field | Type | Description |
| --- | --- | --- |
| `status` | string | `"ok"` on success. |
| `summary` | object | Aggregated optimization metrics. |
| `solutions` | array | Per-sheet layouts with placements. |
| `artifacts` | object | Output artifacts. |

`summary` fields:

| Field | Type | Description |
| --- | --- | --- |
| `objective` | string | Optimization objective used. |
| `used_stock_count` | integer | Number of sheets used. |
| `total_waste_area_mm2` | number | Total waste area in mm². |
| `waste_percent` | number | Waste percentage of used stock. |
| `time_ms` | integer | Total runtime in milliseconds. |
| `restarts_used` | integer | Number of restarts actually used. |
| `seed` | integer | Seed value used. |

`solutions` item fields:

| Field | Type | Description |
| --- | --- | --- |
| `stock_id` | string | Stock ID from the request. |
| `index` | integer | Sheet index for that stock type. |
| `width_mm` | number | Sheet width in mm. |
| `height_mm` | number | Sheet height in mm. |
| `trim_mm` | object | Margins used for this sheet. |
| `placements` | array | Placed items. |

`placements` item fields:

| Field | Type | Description |
| --- | --- | --- |
| `item_id` | string | Item ID from the request. |
| `instance` | integer | Instance number for this item. |
| `x_mm` | number | X coordinate in mm. |
| `y_mm` | number | Y coordinate in mm. |
| `width_mm` | number | Placed width in mm. |
| `height_mm` | number | Placed height in mm. |
| `rotated` | boolean | Whether the item was rotated. |
| `pattern_direction` | string | Pattern direction from the request. |

`artifacts` fields:

| Field | Type | Description |
| --- | --- | --- |
| `svg` | string | Full SVG document of the layout. |

### Error response

| Field | Type | Description |
| --- | --- | --- |
| `status` | string | `"error"` on failure. |
| `error_code` | string | `VALIDATION_ERROR`, `CONSTRAINT_ERROR`, `TIMEOUT`, or `INTERNAL`. |
| `message` | string | Human-readable error message. |
| `details` | object | Optional error details. |

### Examples

- `assets/example_request.json`
- `assets/example_response.json`

## Configuration (env)

- `PORT` (default `8080`)
- `LOG_LEVEL` (default `info`)
- `MAX_BODY_BYTES` (default `5242880`)
- `MAX_INSTANCES` (default `5000`)
- `DEFAULT_TIME_LIMIT_MS` (default `300`)
- `DEFAULT_RESTARTS` (default `10`)
- `MAX_CONCURRENT_JOBS` (default `1`)

## Notes

- `pattern_direction` is validated and returned in placements, but does not currently affect optimization.

## Local development (optional)

Requires Python 3.11 and a C++ toolchain if you want the native core. If the native core is not available, the service falls back to a Python implementation.

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
pip install .
uvicorn libnest2d_service.app:app --host 0.0.0.0 --port 8080
```
