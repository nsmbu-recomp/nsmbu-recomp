#include <cstdio>

#include "core/env.h"
#include "input/input_map.h"

int main(int argc, char** argv) {
    env::apply_flags(argc, argv);
    input_map::set_current(input_map::Mapping::defaults(), true);
    return 0;
}
