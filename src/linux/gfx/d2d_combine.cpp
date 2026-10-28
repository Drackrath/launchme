// CombineWithGeometry: union, intersection, exclusion and xor of two geometries' fills. Both are
// flattened to polygons; every edge is split where any two edges meet; an edge piece stays in
// the result when the result differs on its two sides (each input sampled with its own fill
// mode), and the kept pieces are linked into closed figures, the inside always on one side.
#include "d2d_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace lm_d2d {
namespace {
struct Edge { Point a, b; int owner; };

Point minus(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
double cross(Point a, Point b) { return a.x * b.y - a.y * b.x; }
double dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }
bool same(Point a, Point b) { return a.x == b.x && a.y == b.y; }

void collect(const std::vector<Polyline>& lines, int owner, std::vector<Edge>& edges) {
    for (const auto& line : lines) {
        const auto& p = line.points;
        if (!line.filled || p.size() < 3) continue; // Hollow figures and lines enclose nothing.
        for (size_t i = 0; i < p.size(); ++i) {
            const Point a = p[i], b = p[(i + 1) % p.size()];
            if (!same(a, b)) edges.push_back({a, b, owner});
        }
    }
}

// Whether points lie inside one input's fill, edges bucketed in horizontal bands.
class Fill {
public:
    Fill(const std::vector<Edge>& edges, int owner, D2D1_FILL_MODE mode) : edges_(edges), mode_(mode) {
        std::vector<int> mine;
        for (int i = 0; i < static_cast<int>(edges.size()); ++i) if (edges[i].owner == owner) mine.push_back(i);
        if (mine.empty()) return;
        top_ = bottom_ = edges[mine.front()].a.y;
        for (const int i : mine) {
            top_ = std::min({top_, edges[i].a.y, edges[i].b.y});
            bottom_ = std::max({bottom_, edges[i].a.y, edges[i].b.y});
        }
        bands_.resize(std::clamp(static_cast<size_t>(std::sqrt(static_cast<double>(mine.size())) * 2), size_t{1}, size_t{1024}));
        height_ = std::max((bottom_ - top_) / bands_.size(), 1e-12);
        for (const int i : mine) {
            const auto [low, high] = std::minmax(edges[i].a.y, edges[i].b.y);
            for (size_t band = this->band(low); band <= this->band(high); ++band) bands_[band].push_back(i);
        }
    }
    bool inside(Point p) const {
        if (bands_.empty() || p.y < top_ || p.y > bottom_) return false;
        int winding = 0;
        for (const int i : bands_[band(p.y)]) {
            const Point a = edges_[i].a, b = edges_[i].b;
            if ((a.y <= p.y) == (b.y <= p.y)) continue;
            const double side = cross(minus(b, a), minus(p, a));
            if (b.y > a.y && side > 0) ++winding;
            else if (b.y < a.y && side < 0) --winding;
        }
        return mode_ == D2D1_FILL_MODE_WINDING ? winding != 0 : (winding & 1) != 0;
    }
private:
    size_t band(double y) const {
        return static_cast<size_t>(std::clamp((y - top_) / height_, 0.0, static_cast<double>(bands_.size() - 1)));
    }
    const std::vector<Edge>& edges_;
    D2D1_FILL_MODE mode_;
    double top_ = 0, bottom_ = 0, height_ = 1;
    std::vector<std::vector<int>> bands_;
};

bool result(D2D1_COMBINE_MODE mode, bool first, bool second) {
    switch (mode) {
    case D2D1_COMBINE_MODE_UNION: return first || second;
    case D2D1_COMBINE_MODE_INTERSECT: return first && second;
    case D2D1_COMBINE_MODE_XOR: return first != second;
    case D2D1_COMBINE_MODE_EXCLUDE: return first && !second;
    }
    return false;
}

// Points along each edge where another edge crosses or touches it (excluding its ends).
std::vector<std::vector<Point>> splits(const std::vector<Edge>& edges) {
    std::vector<std::vector<Point>> at(edges.size());
    const auto add = [&](size_t i, Point p) {
        if (!same(p, edges[i].a) && !same(p, edges[i].b)) at[i].push_back(p);
    };
    std::vector<size_t> order(edges.size());
    std::iota(order.begin(), order.end(), size_t{0});
    const auto left = [&](size_t i) { return std::min(edges[i].a.x, edges[i].b.x); };
    const auto right = [&](size_t i) { return std::max(edges[i].a.x, edges[i].b.x); };
    std::sort(order.begin(), order.end(), [&](size_t i, size_t j) { return left(i) < left(j); });
    constexpr double near = 1e-10; // Edge parameters this close to an end are that end.
    for (size_t n = 0; n < order.size(); ++n) {
        const size_t i = order[n];
        const Edge& e = edges[i];
        const Point r = minus(e.b, e.a);
        const double e_top = std::min(e.a.y, e.b.y), e_bottom = std::max(e.a.y, e.b.y);
        for (size_t m = n + 1; m < order.size() && left(order[m]) <= right(i); ++m) {
            const size_t j = order[m];
            const Edge& f = edges[j];
            if (std::max(f.a.y, f.b.y) < e_top || std::min(f.a.y, f.b.y) > e_bottom) continue;
            const Point s = minus(f.b, f.a), q = minus(f.a, e.a);
            const double rr = dot(r, r), ss = dot(s, s), denominator = cross(r, s);
            if (std::abs(denominator) > 1e-12 * std::sqrt(rr * ss)) {
                const double t = cross(q, s) / denominator, u = cross(q, r) / denominator;
                if (t < -near || t > 1 + near || u < -near || u > 1 + near) continue;
                // Shared and touching ends keep their exact coordinates.
                const Point p = u <= near ? f.a : u >= 1 - near ? f.b : t <= near ? e.a : t >= 1 - near ? e.b :
                                Point{e.a.x + r.x * t, e.a.y + r.y * t};
                add(i, p);
                add(j, p);
                continue;
            }
            // Parallel: overlapping collinear edges split each other at their ends.
            const double reach = 1e-9 * std::sqrt(std::max(rr, ss)) + 1e-12;
            if (std::abs(cross(r, q)) / std::sqrt(rr) > reach || std::abs(cross(r, minus(f.b, e.a))) / std::sqrt(rr) > reach) continue;
            for (const Point p : {f.a, f.b}) {
                const double t = dot(minus(p, e.a), r) / rr;
                if (t > near && t < 1 - near) add(i, p);
            }
            for (const Point p : {e.a, e.b}) {
                const double u = dot(minus(p, f.a), s) / ss;
                if (u > near && u < 1 - near) add(j, p);
            }
        }
    }
    return at;
}

// Vertices merged within `reach` of each other (a grid of that size, neighbours searched).
class Vertices {
public:
    explicit Vertices(double reach) : reach_(reach) {}
    int find(Point p) {
        const long long cx = std::llround(p.x / reach_), cy = std::llround(p.y / reach_);
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dy = -1; dy <= 1; ++dy) {
                const auto found = cells_.find(key(cx + dx, cy + dy));
                if (found == cells_.end()) continue;
                for (const int v : found->second)
                    if (std::abs(points_[v].x - p.x) <= reach_ && std::abs(points_[v].y - p.y) <= reach_) return v;
            }
        points_.push_back(p);
        cells_[key(cx, cy)].push_back(static_cast<int>(points_.size() - 1));
        return static_cast<int>(points_.size() - 1);
    }
    Point operator[](int v) const { return points_[v]; }
private:
    static uint64_t key(long long x, long long y) { return static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(y); }
    double reach_;
    std::vector<Point> points_;
    std::unordered_map<uint64_t, std::vector<int>> cells_;
};

// Drops points on a straight run between their neighbours (the split points of one edge).
std::vector<Point> straighten(std::vector<Point> loop) {
    for (bool changed = true; changed && loop.size() > 3;) {
        changed = false;
        for (size_t i = 0; i < loop.size() && loop.size() > 3; ++i) {
            const Point a = loop[(i + loop.size() - 1) % loop.size()], b = loop[i], c = loop[(i + 1) % loop.size()];
            const Point u = minus(b, a), v = minus(c, b);
            if (std::abs(cross(u, v)) <= 1e-12 * std::sqrt(dot(u, u) * dot(v, v)) && dot(u, v) > 0) {
                loop.erase(loop.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
            }
        }
    }
    return loop;
}
} // namespace

HRESULT combineGeometries(const Geometry& first, const Geometry& second, const cairo_matrix_t& secondTransform, D2D1_COMBINE_MODE mode,
                          double tolerance, ID2D1SimplifiedGeometrySink* sink) {
    // Finer than asked: the result is drawn scaled up (viewport, design canvases) and has no curves.
    const double step = std::clamp(tolerance, 1e-3, 0.25) * 0.1;
    std::vector<Edge> edges;
    collect(flatten(first, identityMatrix(), step), 0, edges);
    collect(flatten(second, secondTransform, step), 1, edges);
    double extent = 1;
    for (const auto& e : edges) extent = std::max({extent, std::abs(e.a.x), std::abs(e.a.y)});
    const Fill fill_first(edges, 0, first.fillMode()), fill_second(edges, 1, second.fillMode());

    // Edge pieces between consecutive split points, each once.
    Vertices vertices(extent * 1e-9);
    std::vector<std::pair<int, int>> pieces;
    std::unordered_set<uint64_t> seen;
    const auto split_points = splits(edges);
    for (size_t i = 0; i < edges.size(); ++i) {
        auto points = split_points[i];
        const Point a = edges[i].a, r = minus(edges[i].b, a);
        std::sort(points.begin(), points.end(), [&](Point p, Point q) { return dot(minus(p, a), r) < dot(minus(q, a), r); });
        points.insert(points.begin(), a);
        points.push_back(edges[i].b);
        int previous = vertices.find(points.front());
        for (size_t k = 1; k < points.size(); ++k) {
            const int next = vertices.find(points[k]);
            if (next == previous) continue;
            const uint64_t id = static_cast<uint64_t>(std::min(previous, next)) << 32 | static_cast<uint32_t>(std::max(previous, next));
            if (seen.insert(id).second) pieces.emplace_back(previous, next);
            previous = next;
        }
    }

    // Kept pieces, directed with the result's inside on their left.
    std::vector<std::pair<int, int>> kept;
    for (const auto& [from, to] : pieces) {
        const Point a = vertices[from], b = vertices[to], d = minus(b, a);
        const double length = std::sqrt(dot(d, d));
        const double offset = std::max(std::min(length * 0.01, 1e-4), extent * 1e-8);
        const Point middle{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5}, normal{-d.y / length * offset, d.x / length * offset};
        const Point left{middle.x + normal.x, middle.y + normal.y}, right{middle.x - normal.x, middle.y - normal.y};
        const bool in_left = result(mode, fill_first.inside(left), fill_second.inside(left));
        const bool in_right = result(mode, fill_first.inside(right), fill_second.inside(right));
        if (in_left != in_right) kept.emplace_back(in_left ? from : to, in_left ? to : from);
    }

    // Linked into closed figures.
    std::unordered_map<int, std::vector<size_t>> outgoing;
    for (size_t i = 0; i < kept.size(); ++i) outgoing[kept[i].first].push_back(i);
    std::vector<bool> used(kept.size(), false);
    for (size_t first_piece = 0; first_piece < kept.size(); ++first_piece) {
        if (used[first_piece]) continue;
        std::vector<Point> loop;
        const int start = kept[first_piece].first;
        size_t piece = first_piece;
        for (;;) {
            used[piece] = true;
            loop.push_back(vertices[kept[piece].first]);
            const int at = kept[piece].second;
            if (at == start) break;
            const auto found = outgoing.find(at);
            if (found == outgoing.end()) break;
            const auto next = std::find_if(found->second.begin(), found->second.end(), [&](size_t i) { return !used[i]; });
            if (next == found->second.end()) break;
            piece = *next;
        }
        loop = straighten(std::move(loop));
        if (loop.size() < 3) continue;
        std::vector<D2D1_POINT_2F> points;
        for (size_t i = 1; i < loop.size(); ++i) points.push_back({static_cast<float>(loop[i].x), static_cast<float>(loop[i].y)});
        sink->BeginFigure({static_cast<float>(loop[0].x), static_cast<float>(loop[0].y)}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(points.data(), static_cast<UINT32>(points.size()));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    return S_OK;
}
} // namespace lm_d2d
