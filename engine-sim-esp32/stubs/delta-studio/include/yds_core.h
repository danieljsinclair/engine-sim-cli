// Stub yds_core.h for ESP32 — delta-studio rendering engine not available
// Provides minimal type stubs needed by engine-sim headers to compile
#pragma once

#include <cmath>
#include <cstddef>

// Forward-declare all ys types used by engine-sim headers
struct ysVector {
    float x = 0, y = 0, z = 0, w = 0;
    ysVector() = default;
};

struct ysMatrix {
    float m[4][4] = {};
};

struct ysInputLayout {};
struct ysGPUBuffer {};
struct ysShaderProgram {};
struct ysRenderTarget {};
struct ysAudioBuffer {};
struct ysAudioSource {};

enum class ysError { None };

class ysContextObject {};

namespace ysMath {
    inline ysVector LoadVector(float x, float y = 0, float z = 0, float w = 0) {
        ysVector v; v.x = x; v.y = y; v.z = z; v.w = w; return v;
    }
    inline ysVector Add(const ysVector& a, const ysVector& b) {
        ysVector v; v.x = a.x+b.x; v.y = a.y+b.y; v.z = a.z+b.z; v.w = a.w+b.w; return v;
    }
    inline ysVector Sub(const ysVector& a, const ysVector& b) {
        ysVector v; v.x = a.x-b.x; v.y = a.y-b.y; v.z = a.z-b.z; v.w = a.w-b.w; return v;
    }
    inline ysVector Mul(const ysVector& a, const ysVector& b) {
        ysVector v; v.x = a.x*b.x; v.y = a.y*b.y; v.z = a.z*b.z; v.w = a.w*b.w; return v;
    }
    inline ysVector Scale(const ysVector& a, float s) {
        ysVector v; v.x = a.x*s; v.y = a.y*s; v.z = a.z*s; v.w = a.w*s; return v;
    }
    inline float GetScalar(const ysVector& v) { return v.x; }
}
