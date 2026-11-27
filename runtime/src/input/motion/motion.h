

#pragma once
#include <cstdint>
#include <string>

#include "fusion.h"

namespace motion {

enum Source : int { kOff, kController, kCemuhook, kMouse, kSourceCount };
const char* source_id(int s);
const char* source_label(int s);
int source_from_id(const std::string& id);

struct Settings {
    int source = kOff;
    Tuning tuning;
    float mouse_degrees = 0.1f;
    std::string dsu_host = "127.0.0.1";
    int dsu_port = 26760;
    int dsu_slot = 0;

    int recenter_pad = 0;
    int recenter_key = -1;
    bool stick = false;
    bool operator==(const Settings&) const = default;
};

std::string to_ini(const Settings& s);
void from_kv(Settings& s, const std::string& key, const std::string& value);
const char* const kKeys[] = {"gyro.source", "gyro.axis", "gyro.sensitivityX", "gyro.sensitivityY", "gyro.invertX",
                             "gyro.invertY", "gyro.mouseDegrees", "gyro.dsuHost", "gyro.dsuPort", "gyro.dsuSlot",
                             "gyro.recenterPad", "gyro.recenterKey", "gyro.stick"};
std::string value_of(const Settings& s, const std::string& key);

void upgrade_from_first_release(Settings& s);

Settings settings();

void set_settings(const Settings& s);

bool env_override();

void controller_sample(uint64_t device, uint64_t timestamp_ns, const float gyro[3], const float accel[3]);
void controller_gone(uint64_t device);
bool wants_controller_sensors();
void set_gyro_controllers(int n);
int gyro_controllers();

void mouse_motion(float dx, float dy);

bool mouse_drives_gyro();
void set_aiming(bool aiming);
bool aiming();

void recalibrate();

void poll_recalibrate(const float* pad_values, const bool* keys);

VpadMotion vpad(bool repeat);

bool drives_gamepad();

bool stick_drives_tilt();

void right_stick(float x, float y);

std::string status();

}
