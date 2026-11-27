

#pragma once
#include <cmath>
#include <cstdint>

namespace motion {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
};

struct Quat {
    float w = 1, x = 0, y = 0, z = 0;
    Quat() = default;
    Quat(float a, float b, float c, float d) : w(a), x(b), y(c), z(d) {}
    static Quat axis_angle(Vec3 axis, float radians);
    Quat operator*(const Quat& r) const {
        return {w * r.w - x * r.x - y * r.y - z * r.z, w * r.x + x * r.w + y * r.z - z * r.y,
                w * r.y - x * r.z + y * r.w + z * r.x, w * r.z + x * r.y - y * r.x + z * r.w};
    }
    Quat conj() const { return {w, -x, -y, -z}; }
    void normalize();
    Vec3 rotate(Vec3 v) const;
    Vec3 unrotate(Vec3 v) const;
};

struct VpadMotion {
    Vec3 acc{0, -1, 0};
    float acc_magnitude = 1;
    float acc_variation = 0;
    float acc_xy[2] = {1, 0};
    Vec3 gyro;
    Vec3 angle;
    Vec3 dir[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};

void from_sdl(const float gyro[3], const float accel[3], Vec3& gyro_h, Vec3& acc_h);

void from_dsu(const float gyro_deg[3], const float accel_g[3], Vec3& gyro_h, Vec3& acc_h);

enum AxisMode : int {
    kPlayerSpace,
    kYawAxis,
    kRollAxis,
    kAxisModeCount
};
const char* axis_id(int a);
const char* axis_label(int a);
int axis_from_id(const char* id);

struct Aim {
    float yaw = 0, pitch = 0;
    Aim operator+(Aim o) const { return {yaw + o.yaw, pitch + o.pitch}; }
};

struct Tuning {
    static constexpr float kDefaultSensitivity = 0.5f, kMinSensitivity = 0.05f, kMaxSensitivity = 5.0f;
    float sensitivity_x = kDefaultSensitivity, sensitivity_y = kDefaultSensitivity;
    bool invert_x = false, invert_y = false;
    int axis = kPlayerSpace;
    bool operator==(const Tuning&) const = default;
    Aim apply(Aim a) const {
        return {a.yaw * sensitivity_x * (invert_x ? -1.0f : 1.0f), a.pitch * sensitivity_y * (invert_y ? -1.0f : 1.0f)};
    }
};

class Fusion {
public:
    void recalibrate();

    void update(float dt, Vec3 gyro_h, Vec3 acc_h, int axis_mode);
    Aim take();
    Vec3 bias() const { return bias_; }
    Vec3 gravity() const { return grav_; }
    float rate() const { return rate_; }
    bool calibrated() const { return bias_samples_ >= kBiasSamples; }

    static constexpr float kNoise = 0.008f;
    static constexpr int kBiasSamples = 100;

    static Aim aim_rate(Vec3 w, Vec3 g, int axis_mode);

private:
    Vec3 bias_;
    int bias_samples_ = 0;
    float still_time_ = 0;
    Vec3 last_acc_h_, last_gyro_h_;
    Vec3 grav_{0, 1, 0};
    bool have_grav_ = false;
    float rate_ = 0;
    Aim aim_;
};

class VirtualPad {
public:
    VirtualPad();
    void turn(float dt, Aim a);
    // Roll and pitch are radians from the flat pose. The center is level.
    void set_tilt(float roll, float pitch, float dt);

    VpadMotion vpad();
    Quat orientation() const { return q_; }

private:
    Quat q_;
    Vec3 angle_, angle_read_, last_rate_v_, tilt_prev_;
    double window_ = 0;
};

}
