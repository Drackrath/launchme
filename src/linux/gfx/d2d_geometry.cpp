// Direct2D geometry on Skia paths: rectangles, rounded rectangles, ellipses, groups, transformed
// and path geometries, their measurements (SkContourMeasure) and boolean combinations (SkPathOps).
// A geometry keeps two paths: every figure (strokes) and the filled figures with the fill rule
// (fills): Direct2D's hollow figures are stroked but never filled.
#include "d2d_internal.hpp"

#include "include/core/SkContourMeasure.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/pathops/SkPathOps.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lm_d2d {
namespace {
SkPathFillType fillTypeFor(D2D1_FILL_MODE mode) { return mode == D2D1_FILL_MODE_WINDING ? SkPathFillType::kWinding : SkPathFillType::kEvenOdd; }

// Clockwise from the top-left corner, as Direct2D's rectangle geometry runs.
SkPath rectanglePath(const D2D1_RECT_F& r) {
    SkPathBuilder builder;
    builder.moveTo(r.left, r.top).lineTo(r.right, r.top).lineTo(r.right, r.bottom).lineTo(r.left, r.bottom).close();
    return builder.detach();
}

// The ID2D1Geometry methods every geometry shares; the subclass sets its two paths.
template <class Interface>
class GeometryObject : public lm_com::Object<Interface>, public Geometry {
public:
    const SkPath& outline() const override { return outline_; }
    const SkPath& area() const override { return area_; }

    HRESULT STDMETHODCALLTYPE GetBounds(const D2D1_MATRIX_3X2_F* transform, D2D1_RECT_F* bounds) const override {
        if (!bounds) return E_INVALIDARG;
        if (outline_.isEmpty()) { *bounds = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX}; return S_OK; } // Empty, as Direct2D reports it.
        const SkRect box = (transform ? outline_.makeTransform(toSk(*transform)) : outline_).computeTightBounds();
        *bounds = {box.left(), box.top(), box.right(), box.bottom()};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FillContainsPoint(D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F* transform, FLOAT, BOOL* contains) const override {
        if (!contains) return E_INVALIDARG;
        *contains = (transform ? area_.makeTransform(toSk(*transform)) : area_).contains(point.x, point.y);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CombineWithGeometry(ID2D1Geometry* input, D2D1_COMBINE_MODE mode, const D2D1_MATRIX_3X2_F* transform,
                                                  FLOAT, ID2D1SimplifiedGeometrySink* sink) const override;
    HRESULT STDMETHODCALLTYPE ComputeLength(const D2D1_MATRIX_3X2_F* transform, FLOAT, FLOAT* length) const override {
        if (!length) return E_INVALIDARG;
        double total = 0;
        SkContourMeasureIter contours(transform ? outline_.makeTransform(toSk(*transform)) : outline_, false);
        while (const auto contour = contours.next()) total += contour->length();
        *length = static_cast<float>(total);
        return S_OK;
    }
    // Past the end: the end of the last figure.
    HRESULT STDMETHODCALLTYPE ComputePointAtLength(FLOAT length, const D2D1_MATRIX_3X2_F* transform, FLOAT, D2D1_POINT_2F* point,
                                                   D2D1_POINT_2F* tangent) const override {
        const SkPath path = transform ? outline_.makeTransform(toSk(*transform)) : outline_;
        SkContourMeasureIter contours(path, false);
        float left = std::max(0.0f, length);
        sk_sp<SkContourMeasure> last;
        SkPoint at{}, direction{1, 0};
        bool found = false;
        while (auto contour = contours.next()) {
            last = contour;
            if (left <= contour->length()) {
                found = contour->getPosTan(left, &at, &direction);
                break;
            }
            left -= contour->length();
        }
        if (!found) {
            if (last) (void)last->getPosTan(last->length(), &at, &direction);
            else if (path.countPoints()) at = path.getPoint(0);
            else return D2DERR_WRONG_STATE;
        }
        if (point) *point = {at.x(), at.y()};
        if (tangent) *tangent = {direction.x(), direction.y()};
        return S_OK;
    }

protected:
    void set(SkPath outline, SkPath area) { outline_ = std::move(outline); area_ = std::move(area); }
private:
    SkPath outline_, area_;
};

// The result's outline as closed figures of lines and Béziers (conics become cubics through quadratics).
void emit(const SkPath& path, ID2D1SimplifiedGeometrySink* sink) {
    sink->SetFillMode(path.getFillType() == SkPathFillType::kWinding ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE);
    SkPath::Iter it(path, false);
    SkPoint points[4];
    bool open = false;
    SkPoint current{};
    const auto quadratic = [&](SkPoint control, SkPoint end) {
        const D2D1_BEZIER_SEGMENT cubic{{current.x() + (control.x() - current.x()) * 2 / 3, current.y() + (control.y() - current.y()) * 2 / 3},
                                        {end.x() + (control.x() - end.x()) * 2 / 3, end.y() + (control.y() - end.y()) * 2 / 3},
                                        {end.x(), end.y()}};
        sink->AddBeziers(&cubic, 1);
        current = end;
    };
    for (SkPath::Verb verb; (verb = it.next(points)) != SkPath::kDone_Verb;) {
        switch (verb) {
        case SkPath::kMove_Verb:
            if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->BeginFigure({points[0].x(), points[0].y()}, D2D1_FIGURE_BEGIN_FILLED);
            current = points[0];
            open = true;
            break;
        case SkPath::kLine_Verb: {
            const D2D1_POINT_2F end{points[1].x(), points[1].y()};
            sink->AddLines(&end, 1);
            current = points[1];
            break;
        }
        case SkPath::kQuad_Verb: quadratic(points[1], points[2]); break;
        case SkPath::kConic_Verb: {
            SkPoint quads[1 + 2 * 4];
            const int count = SkPath::ConvertConicToQuads(points[0], points[1], points[2], it.conicWeight(), quads, 2);
            for (int i = 0; i < count; ++i) quadratic(quads[1 + i * 2], quads[2 + i * 2]);
            break;
        }
        case SkPath::kCubic_Verb: {
            const D2D1_BEZIER_SEGMENT cubic{{points[1].x(), points[1].y()}, {points[2].x(), points[2].y()}, {points[3].x(), points[3].y()}};
            sink->AddBeziers(&cubic, 1);
            current = points[3];
            break;
        }
        case SkPath::kClose_Verb:
            if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            open = false;
            break;
        default: break;
        }
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

template <class Interface>
HRESULT STDMETHODCALLTYPE GeometryObject<Interface>::CombineWithGeometry(ID2D1Geometry* input, D2D1_COMBINE_MODE mode,
                                                                         const D2D1_MATRIX_3X2_F* transform, FLOAT,
                                                                         ID2D1SimplifiedGeometrySink* sink) const {
    const auto* other = Geometry::from(input);
    if (!other || !sink) return E_INVALIDARG;
    SkPathOp op = kUnion_SkPathOp;
    switch (mode) {
    case D2D1_COMBINE_MODE_INTERSECT: op = kIntersect_SkPathOp; break;
    case D2D1_COMBINE_MODE_XOR: op = kXOR_SkPathOp; break;
    case D2D1_COMBINE_MODE_EXCLUDE: op = kDifference_SkPathOp; break;
    default: break;
    }
    const auto result = Op(area_, transform ? other->area().makeTransform(toSk(*transform)) : other->area(), op);
    if (!result) return E_FAIL;
    emit(*result, sink);
    return S_OK;
}

class RectangleGeometry final : public GeometryObject<ID2D1RectangleGeometry> {
public:
    explicit RectangleGeometry(const D2D1_RECT_F& rect) { SkPath path = rectanglePath(rect); set(path, path); }
};

class RoundedRectangleGeometry final : public GeometryObject<ID2D1RoundedRectangleGeometry> {
public:
    explicit RoundedRectangleGeometry(const D2D1_ROUNDED_RECT& rounded) { SkPath path = roundedRectanglePath(rounded); set(path, path); }
};

class EllipseGeometry final : public GeometryObject<ID2D1EllipseGeometry> {
public:
    explicit EllipseGeometry(const D2D1_ELLIPSE& ellipse) { SkPath path = ellipsePath(ellipse); set(path, path); }
};

// The children's figures together, filled by the group's own fill mode.
class GeometryGroup final : public GeometryObject<ID2D1GeometryGroup> {
public:
    GeometryGroup(D2D1_FILL_MODE mode, std::vector<ComPtr<ID2D1Geometry>> children) : children_(std::move(children)) {
        SkPathBuilder outline, area(fillTypeFor(mode));
        for (const auto& child : children_)
            if (const auto* geometry = Geometry::from(child.Get())) {
                outline.addPath(geometry->outline());
                area.addPath(geometry->area());
            }
        set(outline.detach(), area.detach());
    }
private:
    std::vector<ComPtr<ID2D1Geometry>> children_;
};

// The source's points mapped by the transform. Strokes are not widened by it: the figures are
// already transformed when stroked in the render target's world space.
class TransformedGeometry final : public GeometryObject<ID2D1TransformedGeometry> {
public:
    TransformedGeometry(ComPtr<ID2D1Geometry> source, const D2D1_MATRIX_3X2_F& transform) : source_(std::move(source)) {
        const auto* geometry = Geometry::from(source_.Get());
        const SkMatrix m = toSk(transform);
        if (geometry) set(geometry->outline().makeTransform(m), geometry->area().makeTransform(m));
    }
private:
    ComPtr<ID2D1Geometry> source_;
};

class GeometrySink;

class PathGeometry final : public GeometryObject<ID2D1PathGeometry> {
public:
    HRESULT STDMETHODCALLTYPE Open(ID2D1GeometrySink** sink) override;
private:
    friend class GeometrySink;
    bool opened_ = false;
};

// Writes into its path geometry at Close. Segments outside BeginFigure/EndFigure are dropped.
class GeometrySink final : public lm_com::Object<ID2D1GeometrySink> {
public:
    explicit GeometrySink(ComPtr<PathGeometry> path) : path_(std::move(path)) {}
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode) override { if (!closed_) area_.setFillType(fillTypeFor(mode)); }
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin) override {
        if (closed_) return;
        if (open_) EndFigure(D2D1_FIGURE_END_OPEN);
        filled_ = begin == D2D1_FIGURE_BEGIN_FILLED;
        outline_.moveTo(start.x, start.y);
        if (filled_) area_.moveTo(start.x, start.y);
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
        if (!open_ || closed_) return;
        if (end == D2D1_FIGURE_END_CLOSED) {
            outline_.close();
            if (filled_) area_.close();
        }
        open_ = false;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        if (closed_) return D2DERR_WRONG_STATE;
        const bool balanced = !open_;
        closed_ = true;
        path_->set(outline_.detach(), area_.detach());
        return balanced ? S_OK : D2DERR_WRONG_STATE;
    }
    void STDMETHODCALLTYPE AddLine(D2D1_POINT_2F point) override {
        if (!open_ || closed_) return;
        outline_.lineTo(point.x, point.y);
        if (filled_) area_.lineTo(point.x, point.y);
        current_ = point;
    }
    void STDMETHODCALLTYPE AddBezier(const D2D1_BEZIER_SEGMENT* b) override {
        if (!b || !open_ || closed_) return;
        outline_.cubicTo(b->point1.x, b->point1.y, b->point2.x, b->point2.y, b->point3.x, b->point3.y);
        if (filled_) area_.cubicTo(b->point1.x, b->point1.y, b->point2.x, b->point2.y, b->point3.x, b->point3.y);
        current_ = b->point3;
    }
    void STDMETHODCALLTYPE AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT* q) override {
        if (!q || !open_ || closed_) return;
        outline_.quadTo(q->point1.x, q->point1.y, q->point2.x, q->point2.y);
        if (filled_) area_.quadTo(q->point1.x, q->point1.y, q->point2.x, q->point2.y);
        current_ = q->point2;
    }
    void STDMETHODCALLTYPE AddQuadraticBeziers(const D2D1_QUADRATIC_BEZIER_SEGMENT* beziers, UINT32 count) override {
        for (UINT32 i = 0; beziers && i < count; ++i) AddQuadraticBezier(&beziers[i]);
    }
    // SVG's elliptical arc: Direct2D's clockwise is SVG's positive sweep in y-down space.
    void STDMETHODCALLTYPE AddArc(const D2D1_ARC_SEGMENT* arc) override {
        if (!arc || !open_ || closed_) return;
        const D2D1_POINT_2F to = arc->point;
        if (to.x == current_.x && to.y == current_.y) return;
        const SkPoint radii{std::abs(arc->size.width), std::abs(arc->size.height)};
        const auto size = arc->arcSize == D2D1_ARC_SIZE_LARGE ? SkPathBuilder::kLarge_ArcSize : SkPathBuilder::kSmall_ArcSize;
        const auto sweep = arc->sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE ? SkPathDirection::kCW : SkPathDirection::kCCW;
        if (radii.x() == 0 || radii.y() == 0) { AddLine(to); return; }
        outline_.arcTo(radii, arc->rotationAngle, size, sweep, {to.x, to.y});
        if (filled_) area_.arcTo(radii, arc->rotationAngle, size, sweep, {to.x, to.y});
        current_ = to;
    }
private:
    ComPtr<PathGeometry> path_;
    SkPathBuilder outline_, area_{SkPathFillType::kEvenOdd};
    D2D1_POINT_2F current_{};
    bool open_ = false, filled_ = false, closed_ = false;
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

// Radii clamped to half the sides, each on its own (as Direct2D does; Skia would scale both).
SkPath roundedRectanglePath(const D2D1_ROUNDED_RECT& rounded) {
    const SkRect rect = SkRect::MakeLTRB(rounded.rect.left, rounded.rect.top, rounded.rect.right, rounded.rect.bottom).makeSorted();
    const float rx = std::min(std::abs(rounded.radiusX), rect.width() * 0.5f), ry = std::min(std::abs(rounded.radiusY), rect.height() * 0.5f);
    if (rx <= 0 || ry <= 0) return rectanglePath({rect.left(), rect.top(), rect.right(), rect.bottom()});
    return SkPath::RRect(SkRRect::MakeRectXY(rect, rx, ry));
}

SkPath ellipsePath(const D2D1_ELLIPSE& e) {
    const float rx = std::abs(e.radiusX), ry = std::abs(e.radiusY);
    return SkPath::Oval(SkRect::MakeLTRB(e.point.x - rx, e.point.y - ry, e.point.x + rx, e.point.y + ry));
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
