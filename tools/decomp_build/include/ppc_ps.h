#pragma once
#include <math.h>

struct f32x2 {
    float v[2];
    float operator[](int i) const { return v[i]; }
    float& operator[](int i) { return v[i]; }
};

inline f32x2 __PS_ADD(f32x2 a, f32x2 b) { return {{a.v[0] + b.v[0], a.v[1] + b.v[1]}}; }
inline f32x2 __PS_SUB(f32x2 a, f32x2 b) { return {{a.v[0] - b.v[0], a.v[1] - b.v[1]}}; }
inline f32x2 __PS_MUL(f32x2 a, f32x2 b) { return {{a.v[0] * b.v[0], a.v[1] * b.v[1]}}; }
inline f32x2 __PS_SUM0(f32x2 a, f32x2 b, f32x2 c) { return {{a.v[0] + b.v[1], c.v[1]}}; }
inline f32x2 __PS_MULS0F(f32x2 a, float s) { return {{a.v[0] * s, a.v[1] * s}}; }
inline f32x2 __PS_MADDS0F(f32x2 a, float s, f32x2 b) { return {{a.v[0] * s + b.v[0], a.v[1] * s + b.v[1]}}; }

struct Vec { float x, y, z; };
struct Quaternion { float x, y, z, w; };
inline void C_QUATSlerp(const Quaternion* a, const Quaternion* b, Quaternion* o, float t) {
    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->w = a->w + (b->w - a->w) * t;
}
inline void ASM_VECCrossProduct(const Vec* a, const Vec* b, Vec* o) {
    o->x = a->y * b->z - a->z * b->y;
    o->y = a->z * b->x - a->x * b->z;
    o->z = a->x * b->y - a->y * b->x;
}
inline float ASM_VECSquareMag(const Vec* v) { return v->x * v->x + v->y * v->y + v->z * v->z; }
inline float ASM_VECMag(const Vec* v) { return sqrtf(ASM_VECSquareMag(v)); }
inline float ASM_VECSquareDistance(const Vec* a, const Vec* b) {
    Vec d{a->x - b->x, a->y - b->y, a->z - b->z};
    return ASM_VECSquareMag(&d);
}
inline float ASM_VECDistance(const Vec* a, const Vec* b) { return sqrtf(ASM_VECSquareDistance(a, b)); }

inline void ASM_MTXIdentity(float (*m)[4]) {
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) m[r][c] = (r == c) ? 1.f : 0.f;
}
inline void ASM_MTX44Identity(float (*m)[4]) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) m[r][c] = (r == c) ? 1.f : 0.f;
}
inline void ASM_MTXCopy(float (*src)[4], float (*dst)[4]) {
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) dst[r][c] = src[r][c];
}
inline void ASM_MTX44Copy(float (*src)[4], float (*dst)[4]) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) dst[r][c] = src[r][c];
}
inline void ASM_MTXTranspose(float (*src)[4], float (*dst)[4]) {
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) dst[r][c] = src[c][r];
    dst[0][3] = dst[1][3] = dst[2][3] = 0;
}
inline unsigned ASM_MTXInverse(float (*src)[4], float (*dst)[4]) {
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) dst[i][j] = src[j][i];
    for (int i = 0; i < 3; i++)
        dst[i][3] = -(dst[i][0] * src[0][3] + dst[i][1] * src[1][3] + dst[i][2] * src[2][3]);
    return 1;
}
inline void ASM_MTXConcat(float (*a)[4], float (*b)[4], float (*o)[4]) {
    float t[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        t[i][3] = a[i][0] * b[0][3] + a[i][1] * b[1][3] + a[i][2] * b[2][3] + a[i][3];
    }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) o[i][j] = t[i][j];
}
inline void ASM_MTX44Concat(float (*a)[4], float (*b)[4], float (*o)[4]) {
    float t[4][4];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        t[i][j] = 0;
        for (int k = 0; k < 4; k++) t[i][j] += a[i][k] * b[k][j];
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) o[i][j] = t[i][j];
}
inline void ASM_MTXScale(float (*m)[4], float x, float y, float z) {
    ASM_MTXIdentity(m);
    m[0][0] = x; m[1][1] = y; m[2][2] = z;
}
inline void ASM_MTXTrans(float (*m)[4], float x, float y, float z) {
    ASM_MTXIdentity(m);
    m[0][3] = x; m[1][3] = y; m[2][3] = z;
}
inline void ASM_MTXScaleApply(float (*src)[4], float (*dst)[4], float x, float y, float z) {
    float s[3][4];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) s[r][c] = src[r][c];
    float scale[3] = {x, y, z};
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) dst[r][c] = s[r][c] * scale[r];
}
inline void ASM_MTXTransApply(float (*src)[4], float (*dst)[4], float x, float y, float z) {
    float s[3][4];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) s[r][c] = src[r][c];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) dst[r][c] = s[r][c];
    dst[0][3] = s[0][3] + x; dst[1][3] = s[1][3] + y; dst[2][3] = s[2][3] + z;
}
inline void ASM_MTXMultVec(float (*m)[4], const Vec* v, Vec* o) {
    o->x = m[0][0] * v->x + m[0][1] * v->y + m[0][2] * v->z + m[0][3];
    o->y = m[1][0] * v->x + m[1][1] * v->y + m[1][2] * v->z + m[1][3];
    o->z = m[2][0] * v->x + m[2][1] * v->y + m[2][2] * v->z + m[2][3];
}
inline void ASM_MTXMultVecSR(float (*m)[4], const Vec* v, Vec* o) {
    o->x = m[0][0] * v->x + m[0][1] * v->y + m[0][2] * v->z;
    o->y = m[1][0] * v->x + m[1][1] * v->y + m[1][2] * v->z;
    o->z = m[2][0] * v->x + m[2][1] * v->y + m[2][2] * v->z;
}
