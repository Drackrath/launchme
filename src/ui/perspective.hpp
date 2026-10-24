#pragma once
// Flat pictures in 3D for Direct2D 1.1's perspective DrawBitmap (ID2D1DeviceContext): the
// screensaver's turning game boxes and Półka's Cover Flow. Pixels; y points down, like the screen,
// and z into it (away from the viewer).
#include <d2d1_1.h>
#include <cmath>

namespace perspective {
struct Vec3 { float x = 0, y = 0, z = 0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 normalized(Vec3 v) { const float length = std::sqrt(dot(v, v)); return length > 0 ? v * (1 / length) : Vec3{0, 0, 1}; }
// Around the vertical axis: a positive angle brings the +x side towards the viewer.
inline Vec3 turned(Vec3 p, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}

// Pinhole camera: the eye is `eye` pixels in front of the plane z = 0, whose origin projects to (cx, cy).
struct Camera {
    float cx, cy, eye;
    D2D1_POINT_2F project(Vec3 p) const { const float w = 1 + p.z / eye; return {cx + p.x / w, cy + p.y / w}; }
};

// A flat side: one corner and the two edge vectors (u along the picture's width, v its height).
struct Face {
    Vec3 origin, u, v;
    Vec3 centre() const { return origin + u * 0.5f + v * 0.5f; }
    // Mirror image below the floor plane y = floor (a reflection).
    Face mirrored(float floor) const {
        return {{origin.x, 2 * floor - origin.y, origin.z}, {u.x, -u.y, u.z}, {v.x, -v.y, v.z}};
    }
    Face inset(float fraction_u, float fraction_v) const {
        return {origin + u * fraction_u + v * fraction_v, u * (1 - 2 * fraction_u), v * (1 - 2 * fraction_v)};
    }
    // The corners in drawing order: top left, top right, bottom right, bottom left.
    void corners(const Camera& camera, D2D1_POINT_2F (&out)[4]) const {
        out[0] = camera.project(origin); out[1] = camera.project(origin + u);
        out[2] = camera.project(origin + u + v); out[3] = camera.project(origin + v);
    }
};

// Maps DrawBitmap's destination rectangle (0,0)-(width,height) onto a face in perspective.
// Direct2D multiplies row vectors (x, y, 0, 1) by it and divides by w.
inline D2D1_MATRIX_4X4_F matrix(const Face& face, float width, float height, const Camera& camera) {
    const Vec3 n = normalized(cross(face.u, face.v)); // Only keeps the matrix invertible.
    const float to_face[4][4] = {
        {face.u.x / width, face.u.y / width, face.u.z / width, 0},
        {face.v.x / height, face.v.y / height, face.v.z / height, 0},
        {n.x, n.y, n.z, 0},
        {face.origin.x, face.origin.y, face.origin.z, 1}};
    // x' = x + cx (1 + z/eye), y' likewise, w' = 1 + z/eye; z'/w' stays inside (0, 1).
    const float projection[4][4] = {
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {camera.cx / camera.eye, camera.cy / camera.eye, 1 / camera.eye, 1 / camera.eye},
        {camera.cx, camera.cy, 0.5f, 1}};
    D2D1_MATRIX_4X4_F result{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) sum += to_face[row][k] * projection[k][column];
            result.m[row][column] = sum;
        }
    return result;
}

// The part of an image of `size` that fills a face of this aspect (centre crop).
inline D2D1_RECT_F cropTo(D2D1_SIZE_F size, float aspect) {
    D2D1_RECT_F source{0, 0, size.width, size.height};
    if (size.width / size.height > aspect) {
        const float width = size.height * aspect;
        source.left = (size.width - width) * 0.5f; source.right = source.left + width;
    } else {
        const float height = size.width / aspect;
        source.top = (size.height - height) * 0.5f; source.bottom = source.top + height;
    }
    return source;
}
}
