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
#include <type_traits>
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

struct BottomLeftConfigParams {
    bool has_min_obj_distance = false;
    int64_t min_obj_distance = 0;
    bool has_epsilon = false;
    int64_t epsilon = 0;
};

struct NfpConfigParams {
    bool has_rotations = false;
    std::vector<double> rotations_rad;
    bool has_alignment = false;
    std::string alignment;
    bool has_starting_point = false;
    std::string starting_point;
    bool has_accuracy = false;
    double accuracy = 0.0;
    bool has_explore_holes = false;
    bool explore_holes = false;
    bool has_parallel = false;
    bool parallel = false;
};

struct Params {
    int64_t spacing;
    int time_limit_ms;
    int restarts;
    std::string objective;
    bool has_seed;
    int64_t seed;
    std::string placer;
    std::string selector;
    BottomLeftConfigParams bottom_left;
    NfpConfigParams nfp;
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

enum class CoordMapping {
    ORIGIN_Y_UP,
    ORIGIN_Y_DOWN,
    CENTER_Y_UP,
    CENTER_Y_DOWN,
};

double deg_to_rad(double degrees);

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

double deg_to_rad(double degrees) {
    return degrees * M_PI / 180.0;
}

Trim parse_trim(const py::dict &data) {
    return Trim{
        scale_value(data["left"].cast<double>()),
        scale_value(data["right"].cast<double>()),
        scale_value(data["top"].cast<double>()),
        scale_value(data["bottom"].cast<double>()),
    };
}

BottomLeftConfigParams parse_bottom_left(const py::dict &data) {
    BottomLeftConfigParams params;
    if (data.contains("min_obj_distance_mm") && !data["min_obj_distance_mm"].is_none()) {
        params.has_min_obj_distance = true;
        params.min_obj_distance = scale_value(data["min_obj_distance_mm"].cast<double>());
    }
    if (data.contains("epsilon_mm") && !data["epsilon_mm"].is_none()) {
        params.has_epsilon = true;
        params.epsilon = scale_value(data["epsilon_mm"].cast<double>());
    }
    return params;
}

NfpConfigParams parse_nfp(const py::dict &data) {
    NfpConfigParams params;
    if (data.contains("rotations_deg") && !data["rotations_deg"].is_none()) {
        params.has_rotations = true;
        auto list = data["rotations_deg"].cast<std::vector<double>>();
        params.rotations_rad.reserve(list.size());
        for (double deg : list) {
            params.rotations_rad.push_back(deg_to_rad(deg));
        }
    }
    if (data.contains("alignment") && !data["alignment"].is_none()) {
        params.has_alignment = true;
        params.alignment = data["alignment"].cast<std::string>();
    }
    if (data.contains("starting_point") && !data["starting_point"].is_none()) {
        params.has_starting_point = true;
        params.starting_point = data["starting_point"].cast<std::string>();
    }
    if (data.contains("accuracy") && !data["accuracy"].is_none()) {
        params.has_accuracy = true;
        params.accuracy = data["accuracy"].cast<double>();
    }
    if (data.contains("explore_holes") && !data["explore_holes"].is_none()) {
        params.has_explore_holes = true;
        params.explore_holes = data["explore_holes"].cast<bool>();
    }
    if (data.contains("parallel") && !data["parallel"].is_none()) {
        params.has_parallel = true;
        params.parallel = data["parallel"].cast<bool>();
    }
    return params;
}

Params parse_params(const py::dict &data) {
    Params params;
    params.spacing = scale_value(data["spacing_mm"].cast<double>());
    params.time_limit_ms = data["time_limit_ms"].cast<int>();
    params.restarts = data["restarts"].cast<int>();
    params.objective = data["objective"].cast<std::string>();
    if (data.contains("placer") && !data["placer"].is_none()) {
        params.placer = data["placer"].cast<std::string>();
    } else {
        params.placer = "bottom_left";
    }
    if (data.contains("selector") && !data["selector"].is_none()) {
        params.selector = data["selector"].cast<std::string>();
    } else {
        params.selector = "first_fit";
    }
    if (data.contains("bottom_left") && !data["bottom_left"].is_none()) {
        params.bottom_left = parse_bottom_left(data["bottom_left"].cast<py::dict>());
    }
    if (data.contains("nfp") && !data["nfp"].is_none()) {
        params.nfp = parse_nfp(data["nfp"].cast<py::dict>());
    }
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

template <typename T, typename = void>
struct has_alignment_field : std::false_type {};

template <typename T>
struct has_alignment_field<T, std::void_t<decltype(std::declval<T>().alignment)>> : std::true_type {};

template <typename T, typename = void>
struct has_starting_point_field : std::false_type {};

template <typename T>
struct has_starting_point_field<T, std::void_t<decltype(std::declval<T>().starting_point)>> : std::true_type {};

template <typename T, typename = void>
struct has_accuracy_field : std::false_type {};

template <typename T>
struct has_accuracy_field<T, std::void_t<decltype(std::declval<T>().accuracy)>> : std::true_type {};

template <typename T, typename = void>
struct has_explore_holes_field : std::false_type {};

template <typename T>
struct has_explore_holes_field<T, std::void_t<decltype(std::declval<T>().explore_holes)>> : std::true_type {};

template <typename T, typename = void>
struct has_parallel_field : std::false_type {};

template <typename T>
struct has_parallel_field<T, std::void_t<decltype(std::declval<T>().parallel)>> : std::true_type {};

template <typename T, typename = void>
struct has_rotations_field : std::false_type {};

template <typename T>
struct has_rotations_field<T, std::void_t<decltype(std::declval<T>().rotations)>> : std::true_type {};

template <typename AlignmentType>
AlignmentType parse_alignment_value(const std::string &value) {
    if (value == "center") {
        return AlignmentType::CENTER;
    }
    if (value == "bottom_left") {
        return AlignmentType::BOTTOM_LEFT;
    }
    if (value == "bottom_right") {
        return AlignmentType::BOTTOM_RIGHT;
    }
    if (value == "top_left") {
        return AlignmentType::TOP_LEFT;
    }
    if (value == "top_right") {
        return AlignmentType::TOP_RIGHT;
    }
    throw std::invalid_argument("invalid alignment value");
}

template <typename StartType>
StartType parse_starting_point_value(const std::string &value) {
    if (value == "bottom_left") {
        return StartType::BOTTOM_LEFT;
    }
    if (value == "bottom_right") {
        return StartType::BOTTOM_RIGHT;
    }
    if (value == "top_left") {
        return StartType::TOP_LEFT;
    }
    if (value == "top_right") {
        return StartType::TOP_RIGHT;
    }
    throw std::invalid_argument("invalid starting_point value");
}

template <class Placer, class Selector>
RunResult nest_run_impl(
    const std::vector<ItemInstance> &instances,
    const std::vector<Stock> &stocks,
    const Trim &trim,
    const Params &params,
    bool allow_rotations,
    const std::chrono::steady_clock::time_point &start
) {
    const auto sheets = expand_sheets(stocks);
    if (sheets.empty()) {
        throw PlacementError("no stock available");
    }

    RunResult result;
    std::vector<libnest2d::Item> items;
    items.reserve(instances.size());

    const int64_t spacing = params.spacing;
    for (const auto &inst : instances) {
        const int64_t width = inst.width + spacing;
        const int64_t height = inst.height + spacing;
        const int64_t half_w = width / 2;
        const int64_t half_h = height / 2;
        std::vector<libnest2d::Point> poly = {
            libnest2d::Point{-half_w, -half_h},
            libnest2d::Point{width - half_w, -half_h},
            libnest2d::Point{width - half_w, height - half_h},
            libnest2d::Point{-half_w, height - half_h},
        };
        items.emplace_back(poly);
    }

    const int64_t usable_w = sheets.front().first->width - trim.left - trim.right;
    const int64_t usable_h = sheets.front().first->height - trim.top - trim.bottom;
    libnest2d::Box bin(usable_w, usable_h);

    ensure_time(start, params.time_limit_ms);
    libnest2d::NestConfig<Placer, Selector> cfg;
    if constexpr (std::is_same_v<Placer, libnest2d::BottomLeftPlacer>) {
        cfg.placer_config.allow_rotations = allow_rotations;
        if (params.bottom_left.has_min_obj_distance) {
            cfg.placer_config.min_obj_distance = params.bottom_left.min_obj_distance;
        }
        if (params.bottom_left.has_epsilon) {
            cfg.placer_config.epsilon = params.bottom_left.epsilon;
        }
    } else if constexpr (std::is_same_v<Placer, libnest2d::NfpPlacer>) {
        using ConfigT = decltype(cfg.placer_config);
        if constexpr (has_rotations_field<ConfigT>::value) {
            if (!allow_rotations) {
                cfg.placer_config.rotations = {static_cast<libnest2d::Radians>(0.0)};
            } else if (params.nfp.has_rotations) {
                std::vector<libnest2d::Radians> rotations;
                rotations.reserve(params.nfp.rotations_rad.size());
                for (double rad : params.nfp.rotations_rad) {
                    rotations.push_back(static_cast<libnest2d::Radians>(rad));
                }
                cfg.placer_config.rotations = std::move(rotations);
            }
        }
        if constexpr (has_alignment_field<ConfigT>::value) {
            if (params.nfp.has_alignment) {
                using AlignmentType = std::decay_t<decltype(cfg.placer_config.alignment)>;
                cfg.placer_config.alignment = parse_alignment_value<AlignmentType>(params.nfp.alignment);
            }
        }
        if constexpr (has_starting_point_field<ConfigT>::value) {
            if (params.nfp.has_starting_point) {
                using StartType = std::decay_t<decltype(cfg.placer_config.starting_point)>;
                cfg.placer_config.starting_point = parse_starting_point_value<StartType>(params.nfp.starting_point);
            }
        }
        if constexpr (has_accuracy_field<ConfigT>::value) {
            if (params.nfp.has_accuracy) {
                cfg.placer_config.accuracy = params.nfp.accuracy;
            }
        }
        if constexpr (has_explore_holes_field<ConfigT>::value) {
            if (params.nfp.has_explore_holes) {
                cfg.placer_config.explore_holes = params.nfp.explore_holes;
            }
        }
        if constexpr (has_parallel_field<ConfigT>::value) {
            if (params.nfp.has_parallel) {
                cfg.placer_config.parallel = params.nfp.parallel;
            }
        }
    }

    const auto bins_used = libnest2d::nest<Placer, Selector>(
        items,
        bin,
        static_cast<int>(sheets.size()),
        cfg
    );
    ensure_time(start, params.time_limit_ms);

    struct RawPlacement {
        int bin_id;
        int index;
        int64_t min_x;
        int64_t min_y;
        int64_t max_x;
        int64_t max_y;
        int64_t width;
        int64_t height;
        bool rotated;
        ItemInstance meta;
    };

    std::unordered_map<int, SheetSolution> solutions;
    std::vector<RawPlacement> raw_placements;
    raw_placements.reserve(items.size());

    for (size_t idx = 0; idx < items.size(); ++idx) {
        const auto &item = items[idx];
        const auto &meta = instances[idx];
        const auto &polygon = item.transformedShape();
        const auto bb = libnest2d::shapelike::boundingBox(polygon);
        const auto min_pt = bb.minCorner();
        const auto max_pt = bb.maxCorner();

        const int64_t min_x = min_pt.X;
        const int64_t min_y = min_pt.Y;
        const int64_t max_x = max_pt.X;
        const int64_t max_y = max_pt.Y;

        const int64_t width = max_x - min_x - spacing;
        const int64_t height = max_y - min_y - spacing;

        int bin_id = item.binId();
        if (bin_id < 0) {
            throw PlacementError("unable to place all items with available stock");
        }
        if (static_cast<size_t>(bin_id) >= sheets.size()) {
            const int alt = bin_id - 1;
            if (alt >= 0 && static_cast<size_t>(alt) < sheets.size()) {
                bin_id = alt;
            } else {
                throw PlacementError("invalid bin index from libnest2d");
            }
        }

        const bool rotated = is_rotated(item.rotation());
        raw_placements.push_back(RawPlacement{
            bin_id,
            static_cast<int>(idx + 1),
            min_x,
            min_y,
            max_x,
            max_y,
            width,
            height,
            rotated,
            meta,
        });
    }

    const CoordMapping selected_mapping = CoordMapping::CENTER_Y_UP;

    const int64_t half_w = usable_w / 2;
    const int64_t half_h = usable_h / 2;
    for (const auto &raw : raw_placements) {
        auto it = solutions.find(raw.bin_id);
        if (it == solutions.end()) {
            const auto *stock = sheets[raw.bin_id].first;
            SheetSolution sheet{
                stock->id,
                sheets[raw.bin_id].second,
                stock->width,
                stock->height,
                trim,
                {},
            };
            it = solutions.emplace(raw.bin_id, std::move(sheet)).first;
        }

        int64_t x = 0;
        int64_t y = 0;
        switch (selected_mapping) {
        case CoordMapping::ORIGIN_Y_UP:
            x = raw.min_x;
            y = usable_h - raw.max_y;
            break;
        case CoordMapping::ORIGIN_Y_DOWN:
            x = raw.min_x;
            y = raw.min_y;
            break;
        case CoordMapping::CENTER_Y_UP:
            x = raw.min_x + half_w;
            y = half_h - raw.max_y;
            break;
        case CoordMapping::CENTER_Y_DOWN:
            x = raw.min_x + half_w;
            y = raw.min_y + half_h;
            break;
        }

        it->second.placements.push_back(Placement{
            raw.meta.id,
            raw.index,
            x,
            y,
            raw.width,
            raw.height,
            raw.rotated,
            raw.meta.pattern_direction,
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

RunResult nest_run(
    const std::vector<ItemInstance> &instances,
    const std::vector<Stock> &stocks,
    const Trim &trim,
    const Params &params,
    const std::chrono::steady_clock::time_point &start
) {
    bool allow_rotations = true;
    for (const auto &inst : instances) {
        if (!inst.allow_rotation) {
            allow_rotations = false;
            break;
        }
    }

    if (params.placer == "bottom_left") {
        if (params.selector == "first_fit") {
            return nest_run_impl<libnest2d::BottomLeftPlacer, libnest2d::FirstFitSelection>(
                instances, stocks, trim, params, allow_rotations, start
            );
        }
        if (params.selector == "filler") {
            return nest_run_impl<libnest2d::BottomLeftPlacer, libnest2d::FillerSelection>(
                instances, stocks, trim, params, allow_rotations, start
            );
        }
        if (params.selector == "djd_heuristic") {
            return nest_run_impl<libnest2d::BottomLeftPlacer, libnest2d::DJDHeuristic>(
                instances, stocks, trim, params, allow_rotations, start
            );
        }
    }

    if (params.placer == "nfp") {
        throw PlacementError("nfp placer not available in this build");
    }

    throw PlacementError("unsupported placer/selector combination");
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

        const auto run_result = nest_run(shuffled, stocks, trim, params, start);
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
