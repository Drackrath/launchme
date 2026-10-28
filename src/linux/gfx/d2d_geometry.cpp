// Direct2D geometry on Cairo: rectangles, rounded rectangles, ellipses, groups, transformed and
// path geometries as figures of lines and cubic Béziers, with their measurements.
#include "d2d_internal.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numbers>

namespace lm_d2d {
namespace {
constexpr double kappa = 0.55228474983079339840; // Cubic control length of a quarter circle.

// ---- Builders -----------------------------------------------------------------------------

class CairoBuilder final : public PathBuilder {
public:
    CairoBuilder(cairo_t* context, bool fill) : context_(context), fill_(fill) {}
    void begin(D2D1_POINT_2F point, bool filled) override {
        skip_ = fill_ && !filled;
        if (!skip_) cairo_move_to(context_, point.x, point.y);
    }
    void line(D2D1_POINT_2F point) override { if (!skip_) cairo_line_to(context_, point.x, point.y); }
    void cubic(D2D1_POINT_2F c1, D2D1_POINT_2F c2, D2D1_POINT_2F end) override {
        if (!skip_) cairo_curve_to(context_, c1.x, c1.y, c2.x, c2.y, end.x, end.y);
    }
    void end(bool closed) override { if (!skip_ && closed) cairo_close_path(context_); }
private:
    cairo_t* context_;
    bool fill_;
    bool skip_ = false;
};

class FlattenBuilder final : public PathBuilder {
public:
    explicit FlattenBuilder(double tolerance) : tolerance_(std::max(tolerance, 1e-4)) {}
    void begin(D2D1_POINT_2F point, bool filled) override {
        lines_.push_back({{{point.x, point.y}}, false, filled});
    }
    void line(D2D1_POINT_2F point) override { if (!lines_.empty()) add({point.x, point.y}); }
    void cubic(D2D1_POINT_2F c1, D2D1_POINT_2F c2, D2D1_POINT_2F end) override {
        if (lines_.empty()) return;
        const Point p0 = lines_.back().points.back(), p1{c1.x, c1.y}, p2{c2.x, c2.y}, p3{end.x, end.y};
        // Wang's formula: segments enough for the curve to stay within the tolerance.
        const double ax = p0.x - 2 * p1.x + p2.x, ay = p0.y - 2 * p1.y + p2.y;
        const double bx = p1.x - 2 * p2.x + p3.x, by = p1.y - 2 * p2.y + p3.y;
        const double bend = std::sqrt(std::max(ax * ax + ay * ay, bx * bx + by * by));
        const int steps = std::clamp(static_cast<int>(std::ceil(std::sqrt(0.75 * bend / tolerance_))), 1, 1024);
        for (int i = 1; i < steps; ++i) {
            const double t = static_cast<double>(i) / steps, u = 1 - t;
            const double w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
            add({w0 * p0.x + w1 * p1.x + w2 * p2.x + w3 * p3.x, w0 * p0.y + w1 * p1.y + w2 * p2.y + w3 * p3.y});
        }
        add(p3);
    }
    void end(bool closed) override { if (!lines_.empty()) lines_.back().closed = closed; }
    std::vector<Polyline> take() { return std::move(lines_); }
private:
    void add(Point point) { lines_.back().points.push_back(point); }
    double tolerance_;
    std::vector<Polyline> lines_;
};

// The arc of an SVG-style elliptical arc segment from `from` as cubic Béziers (SVG 1.1, F.6.5).
void emitArc(PathBuilder& out, D2D1_POINT_2F from, const D2D1_ARC_SEGMENT& arc) {
    const D2D1_POINT_2F to = arc.point;
    double rx = std::abs(arc.size.width), ry = std::abs(arc.size.height);
    if ((from.x == to.x && from.y == to.y)) return;
    if (rx == 0 || ry == 0) { out.line(to); return; }
    const double phi = arc.rotationAngle * std::numbers::pi / 180, cos_phi = std::cos(phi), sin_phi = std::sin(phi);
    const double hx = (from.x - to.x) * 0.5, hy = (from.y - to.y) * 0.5;
    const double x1 = cos_phi * hx + sin_phi * hy, y1 = -sin_phi * hx + cos_phi * hy;
    const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) { rx *= std::sqrt(lambda); ry *= std::sqrt(lambda); }
    const bool sweep = arc.sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE, large = arc.arcSize == D2D1_ARC_SIZE_LARGE;
    const double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double factor = denominator > 0 ? std::sqrt(std::max(0.0, numerator / denominator)) : 0;
    if (large == sweep) factor = -factor;
    const double cx1 = factor * rx * y1 / ry, cy1 = -factor * ry * x1 / rx;
    const double cx = cos_phi * cx1 - sin_phi * cy1 + (from.x + to.x) * 0.5;
    const double cy = sin_phi * cx1 + cos_phi * cy1 + (from.y + to.y) * 0.5;
    const auto angle = [](double ux, double uy, double vx, double vy) { return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
    const double start = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    double delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
    if (!sweep && delta > 0) delta -= 2 * std::numbers::pi;
    else if (sweep && delta < 0) delta += 2 * std::numbers::pi;
    const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (std::numbers::pi / 2) - 1e-9)));
    const double step = delta / pieces, handle = 4.0 / 3.0 * std::tan(step / 4);
    const auto at = [&](double a) { // Point and derivative on the ellipse.
        const double ex = rx * std::cos(a), ey = ry * std::sin(a), dx = -rx * std::sin(a), dy = ry * std::cos(a);
        return std::array<double, 4>{cx + cos_phi * ex - sin_phi * ey, cy + sin_phi * ex + cos_phi * ey,
                                     cos_phi * dx - sin_phi * dy, sin_phi * dx + cos_phi * dy};
    };
    for (int i = 0; i < pieces; ++i) {
        const auto a = at(start + step * i), b = at(start + step * (i + 1));
        const D2D1_POINT_2F end = i == pieces - 1 ? to : D2D1_POINT_2F{static_cast<float>(b[0]), static_cast<float>(b[1])};
        out.cubic({static_cast<float>(a[0] + handle * a[2]), static_cast<float>(a[1] + handle * a[3])},
                  {static_cast<float>(b[0] - handle * b[2]), static_cast<float>(b[1] - handle * b[3])}, end);
    }
}

// A quarter of an ellipse from `from` (on an axis) to `to` around the corner `corner`.
void quarter(PathBuilder& out, const cairo_matrix_t& m, D2D1_POINT_2F from, D2D1_POINT_2F corner, D2D1_POINT_2F to) {
    const auto mix = [](D2D1_POINT_2F a, D2D1_POINT_2F b) {
        return D2D1_POINT_2F{static_cast<float>(a.x + (b.x - a.x) * kappa), static_cast<float>(a.y + (b.y - a.y) * kappa)};
    };
    out.cubic(apply(m, mix(from, corner)), apply(m, mix(to, corner)), apply(m, to));
}

// Clockwise from the top-left corner, as Direct2D's rectangle geometry runs.
void emitRectangle(PathBuilder& out, const D2D1_RECT_F& r, const cairo_matrix_t& m) {
    out.begin(apply(m, {r.left, r.top}), true);
    out.line(apply(m, {r.right, r.top}));
    out.line(apply(m, {r.right, r.bottom}));
    out.line(apply(m, {r.left, r.bottom}));
    out.end(true);
}

// Radii clamped to half the sides, as Direct2D does.
void emitRoundedRectangle(PathBuilder& out, const D2D1_ROUNDED_RECT& rounded, const cairo_matrix_t& m) {
    const float left = std::min(rounded.rect.left, rounded.rect.right), right = std::max(rounded.rect.left, rounded.rect.right);
    const float top = std::min(rounded.rect.top, rounded.rect.bottom), bottom = std::max(rounded.rect.top, rounded.rect.bottom);
    const float rx = std::min(std::abs(rounded.radiusX), (right - left) * 0.5f), ry = std::min(std::abs(rounded.radiusY), (bottom - top) * 0.5f);
    if (rx <= 0 || ry <= 0) { emitRectangle(out, {left, top, right, bottom}, m); return; }
    out.begin(apply(m, {left + rx, top}), true);
    out.line(apply(m, {right - rx, top}));
    quarter(out, m, {right - rx, top}, {right, top}, {right, top + ry});
    out.line(apply(m, {right, bottom - ry}));
    quarter(out, m, {right, bottom - ry}, {right, bottom}, {right - rx, bottom});
    out.line(apply(m, {left + rx, bottom}));
    quarter(out, m, {left + rx, bottom}, {left, bottom}, {left, bottom - ry});
    out.line(apply(m, {left, top + ry}));
    quarter(out, m, {left, top + ry}, {left, top}, {left + rx, top});
    out.end(true);
}

void emitEllipse(PathBuilder& out, const D2D1_ELLIPSE& e, const cairo_matrix_t& m) {
    const float cx = e.point.x, cy = e.point.y, rx = std::abs(e.radiusX), ry = std::abs(e.radiusY);
    out.begin(apply(m, {cx + rx, cy}), true);
    quarter(out, m, {cx + rx, cy}, {cx + rx, cy + ry}, {cx, cy + ry});
    quarter(out, m, {cx, cy + ry}, {cx - rx, cy + ry}, {cx - rx, cy});
    quarter(out, m, {cx - rx, cy}, {cx - rx, cy - ry}, {cx, cy - ry});
    quarter(out, m, {cx, cy - ry}, {cx + rx, cy - ry}, {cx + rx, cy});
    out.end(true);
}

bool inside(const std::vector<Polyline>& lines, D2D1_FILL_MODE mode, double x, double y) {
    int winding = 0;
    for (const auto& line : lines) {
        if (!line.filled || line.points.size() < 2) continue;
        const auto& p = line.points;
        for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
            const Point a = p[j], b = p[i];
            if ((a.y <= y) != (b.y <= y)) {
                const double cross = (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y);
                if (b.y > a.y ? cross > 0 : cross < 0) winding += b.y > a.y ? 1 : -1;
            }
        }
    }
    return mode == D2D1_FILL_MODE_WINDING ? winding != 0 : (winding & 1) != 0;
}

// Every segment of the figures in drawing order (closed figures back to their start).
template <class Visit>
void segments(const std::vector<Polyline>& lines, Visit&& visit) {
    for (const auto& line : lines) {
        const auto& p = line.points;
        for (size_t i = 1; i < p.size(); ++i) visit(p[i - 1], p[i]);
        if (line.closed && p.size() > 1) visit(p.back(), p.front());
    }
}

cairo_matrix_t worldOf(const D2D1_MATRIX_3X2_F* transform) { return transform ? toCairo(*transform) : identityMatrix(); }

// ---- Geometry objects ---------------------------------------------------------------------

// The ID2D1Geometry methods every geometry shares, on top of Geometry::emit.
template <class Interface>
class GeometryObject : public lm_com::Object<Interface>, public Geometry {
public:
    HRESULT STDMETHODCALLTYPE GetBounds(const D2D1_MATRIX_3X2_F* transform, D2D1_RECT_F* bounds) const override {
        if (!bounds) return E_INVALIDARG;
        double left = DBL_MAX, top = DBL_MAX, right = -DBL_MAX, bottom = -DBL_MAX;
        for (const auto& line : flatten(*this, worldOf(transform), 0.01))
            for (const auto& p : line.points) {
                left = std::min(left, p.x); top = std::min(top, p.y);
                right = std::max(right, p.x); bottom = std::max(bottom, p.y);
            }
        if (left > right) *bounds = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX}; // Empty, as Direct2D reports it.
        else *bounds = {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FillContainsPoint(D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F* transform, FLOAT tolerance,
                                                BOOL* contains) const override {
        if (!contains) return E_INVALIDARG;
        *contains = inside(flatten(*this, worldOf(transform), std::min(tolerance, D2D1_DEFAULT_FLATTENING_TOLERANCE)), fillMode(), point.x, point.y);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CombineWithGeometry(ID2D1Geometry* input, D2D1_COMBINE_MODE mode, const D2D1_MATRIX_3X2_F* transform,
                                                  FLOAT tolerance, ID2D1SimplifiedGeometrySink* sink) const override {
        const auto* other = Geometry::from(input);
        if (!other || !sink) return E_INVALIDARG;
        return combineGeometries(*this, *other, worldOf(transform), mode, tolerance, sink);
    }
    HRESULT STDMETHODCALLTYPE ComputeLength(const D2D1_MATRIX_3X2_F* transform, FLOAT tolerance, FLOAT* length) const override {
        if (!length) return E_INVALIDARG;
        double total = 0;
        segments(flatten(*this, worldOf(transform), measureTolerance(tolerance)),
                 [&](Point a, Point b) { total += std::hypot(b.x - a.x, b.y - a.y); });
        *length = static_cast<float>(total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ComputePointAtLength(FLOAT length, const D2D1_MATRIX_3X2_F* transform, FLOAT tolerance,
                                                   D2D1_POINT_2F* point, D2D1_POINT_2F* tangent) const override {
        const auto lines = flatten(*this, worldOf(transform), measureTolerance(tolerance));
        double left = std::max(0.0, static_cast<double>(length));
        bool found = false, any = false;
        Point at{}, direction{1, 0};
        segments(lines, [&](Point a, Point b) {
            const double piece = std::hypot(b.x - a.x, b.y - a.y);
            if (found || piece <= 0) return;
            any = true;
            direction = {(b.x - a.x) / piece, (b.y - a.y) / piece};
            const double t = std::min(1.0, left / piece);
            at = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            if (left <= piece) found = true;
            else left -= piece;
        });
        if (!any) {
            if (lines.empty() || lines.front().points.empty()) return D2DERR_WRONG_STATE;
            at = lines.front().points.front();
        }
        if (point) *point = {static_cast<float>(at.x), static_cast<float>(at.y)};
        if (tangent) *tangent = {static_cast<float>(direction.x), static_cast<float>(direction.y)};
        return S_OK;
    }
private:
    static double measureTolerance(FLOAT tolerance) { return std::min<double>(tolerance > 0 ? tolerance : 0.25, 0.25) * 0.25; }
};

class RectangleGeometry final : public GeometryObject<ID2D1RectangleGeometry> {
public:
    explicit RectangleGeometry(const D2D1_RECT_F& rect) : rect_(rect) {}
    void emit(PathBuilder& out, const cairo_matrix_t& matrix) const override { emitRectangle(out, rect_, matrix); }
private:
    D2D1_RECT_F rect_;
};

class RoundedRectangleGeometry final : public GeometryObject<ID2D1RoundedRectangleGeometry> {
public:
    explicit RoundedRectangleGeometry(const D2D1_ROUNDED_RECT& rounded) : rounded_(rounded) {}
    void emit(PathBuilder& out, const cairo_matrix_t& matrix) const override { emitRoundedRectangle(out, rounded_, matrix); }
private:
    D2D1_ROUNDED_RECT rounded_;
};

class EllipseGeometry final : public GeometryObject<ID2D1EllipseGeometry> {
public:
    explicit EllipseGeometry(const D2D1_ELLIPSE& ellipse) : ellipse_(ellipse) {}
    void emit(PathBuilder& out, const cairo_matrix_t& matrix) const override { emitEllipse(out, ellipse_, matrix); }
private:
    D2D1_ELLIPSE ellipse_;
};

// The children's figures together, filled by the group's own fill mode.
class GeometryGroup final : public GeometryObject<ID2D1GeometryGroup> {
public:
    GeometryGroup(D2D1_FILL_MODE mode, std::vector<ComPtr<ID2D1Geometry>> children) : mode_(mode), children_(std::move(children)) {}
    void emit(PathBuilder& out, const cairo_matrix_t& matrix) const override {
        for (const auto& child : children_) if (const auto* geometry = Geometry::from(child.Get())) geometry->emit(out, matrix);
    }
    D2D1_FILL_MODE fillMode() const override { return mode_; }
private:
    D2D1_FILL_MODE mode_;
    std::vector<ComPtr<ID2D1Geometry>> children_;
};

// The source's points mapped by the transform. Strokes are not widened by it: the figures are
// emitted already transformed and stroked in the render target's world space.
class TransformedGeometry final : public GeometryObject<ID2D1TransformedGeometry> {
public:
    TransformedGeometry(ComPtr<ID2D1Geometry> source, const D2D1_MATRIX_3X2_F& transform)
        : source_(std::move(source)), transform_(toCairo(transform)) {}
    void emit(PathBuilder& out, const cairo_matrix_t& matrix) const override {
        if (const auto* geometry = Geometry::from(source_.Get())) geometry->emit(out, multiply(transform_, matrix));
    }
    D2D1_FILL_MODE fillMode() const override {
        const auto* geometry = Geometry::from(source_.Get());
        return geometry ? geometry->fillMode() : D2D1_FILL_MODE_ALTERNATE;
    }
private:
    ComPtr<ID2D1Geometry> source_;
    cairo_matrix_t transform_;
};

struct Segment { bool curve; D2D1_POINT_2F control1, control2, end; };
struct Figure { D2D1_POINT_2F start; bool filled; bool closed = false; std::vector<Segment> segments; };

class PathGeometry final : public GeometryObject<ID2D1PathGeometry> {
public:
    HRESULT STDMETHODCALLTYPE Open(ID2D1GeometrySink** sink) override;
    void emit(PathBuilder& out, const cairo_matrix_t& m) const override {
        for (const auto& figure : figures_) {
            out.begin(apply(m, figure.start), figure.filled);
            for (const auto& s : figure.segments) {
                if (s.curve) out.cubic(apply(m, s.control1), apply(m, s.control2), apply(m, s.end));
                else out.line(apply(m, s.end));
            }
            out.end(figure.closed);
        }
    }
    D2D1_FILL_MODE fillMode() const override { return mode_; }
private:
    friend class GeometrySink;
    std::vector<Figure> figures_;
    D2D1_FILL_MODE mode_ = D2D1_FILL_MODE_ALTERNATE;
    bool opened_ = false;
};

// Writes into its path geometry until Close. Segments outside BeginFigure/EndFigure are dropped.
class GeometrySink final : public lm_com::Object<ID2D1GeometrySink> {
public:
    explicit GeometrySink(ComPtr<PathGeometry> path) : path_(std::move(path)) {}
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode) override { if (!closed_) path_->mode_ = mode; }
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin) override {
        if (closed_) return;
        path_->figures_.push_back({start, begin == D2D1_FIGURE_BEGIN_FILLED});
        current_ = start;
        open_ = true;
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 count) override {
        for (UINT32 i = 0; points && i < count; ++i) AddLine(points[i]);
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 count) override {
        for (UINT32 i = 0; beziers && i < count; ++i) AddBezier(&beziers[i]);
    }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) override {
        if (open_) path_->figures_.back().closed = end == D2D1_FIGURE_END_CLOSED;
        open_ = false;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        if (closed_) return D2DERR_WRONG_STATE;
        closed_ = true;
        return open_ ? D2DERR_WRONG_STATE : S_OK;
    }
    void STDMETHODCALLTYPE AddLine(D2D1_POINT_2F point) override { add({false, {}, {}, point}); }
    void STDMETHODCALLTYPE AddBezier(const D2D1_BEZIER_SEGMENT* b) override { if (b) add({true, b->point1, b->point2, b->point3}); }
    void STDMETHODCALLTYPE AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT* q) override {
        if (!q) return;
        const auto toward = [](D2D1_POINT_2F from, D2D1_POINT_2F control) {
            return D2D1_POINT_2F{from.x + (control.x - from.x) * 2 / 3, from.y + (control.y - from.y) * 2 / 3};
        };
        add({true, toward(current_, q->point1), toward(q->point2, q->point1), q->point2});
    }
    void STDMETHODCALLTYPE AddQuadraticBeziers(const D2D1_QUADRATIC_BEZIER_SEGMENT* beziers, UINT32 count) override {
        for (UINT32 i = 0; beziers && i < count; ++i) AddQuadraticBezier(&beziers[i]);
    }
    void STDMETHODCALLTYPE AddArc(const D2D1_ARC_SEGMENT* arc) override {
        if (!arc || !open_ || closed_) return;
        // Recorded as the Béziers the arc becomes.
        class Recorder final : public PathBuilder {
        public:
            explicit Recorder(GeometrySink& sink) : sink_(sink) {}
            void begin(D2D1_POINT_2F, bool) override {}
            void line(D2D1_POINT_2F point) override { sink_.AddLine(point); }
            void cubic(D2D1_POINT_2F c1, D2D1_POINT_2F c2, D2D1_POINT_2F end) override { sink_.add({true, c1, c2, end}); }
            void end(bool) override {}
        private:
            GeometrySink& sink_;
        } recorder(*this);
        emitArc(recorder, current_, *arc);
    }
private:
    void add(const Segment& segment) {
        if (!open_ || closed_) return;
        path_->figures_.back().segments.push_back(segment);
        current_ = segment.end;
    }
    ComPtr<PathGeometry> path_;
    D2D1_POINT_2F current_{};
    bool open_ = false;
    bool closed_ = false;
};

HRESULT STDMETHODCALLTYPE PathGeometry::Open(ID2D1GeometrySink** sink) {
    if (!sink) return E_POINTER;
    *sink = nullptr;
    if (opened_) return D2DERR_WRONG_STATE;
    opened_ = true;
    *sink = new GeometrySink(ComPtr<PathGeometry>(this));
    return S_OK;
}

template <class Object, class Interface, class... Arguments>
HRESULT make(Interface** out, Arguments&&... arguments) {
    if (!out) return E_POINTER;
    *out = new Object(std::forward<Arguments>(arguments)...);
    return S_OK;
}
} // namespace

const Geometry* Geometry::from(ID2D1Geometry* geometry) { return dynamic_cast<const Geometry*>(geometry); }

std::vector<Polyline> flatten(const Geometry& geometry, const cairo_matrix_t& matrix, double tolerance) {
    FlattenBuilder builder(tolerance);
    geometry.emit(builder, matrix);
    return builder.take();
}

void appendPath(cairo_t* context, const Geometry& geometry, bool fill) {
    CairoBuilder builder(context, fill);
    geometry.emit(builder, identityMatrix());
    if (fill) cairo_set_fill_rule(context, geometry.fillMode() == D2D1_FILL_MODE_WINDING ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
}

void appendRoundedRectangle(cairo_t* context, const D2D1_ROUNDED_RECT& rounded) {
    CairoBuilder builder(context, false);
    emitRoundedRectangle(builder, rounded, identityMatrix());
}

void appendEllipse(cairo_t* context, const D2D1_ELLIPSE& ellipse) {
    CairoBuilder builder(context, false);
    emitEllipse(builder, ellipse, identityMatrix());
}

HRESULT createRectangleGeometry(const D2D1_RECT_F& rect, ID2D1RectangleGeometry** geometry) {
    return make<RectangleGeometry>(geometry, rect);
}
HRESULT createRoundedRectangleGeometry(const D2D1_ROUNDED_RECT& rounded, ID2D1RoundedRectangleGeometry** geometry) {
    return make<RoundedRectangleGeometry>(geometry, rounded);
}
HRESULT createEllipseGeometry(const D2D1_ELLIPSE& ellipse, ID2D1EllipseGeometry** geometry) {
    return make<EllipseGeometry>(geometry, ellipse);
}
HRESULT createGeometryGroup(D2D1_FILL_MODE mode, ID2D1Geometry** geometries, UINT32 count, ID2D1GeometryGroup** group) {
    std::vector<ComPtr<ID2D1Geometry>> children;
    for (UINT32 i = 0; geometries && i < count; ++i) {
        if (!Geometry::from(geometries[i])) return E_INVALIDARG;
        children.emplace_back(geometries[i]);
    }
    return make<GeometryGroup>(group, mode, std::move(children));
}
HRESULT createTransformedGeometry(ID2D1Geometry* source, const D2D1_MATRIX_3X2_F& transform, ID2D1TransformedGeometry** geometry) {
    if (!Geometry::from(source)) return E_INVALIDARG;
    return make<TransformedGeometry>(geometry, ComPtr<ID2D1Geometry>(source), transform);
}
HRESULT createPathGeometry(ID2D1PathGeometry** geometry) { return make<PathGeometry>(geometry); }
} // namespace lm_d2d
