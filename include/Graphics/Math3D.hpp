#pragma once

#include <cmath>
#include <cstdint>

namespace Neleo {

struct Point3D {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    Point3D operator-(const Point3D& o) const {
        return Point3D{x - o.x, y - o.y, z - o.z};
    }

    Point3D operator+(const Point3D& o) const {
        return Point3D{x + o.x, y + o.y, z + o.z};
    }

    Point3D operator*(float s) const {
        return Point3D{x * s, y * s, z * s};
    }

    float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }
    float length() const { return Length(); }

    /**
     * @brief Reciprocal square root. With -ffast-math on devkitPPC,
     * 1.0f / std::sqrt(x) is lowered directly to PowerPC frsqrte hardware instruction.
     */
    static inline float FastInvSqrt(float number) {
        return 1.0f / std::sqrt(number);
    }
    static inline float fastInvSqrt(float number) { return FastInvSqrt(number); }

    Point3D Normalized() const {
        const float lenSq = x * x + y * y + z * z;
        if (lenSq < 0.00001f) return Point3D{0.0f, 0.0f, 0.0f};
        const float invLen = FastInvSqrt(lenSq);
        return Point3D{x * invLen, y * invLen, z * invLen};
    }
    Point3D normalized() const { return Normalized(); }

    static float Dot(const Point3D& a, const Point3D& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    static float dot(const Point3D& a, const Point3D& b) { return Dot(a, b); }

    static Point3D Cross(const Point3D& a, const Point3D& b) {
        return Point3D{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }
    static Point3D cross(const Point3D& a, const Point3D& b) { return Cross(a, b); }
};

struct Point2D {
    float x{0.0f};
    float y{0.0f};
    float depthZ{0.0f};
};

/**
 * @brief [OPT-330] Cuaternión para Rotaciones 3D e Interpolación Esquelética en SO(3).
 * 
 * Basado en Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 4.4 & 11).
 * Implementa Slerp y Nlerp analítico para animación continua de keyframes.
 */
struct Quaternion {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};

    constexpr Quaternion() noexcept = default;
    constexpr Quaternion(float qx, float qy, float qz, float qw) noexcept
        : x(qx), y(qy), z(qz), w(qw) {}

    static constexpr Quaternion Identity() noexcept {
        return Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
    }

    static Quaternion FromAxisAngle(const Point3D& axis, float angleRad) noexcept {
        float halfAngle = angleRad * 0.5f;
        float s = std::sin(halfAngle);
        Point3D normAxis = axis.Normalized();
        return Quaternion{
            normAxis.x * s,
            normAxis.y * s,
            normAxis.z * s,
            std::cos(halfAngle)
        };
    }

    static Quaternion FromEuler(float pitchRad, float yawRad, float rollRad) noexcept {
        float cp = std::cos(pitchRad * 0.5f);
        float sp = std::sin(pitchRad * 0.5f);
        float cy = std::cos(yawRad * 0.5f);
        float sy = std::sin(yawRad * 0.5f);
        float cr = std::cos(rollRad * 0.5f);
        float sr = std::sin(rollRad * 0.5f);

        return Quaternion{
            sp * cy * cr - cp * sy * sr,
            cp * sy * cr + sp * cy * sr,
            cp * cy * sr - sp * sy * cr,
            cp * cy * cr + sp * sy * sr
        };
    }

    [[nodiscard]] float LengthSq() const noexcept {
        return x * x + y * y + z * z + w * w;
    }

    [[nodiscard]] float Length() const noexcept {
        return std::sqrt(LengthSq());
    }

    [[nodiscard]] Quaternion Normalized() const noexcept {
        float lenSq = LengthSq();
        if (lenSq < 1e-6f) return Identity();
        float invLen = Point3D::FastInvSqrt(lenSq);
        return Quaternion{x * invLen, y * invLen, z * invLen, w * invLen};
    }

    [[nodiscard]] Quaternion Conjugate() const noexcept {
        return Quaternion{-x, -y, -z, w};
    }

    [[nodiscard]] Quaternion operator*(const Quaternion& rhs) const noexcept {
        return Quaternion{
            w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
            w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
            w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
            w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z
        };
    }

    [[nodiscard]] Point3D Rotate(const Point3D& v) const noexcept {
        Point3D r{x, y, z};
        Point3D t = Point3D::Cross(r, v) * 2.0f;
        return v + (t * w) + Point3D::Cross(r, t);
    }

    static float Dot(const Quaternion& a, const Quaternion& b) noexcept {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    static Quaternion Nlerp(const Quaternion& a, const Quaternion& b, float t) noexcept {
        float cosTheta = Dot(a, b);
        float sign = (cosTheta < 0.0f) ? -1.0f : 1.0f;
        Quaternion res{
            a.x + (b.x * sign - a.x) * t,
            a.y + (b.y * sign - a.y) * t,
            a.z + (b.z * sign - a.z) * t,
            a.w + (b.w * sign - a.w) * t
        };
        return res.Normalized();
    }

    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) noexcept {
        float cosTheta = Dot(a, b);
        float sign = 1.0f;
        if (cosTheta < 0.0f) {
            cosTheta = -cosTheta;
            sign = -1.0f;
        }

        if (cosTheta > 0.9995f) {
            return Nlerp(a, b, t);
        }

        float theta = std::acos(cosTheta);
        float sinTheta = std::sin(theta);
        float wA = std::sin((1.0f - t) * theta) / sinTheta;
        float wB = std::sin(t * theta) / sinTheta * sign;

        return Quaternion{
            a.x * wA + b.x * wB,
            a.y * wA + b.y * wB,
            a.z * wA + b.z * wB,
            a.w * wA + b.w * wB
        }.Normalized();
    }

    /**
     * @brief Shortest-arc quaternion rotating direction `fromDir` onto `toDir`.
     * Ref: Gregory, Game Engine Architecture (3rd Ed.), Cap. 4.4 & 12.5.
     */
    static Quaternion FromToRotation(const Point3D& fromDir, const Point3D& toDir) noexcept {
        const float fromLenSq = Point3D::Dot(fromDir, fromDir);
        const float toLenSq = Point3D::Dot(toDir, toDir);
        if (fromLenSq < 1e-6f || toLenSq < 1e-6f) {
            return Identity();
        }

        const Point3D u = fromDir * Point3D::FastInvSqrt(fromLenSq);
        const Point3D v = toDir * Point3D::FastInvSqrt(toLenSq);
        const float d = Point3D::Dot(u, v);

        if (d >= 0.99995f) {
            return Identity();
        }

        if (d <= -0.99995f) {
            Point3D axis = Point3D::Cross(Point3D{1.0f, 0.0f, 0.0f}, u);
            if (Point3D::Dot(axis, axis) < 1e-4f) {
                axis = Point3D::Cross(Point3D{0.0f, 1.0f, 0.0f}, u);
            }
            const Point3D normAxis = axis.Normalized();
            return Quaternion{normAxis.x, normAxis.y, normAxis.z, 0.0f};
        }

        const float s = std::sqrt((1.0f + d) * 2.0f);
        const float invS = 1.0f / s;
        const Point3D c = Point3D::Cross(u, v);
        return Quaternion{c.x * invS, c.y * invS, c.z * invS, s * 0.5f}.Normalized();
    }
};

/**
 * @brief Precomputed 3D Euler rotation matrix for fast vertex transformations.
 */
struct RotationMatrix {
    float cx, sx;
    float cy, sy;
    float cz, sz;

    static RotationMatrix FromEuler(float rotX, float rotY, float rotZ) {
        constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;
        const float radX = rotX * DEG_TO_RAD;
        const float radY = rotY * DEG_TO_RAD;
        const float radZ = rotZ * DEG_TO_RAD;
        return RotationMatrix{
            std::cos(radX), std::sin(radX),
            std::cos(radY), std::sin(radY),
            std::cos(radZ), std::sin(radZ)
        };
    }

    Point3D Transform(const Point3D& p) const {
        const float y1 = p.y * cx - p.z * sx;
        const float z1 = p.y * sx + p.z * cx;

        const float x2 = p.x * cy + z1 * sy;
        const float z2 = -p.x * sy + z1 * cy;

        const float x3 = x2 * cz - y1 * sz;
        const float y3 = x2 * sz + y1 * cz;

        return Point3D{x3, y3, z2};
    }
    Point3D transform(const Point3D& p) const { return Transform(p); }
};

inline Point2D ProjectTransformed3D(const Point3D& transformedP, float zOffset, 
                                    float centerX, float centerY, float scaleFactor) {
    float finalZ = transformedP.z + zOffset;
    if (finalZ < 0.1f) finalZ = 0.1f;

    const float invZ = 1.0f / finalZ;
    const float screenX = centerX + (transformedP.x * invZ) * scaleFactor;
    const float screenY = centerY - (transformedP.y * invZ) * scaleFactor;

    return Point2D{screenX, screenY, finalZ};
}
inline Point2D projectTransformed3D(const Point3D& transformedP, float zOffset, 
                                    float centerX, float centerY, float scaleFactor) {
    return ProjectTransformed3D(transformedP, zOffset, centerX, centerY, scaleFactor);
}

inline Point2D Project3D(const Point3D& p, float rotX, float rotY, float rotZ, 
                         float zOffset, float centerX, float centerY, float scaleFactor) {
    const RotationMatrix rot = RotationMatrix::FromEuler(rotX, rotY, rotZ);
    const Point3D t = rot.Transform(p);
    return ProjectTransformed3D(t, zOffset, centerX, centerY, scaleFactor);
}
inline Point2D project3D(const Point3D& p, float rotX, float rotY, float rotZ, 
                         float zOffset, float centerX, float centerY, float scaleFactor) {
    return Project3D(p, rotX, rotY, rotZ, zOffset, centerX, centerY, scaleFactor);
}

/**
 * @brief 4x4 Transformation Matrix with column-major memory layout
 * matching standard GLSL / CafeGLSL uniform registers (KC2[0 .. 3]).
 */
struct Matrix4x4 {
    float m[16]{};

    static constexpr Matrix4x4 Identity() noexcept {
        Matrix4x4 res{};
        res.m[0] = 1.0f; res.m[5] = 1.0f; res.m[10] = 1.0f; res.m[15] = 1.0f;
        return res;
    }

    static Matrix4x4 Translation(float tx, float ty, float tz) noexcept {
        Matrix4x4 res = Identity();
        res.m[12] = tx;
        res.m[13] = ty;
        res.m[14] = tz;
        return res;
    }

    static Matrix4x4 Scale(float sx, float sy, float sz) noexcept {
        Matrix4x4 res{};
        res.m[0] = sx;
        res.m[5] = sy;
        res.m[10] = sz;
        res.m[15] = 1.0f;
        return res;
    }

    static Matrix4x4 RotationX(float rad) noexcept {
        Matrix4x4 res = Identity();
        float c = std::cos(rad);
        float s = std::sin(rad);
        res.m[5]  = c;
        res.m[6]  = s;
        res.m[9]  = -s;
        res.m[10] = c;
        return res;
    }

    static Matrix4x4 RotationY(float rad) noexcept {
        Matrix4x4 res = Identity();
        float c = std::cos(rad);
        float s = std::sin(rad);
        res.m[0]  = c;
        res.m[2]  = -s;
        res.m[8]  = s;
        res.m[10] = c;
        return res;
    }

    static Matrix4x4 RotationZ(float rad) noexcept {
        Matrix4x4 res = Identity();
        float c = std::cos(rad);
        float s = std::sin(rad);
        res.m[0] = c;
        res.m[1] = s;
        res.m[4] = -s;
        res.m[5] = c;
        return res;
    }

    static Matrix4x4 Perspective(float fovYRad, float aspect, float nearZ, float farZ) noexcept {
        Matrix4x4 res{};
        float tanHalfFov = std::tan(fovYRad * 0.5f);
        res.m[0] = 1.0f / (aspect * tanHalfFov);
        res.m[5] = 1.0f / tanHalfFov;
        res.m[10] = -(farZ + nearZ) / (farZ - nearZ);
        res.m[11] = -1.0f;
        res.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        return res;
    }

    static Matrix4x4 Orthographic(float left, float right, float bottom, float top, float nearZ = -1.0f, float farZ = 1.0f) noexcept {
        Matrix4x4 res{};
        res.m[0] = 2.0f / (right - left);
        res.m[5] = 2.0f / (top - bottom);
        res.m[10] = -2.0f / (farZ - nearZ);
        res.m[12] = -(right + left) / (right - left);
        res.m[13] = -(top + bottom) / (top - bottom);
        res.m[14] = -(farZ + nearZ) / (farZ - nearZ);
        res.m[15] = 1.0f;
        return res;
    }

    static Matrix4x4 LookAt(const Point3D& eye, const Point3D& target, const Point3D& up) noexcept {
        Point3D f = (target - eye).Normalized();
        Point3D r = Point3D::Cross(f, up).Normalized();
        Point3D u = Point3D::Cross(r, f);

        Matrix4x4 res{};
        res.m[0] = r.x;
        res.m[4] = r.y;
        res.m[8] = r.z;

        res.m[1] = u.x;
        res.m[5] = u.y;
        res.m[9] = u.z;

        res.m[2] = -f.x;
        res.m[6] = -f.y;
        res.m[10] = -f.z;

        res.m[12] = -Point3D::Dot(r, eye);
        res.m[13] = -Point3D::Dot(u, eye);
        res.m[14] = Point3D::Dot(f, eye);
        res.m[15] = 1.0f;
        return res;
    }

    static Matrix4x4 FromQuaternion(const Quaternion& q) noexcept {
        Matrix4x4 res = Identity();
        float xx = q.x * q.x;
        float yy = q.y * q.y;
        float zz = q.z * q.z;
        float xy = q.x * q.y;
        float xz = q.x * q.z;
        float yz = q.y * q.z;
        float wx = q.w * q.x;
        float wy = q.w * q.y;
        float wz = q.w * q.z;

        res.m[0]  = 1.0f - 2.0f * (yy + zz);
        res.m[1]  = 2.0f * (xy + wz);
        res.m[2]  = 2.0f * (xz - wy);

        res.m[4]  = 2.0f * (xy - wz);
        res.m[5]  = 1.0f - 2.0f * (xx + zz);
        res.m[6]  = 2.0f * (yz + wx);

        res.m[8]  = 2.0f * (xz + wy);
        res.m[9]  = 2.0f * (yz - wx);
        res.m[10] = 1.0f - 2.0f * (xx + yy);

        return res;
    }

    static Matrix4x4 FromTRS(const Point3D& t, const Quaternion& r, const Point3D& s) noexcept {
        Matrix4x4 rot = FromQuaternion(r);
        Matrix4x4 res{};
        res.m[0]  = rot.m[0] * s.x;
        res.m[1]  = rot.m[1] * s.x;
        res.m[2]  = rot.m[2] * s.x;
        res.m[3]  = 0.0f;

        res.m[4]  = rot.m[4] * s.y;
        res.m[5]  = rot.m[5] * s.y;
        res.m[6]  = rot.m[6] * s.y;
        res.m[7]  = 0.0f;

        res.m[8]  = rot.m[8] * s.z;
        res.m[9]  = rot.m[9] * s.z;
        res.m[10] = rot.m[10] * s.z;
        res.m[11] = 0.0f;

        res.m[12] = t.x;
        res.m[13] = t.y;
        res.m[14] = t.z;
        res.m[15] = 1.0f;
        return res;
    }

    static Matrix4x4 InvertAffine(const Matrix4x4& m) noexcept {
        Matrix4x4 inv = Identity();
        inv.m[0] = m.m[0]; inv.m[1] = m.m[4]; inv.m[2] = m.m[8];
        inv.m[4] = m.m[1]; inv.m[5] = m.m[5]; inv.m[6] = m.m[9];
        inv.m[8] = m.m[2]; inv.m[9] = m.m[6]; inv.m[10] = m.m[10];

        inv.m[12] = -(inv.m[0] * m.m[12] + inv.m[4] * m.m[13] + inv.m[8] * m.m[14]);
        inv.m[13] = -(inv.m[1] * m.m[12] + inv.m[5] * m.m[13] + inv.m[9] * m.m[14]);
        inv.m[14] = -(inv.m[2] * m.m[12] + inv.m[6] * m.m[13] + inv.m[10] * m.m[14]);
        inv.m[15] = 1.0f;
        return inv;
    }

    [[nodiscard]] Point3D GetTranslation() const noexcept {
        return Point3D{m[12], m[13], m[14]};
    }

    void SetTranslation(const Point3D& t) noexcept {
        m[12] = t.x;
        m[13] = t.y;
        m[14] = t.z;
    }

    [[nodiscard]] Point3D TransformPoint(const Point3D& p) const noexcept {
        return Point3D{
            m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]
        };
    }

    [[nodiscard]] Point3D TransformDirection(const Point3D& v) const noexcept {
        return Point3D{
            m[0] * v.x + m[4] * v.y + m[8]  * v.z,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z
        };
    }

    /**
     * @brief Extracts the normalized rotation quaternion from the upper 3x3 submatrix.
     */
    [[nodiscard]] Quaternion ToQuaternion() const noexcept {
        Point3D col0{m[0], m[1], m[2]};
        Point3D col1{m[4], m[5], m[6]};
        Point3D col2{m[8], m[9], m[10]};

        const float len0Sq = Point3D::Dot(col0, col0);
        const float len1Sq = Point3D::Dot(col1, col1);
        const float len2Sq = Point3D::Dot(col2, col2);
        if (len0Sq < 1e-8f || len1Sq < 1e-8f || len2Sq < 1e-8f) {
            return Quaternion::Identity();
        }

        col0 = col0 * Point3D::FastInvSqrt(len0Sq);
        col1 = col1 * Point3D::FastInvSqrt(len1Sq);
        col2 = col2 * Point3D::FastInvSqrt(len2Sq);

        // Column-major elements: m00=col0.x, m10=col0.y, m20=col0.z, etc.
        const float m00 = col0.x, m10 = col0.y, m20 = col0.z;
        const float m01 = col1.x, m11 = col1.y, m21 = col1.z;
        const float m02 = col2.x, m12 = col2.y, m22 = col2.z;

        const float trace = m00 + m11 + m22;
        Quaternion q{};
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            const float invS = 1.0f / s;
            q.w = 0.25f * s;
            q.x = (m21 - m12) * invS;
            q.y = (m02 - m20) * invS;
            q.z = (m10 - m01) * invS;
        } else if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            const float invS = 1.0f / s;
            q.w = (m21 - m12) * invS;
            q.x = 0.25f * s;
            q.y = (m01 + m10) * invS;
            q.z = (m02 + m20) * invS;
        } else if (m11 > m22) {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            const float invS = 1.0f / s;
            q.w = (m02 - m20) * invS;
            q.x = (m01 + m10) * invS;
            q.y = 0.25f * s;
            q.z = (m12 + m21) * invS;
        } else {
            const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            const float invS = 1.0f / s;
            q.w = (m10 - m01) * invS;
            q.x = (m02 + m20) * invS;
            q.y = (m12 + m21) * invS;
            q.z = 0.25f * s;
        }
        return q.Normalized();
    }

    Matrix4x4 operator*(const Matrix4x4& rhs) const noexcept {
        Matrix4x4 out{};
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                out.m[c * 4 + r] =
                    m[0 * 4 + r] * rhs.m[c * 4 + 0] +
                    m[1 * 4 + r] * rhs.m[c * 4 + 1] +
                    m[2 * 4 + r] * rhs.m[c * 4 + 2] +
                    m[3 * 4 + r] * rhs.m[c * 4 + 3];
            }
        }
        return out;
    }
};

} // namespace Neleo
