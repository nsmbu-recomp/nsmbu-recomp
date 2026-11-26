#pragma once
struct VEC3 { float x, y, z; };
struct VEC4 { float x, y, z, w; };
struct QUAT { float x, y, z, w; };
struct MTX34 {
    union {
        float m[3][4];
        struct {
            float _00, _01, _02, _03;
            float _10, _11, _12, _13;
            float _20, _21, _22, _23;
        } f;
    };
};
struct MTX44 {
    union {
        float m[4][4];
        struct {
            float _00, _01, _02, _03;
            float _10, _11, _12, _13;
            float _20, _21, _22, _23;
            float _30, _31, _32, _33;
        } f;
    };
};
