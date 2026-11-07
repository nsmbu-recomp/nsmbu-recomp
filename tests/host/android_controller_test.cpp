#include "platform/android_controller.h"
#include <cassert>
#include <string>

int main() {
    const std::string old = "03000000,name,a:b0,b:b1,back:b4,start:b6,leftx:a0,lefty:a1,face:abxy,";
    assert(android_controller::mapping(0x045e, 0x111, old) == old);
    assert(android_controller::mapping(0x2020, 0x112, old) == old);
    const auto fixed = android_controller::mapping(0x2020, 0x0111, old);
    assert(fixed.find("a:b1,b:b0,x:b3,y:b2,face:bayx,") != std::string::npos);
    assert(fixed.find("back:b4,start:b6,leftx:a0,lefty:a1,") != std::string::npos);
    assert(fixed.find("a:b0,") == std::string::npos);
    assert(android_controller::mapping(0x2020, 0x0111, fixed) == fixed);
    const std::string bare = "03000000,name,a:b0,b:b1,face:abxy,";
    const auto axes = android_controller::mapping(0x2020, 0x0112, bare);
    assert(axes.find("leftx:a0,lefty:a1,rightx:a2,righty:a3,") != std::string::npos);
    return 0;
}
