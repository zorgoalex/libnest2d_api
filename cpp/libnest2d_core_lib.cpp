#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <libnest2d/libnest2d.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {

constexpr int64_t kScale = 1000;

int64_t scale_value(double value) {
    return static_cast<int64_t>(std::llround(value * static_cast<double>(kScale)));
}

double unscale_value(int64_t value) {
    return static_cast<double>(value) / static_cast<double>(kScale);
}

struct Trim {
    int64_t left;
    int64_t right;
    int64_t top;
    int64_t bottom;
};

struct Params {
    int64_t spacing;
    int time_limit_ms;
    int restarts;
    std::string objective;
    bool has_seed;
    int64_t seed;
};

struct Stock {
    std::string id;
    int64_t width;
    int64_t height;
    int qty;
};

struct ItemInstance {
    std::string id;
    int64_t width;
    int64_t height;
    bool allow_rotation;
    std::string pattern_direction;
};

struct Placement {
    std::string item_id;
    int instance;
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
    bool rotated;
    std::string pattern_direction;
};

struct SheetSolution {
    std::string stock_id;
    int index;
    int64_t width;
    int64_t height;
    Trim trim;
    std::vector<Placement> placements;
};

struct TimeoutError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct PlacementError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct RunResult {
    std::vector<SheetSolution> solutions;
    int used_bins = 0;
};

int64_t splitmix64(int64_t value) {
    uint64_t z = static_cast<uint64_t>(value) + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<int64_t>(z ^ (z >> 31));
}

void ensure_time(const std::chrono::steady_clock::time_point &start, int time_limit_ms) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    );
    if (elapsed.count() > time_limit_ms) {
        throw TimeoutError("optimization exceeded time limit");
    }
}

Trim parse_trim(const py::dict &data) {
    return Trim{
        scale_value(data["left"].cast<double>()),
        scale_value(data["right"].cast<double>()),
        scale_value(data["top"].cast<double>()),
        scale_value(data["bottom"].cast<double>()),
    };
}

Params parse_params(const py::dict &data) {
    Params params;
    params.spacing = scale_value(data["spacing_mm"].cast<double>());
    params.time_limit_ms = data["time_limit_ms"].cast<int>();
    params.restarts = data["restarts"].cast<int>();
    params.objective = data["objective"].cast<std::string>();
    if (data.contains("seed") && !data["seed"].is_none()) {
        params.has_seed = true;
        params.seed = data["seed"].cast<int64_t>();
    } else {
        params.has_seed = false;
        params.seed = 0;
    }
    return params;
}

std::vector<Stock> parse_stocks(const py::list &data) {
    std::vector<Stock> stocks;
    stocks.reserve(data.size());
    for (const auto &entry : data) {
        py::dict item = py::cast<py::dict>(entry);
        stocks.push_back(Stock{
            item["id"].cast<std::string>(),
            scale_value(item["width_mm"].cast<double>()),
            scale_value(item["height_mm"].cast<double>()),
            item["qty"].cast<int>(),
        });
    }
    return stocks;
}

std::vector<ItemInstance> parse_items(const py::list &data) {
    std::vector<ItemInstance> items;
    for (const auto &entry : data) {
        py::dict item = py::cast<py::dict>(entry);
        const auto qty = item["qty"].cast<int>();
        for (int i = 0; i < qty; ++i) {
            items.push_back(ItemInstance{
                item["id"].cast<std::string>(),
                scale_value(item["width_mm"].cast<double>()),
                scale_value(item["height_mm"].cast<double>()),
                item["rotation"].cast<std::string>() == "allow_90",
                item["pattern_direction"].cast<std::string>(),
            });
        }
    }
    return items;
}

std::vector<std::pair<const Stock *, int>> expand_sheets(const std::vector<Stock> &stocks) {
    std::vector<std::pair<const Stock *, int>> sheets;
    for (const auto &stock : stocks) {
        for (int i = 0; i < stock.qty; ++i) {
            sheets.emplace_back(&stock, i);
        }
    }
    return sheets;
}

bool is_rotated(double radians) {
    const double half_pi = M_PI / 2.0;
    const double two_pi = M_PI * 2.0;
    double norm = std::fmod(radians, two_pi);
    if (norm < 0) {
        norm += two_pi;
    }
    return std::fabs(norm - half_pi) < 1e-3 || std::fabs(norm - 3.0 * half_pi) < 1e-3;
}

RunResult nest_run(
    const std::vector<ItemInstance> &instances,
    const std::vector<Stock> &stocks,
    const Trim &trim,
    int64_t spacing,
    const std::chrono::steady_clock::time_point &start,
    int time_limit_ms
) {
    const auto sheets = expand_sheets(stocks);
    if (sheets.empty()) {
        throw PlacementError("no stock available");
    }

    RunResult result;
    std::vector<libnest2d::Item> items;
    items.reserve(instances.size());

    for (const auto &inst : instances) {
        const int64_t width = inst.width + spacing;
        const int64_t height = inst.height + spacing;
        std::vector<libnest2d::Point> poly = {
            libnest2d::Point{0, 0},
            libnest2d::Point{width, 0},
            libnest2d::Point{width, height},
            libnest2d::Point{0, height},
        };
        items.emplace_back(poly);
    }

    const int64_t usable_w = sheets.front().first->width - trim.left - trim.right;
    const int64_t usable_h = sheets.front().first->height - trim.top - trim.bottom;
    libnest2d::Box bin(usable_w, usable_h);

    ensure_time(start, time_limit_ms);
    const auto bins_used = libnest2d::nest(items, bin);
    ensure_time(start, time_limit_ms);

    std::unordered_map<int, SheetSolution> solutions;

    for (size_t idx = 0; idx < items.size(); ++idx) {
        const auto &item = items[idx];
        const auto &meta = instances[idx];
        const auto polygon = item.transformedShape();
        if (polygon.empty()) {
            continue;
        }

        int64_t min_x = polygon.front().X;
        int64_t min_y = polygon.front().Y;
        int64_t max_x = polygon.front().X;
        int64_t max_y = polygon.front().Y;
        for (const auto &pt : polygon) {
            min_x = std::min(min_x, static_cast<int64_t>(pt.X));
            min_y = std::min(min_y, static_cast<int64_t>(pt.Y));
            max_x = std::max(max_x, static_cast<int64_t>(pt.X));
            max_y = std::max(max_y, static_cast<int64_t>(pt.Y));
        }

        const int64_t shift_x = usable_w / 2;
        const int64_t shift_y = usable_h / 2;
        const int64_t x = min_x + shift_x;
        const int64_t y = (shift_y - max_y);
        const int64_t width = max_x - min_x - spacing;
        const int64_t height = max_y - min_y - spacing;

        int bin_id = item.binId();

        auto it = solutions.find(bin_id);
        if (it == solutions.end()) {
            if (bin_id < 0 || static_cast<size_t>(bin_id) >= sheets.size()) {
                throw PlacementError("invalid bin index from libnest2d");
            }
            const auto *stock = sheets[bin_id].first;
            SheetSolution sheet{
                stock->id,
                sheets[bin_id].second,
                stock->width,
                stock->height,
                trim,
                {},
            };
            it = solutions.emplace(bin_id, std::move(sheet)).first;
        }

        const bool rotated = is_rotated(item.rotation());
        it->second.placements.push_back(Placement{
            meta.id,
            static_cast<int>(idx + 1),
            x,
            y,
            width,
            height,
            rotated,
            meta.pattern_direction,
        });
    }

    for (auto &entry : solutions) {
        result.solutions.push_back(std::move(entry.second));
    }

    std::sort(result.solutions.begin(), result.solutions.end(), [](const SheetSolution &a, const SheetSolution &b) {
        if (a.stock_id != b.stock_id) {
            return a.stock_id < b.stock_id;
        }
        return a.index < b.index;
    });

    result.used_bins = bins_used;
    return result;
}

py::dict build_summary(
    const std::string &objective,
    const std::vector<SheetSolution> &solutions,
    int elapsed_ms,
    int restarts_used,
    int64_t used_seed
) {
    double total_sheet_area = 0.0;
    double total_item_area = 0.0;

    for (const auto &sheet : solutions) {
        total_sheet_area += unscale_value(sheet.width) * unscale_value(sheet.height);
        for (const auto &placement : sheet.placements) {
            total_item_area += unscale_value(placement.width) * unscale_value(placement.height);
        }
    }

    const double waste_area = std::max(0.0, total_sheet_area - total_item_area);
    const double waste_percent = total_sheet_area > 0.0 ? (waste_area / total_sheet_area * 100.0) : 0.0;

    py::dict summary;
    summary["objective"] = objective;
    summary["used_stock_count"] = static_cast<int>(solutions.size());
    summary["total_waste_area_mm2"] = waste_area;
    summary["waste_percent"] = waste_percent;
    summary["time_ms"] = elapsed_ms;
    summary["restarts_used"] = restarts_used;
    summary["seed"] = used_seed;
    return summary;
}

bool is_better(const std::string &objective, const py::dict &candidate, const py::dict &best) {
    const auto c_used = candidate["used_stock_count"].cast<int>();
    const auto b_used = best["used_stock_count"].cast<int>();
    const auto c_waste = candidate["total_waste_area_mm2"].cast<double>();
    const auto b_waste = best["total_waste_area_mm2"].cast<double>();

    if (objective == "min_sheets") {
        if (c_used != b_used) {
            return c_used < b_used;
        }
        return c_waste < b_waste;
    }

    if (c_waste != b_waste) {
        return c_waste < b_waste;
    }
    return c_used < b_used;
}

py::dict solutions_to_python(const std::vector<SheetSolution> &solutions) {
    py::list list;
    for (const auto &sheet : solutions) {
        py::dict trim;
        trim["left"] = unscale_value(sheet.trim.left);
        trim["right"] = unscale_value(sheet.trim.right);
        trim["top"] = unscale_value(sheet.trim.top);
        trim["bottom"] = unscale_value(sheet.trim.bottom);

        py::list placements;
        for (const auto &placement : sheet.placements) {
            py::dict item;
            item["item_id"] = placement.item_id;
            item["instance"] = placement.instance;
            item["x_mm"] = unscale_value(placement.x);
            item["y_mm"] = unscale_value(placement.y);
            item["width_mm"] = unscale_value(placement.width);
            item["height_mm"] = unscale_value(placement.height);
            item["rotated"] = placement.rotated;
            item["pattern_direction"] = placement.pattern_direction;
            placements.append(item);
        }

        py::dict sheet_dict;
        sheet_dict["stock_id"] = sheet.stock_id;
        sheet_dict["index"] = sheet.index;
        sheet_dict["width_mm"] = unscale_value(sheet.width);
        sheet_dict["height_mm"] = unscale_value(sheet.height);
        sheet_dict["trim_mm"] = trim;
        sheet_dict["placements"] = placements;
        list.append(sheet_dict);
    }

    py::dict result;
    result["solutions"] = list;
    return result;
}

} // namespace

py::dict optimize(const py::dict &request) {
    const auto params = parse_params(request["params"].cast<py::dict>());
    const auto trim = parse_trim(request["params"].cast<py::dict>()["trim_mm"].cast<py::dict>());
    const auto stocks = parse_stocks(request["stock"].cast<py::list>());
    auto items = parse_items(request["items"].cast<py::list>());

    const int64_t used_seed = params.has_seed
        ? params.seed
        : static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()
          ).count());

    int restarts = params.restarts;
    const int max_restarts = std::max(1, params.time_limit_ms / 80);
    if (restarts > max_restarts) {
        restarts = max_restarts;
    }

    const auto start = std::chrono::steady_clock::now();

    py::dict best_summary;
    py::dict best_result;
    bool has_best = false;

    for (int i = 0; i < restarts; ++i) {
        ensure_time(start, params.time_limit_ms);

        const int64_t run_seed = splitmix64(used_seed + i);
        auto shuffled = items;
        std::mt19937_64 rng(static_cast<uint64_t>(run_seed));
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        const auto run_result = nest_run(shuffled, stocks, trim, params.spacing, start, params.time_limit_ms);
        const auto elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
        );

        py::dict summary = build_summary(params.objective, run_result.solutions, elapsed_ms, i + 1, used_seed);
        py::dict output = solutions_to_python(run_result.solutions);
        output["summary"] = summary;

        if (!has_best || is_better(params.objective, summary, best_summary)) {
            best_summary = summary;
            best_result = output;
            has_best = true;
        }
    }

    if (!has_best) {
        throw PlacementError("unable to place all items with available stock");
    }

    return best_result;
}

PYBIND11_MODULE(libnest2d_core, m) {
    py::register_exception<TimeoutError>(m, "TimeoutError");
    py::register_exception<PlacementError>(m, "PlacementError");
    m.def("optimize", &optimize, "Optimize nesting with libnest2d core");
}
