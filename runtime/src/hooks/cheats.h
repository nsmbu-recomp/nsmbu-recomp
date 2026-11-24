#pragma once

struct Cpu;

namespace cheats {

bool enabled();
void set_enabled(bool on);
bool infinite_lives();
void set_infinite_lives(bool on);
bool invincible();
void set_invincible(bool on);
void request_power(int mode);
int pending_power();
int power();
void before_logic(Cpu* cpu);

}
