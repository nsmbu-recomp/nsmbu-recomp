
#include "input_sdl.h"
#include "keycodes.h"
#include "mouse_sdl.h"
#include "input/input.h"
#include "input/input_map.h"
#include "../input/motion/motion.h"
#include "hooks/interp.h"
#include "input/rumble.h"
#include "core/runtime.h"
#include "save/savestate.h"
#include "gfx/screenshot.h"
#include "../gfx/vulkan/settings.h"
#include "agent/agent_ctl.h"
#include "test/script_frame.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"
#include "../overlay/text_entry.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "core/env.h"
#ifdef __ANDROID__
#include <jni.h>
#include "phone_rumble.h"
#include "touch_buttons.h"
#include "android_controller.h"
#include "../overlay/hostui.h"
#endif
namespace render { uint64_t frame_count(); }
namespace gfxvk { bool graphics_hotkey(char key, bool activate); }
namespace gfx { void display_plus_pressed(); }
namespace input {
static std::mutex g_mu;
static bool g_keys[256]={},g_script_keys[256]={};
static PadState g_pads[input::kMaxPlayers];
static float g_values[input_map::kPadCount]={};
static float g_player_values[input::kMaxPlayers][input_map::kPadCount]={};
static bool g_touch=false;static float g_tx=0,g_ty=0;
static std::map<SDL_JoystickID,SDL_Gamepad*> g_controllers;
static std::set<SDL_JoystickID> g_rumble_controllers;
static std::array<SDL_JoystickID,input::kMaxPlayers> g_slot{};
static std::array<std::string,input::kMaxPlayers> g_slot_name;
static std::mutex g_pads_mu;
static SDL_Window* g_prompt_window=nullptr;
static std::function<void(bool,std::u16string)> g_pending,g_done;
static std::u16string g_initial,g_text;
static int g_max_len=0,g_pending_max_len=0;
static std::string g_previous_title;
void set_prompt_window(SDL_Window* window){g_prompt_window=window;}
bool text_prompt_active(){return (bool)g_done;}
static int keycode(SDL_Scancode code) {
 switch(code) {
 case SDL_SCANCODE_A: return kVK_ANSI_A;
 case SDL_SCANCODE_B: return kVK_ANSI_B;
 case SDL_SCANCODE_C: return kVK_ANSI_C;
 case SDL_SCANCODE_D: return kVK_ANSI_D;
 case SDL_SCANCODE_E: return kVK_ANSI_E;
 case SDL_SCANCODE_F: return kVK_ANSI_F;
 case SDL_SCANCODE_G: return kVK_ANSI_G;
 case SDL_SCANCODE_H: return kVK_ANSI_H;
 case SDL_SCANCODE_I: return kVK_ANSI_I;
 case SDL_SCANCODE_J: return kVK_ANSI_J;
 case SDL_SCANCODE_K: return kVK_ANSI_K;
 case SDL_SCANCODE_L: return kVK_ANSI_L;
 case SDL_SCANCODE_M: return kVK_ANSI_M;
 case SDL_SCANCODE_N: return kVK_ANSI_N;
 case SDL_SCANCODE_O: return kVK_ANSI_O;
 case SDL_SCANCODE_P: return kVK_ANSI_P;
 case SDL_SCANCODE_Q: return kVK_ANSI_Q;
 case SDL_SCANCODE_R: return kVK_ANSI_R;
 case SDL_SCANCODE_S: return kVK_ANSI_S;
 case SDL_SCANCODE_T: return kVK_ANSI_T;
 case SDL_SCANCODE_U: return kVK_ANSI_U;
 case SDL_SCANCODE_V: return kVK_ANSI_V;
 case SDL_SCANCODE_W: return kVK_ANSI_W;
 case SDL_SCANCODE_X: return kVK_ANSI_X;
 case SDL_SCANCODE_Y: return kVK_ANSI_Y;
 case SDL_SCANCODE_Z: return kVK_ANSI_Z;
 case SDL_SCANCODE_0: return kVK_ANSI_0;
 case SDL_SCANCODE_1: return kVK_ANSI_1;
 case SDL_SCANCODE_2: return kVK_ANSI_2;
 case SDL_SCANCODE_3: return kVK_ANSI_3;
 case SDL_SCANCODE_4: return kVK_ANSI_4;
 case SDL_SCANCODE_5: return kVK_ANSI_5;
 case SDL_SCANCODE_6: return kVK_ANSI_6;
 case SDL_SCANCODE_7: return kVK_ANSI_7;
 case SDL_SCANCODE_8: return kVK_ANSI_8;
 case SDL_SCANCODE_9: return kVK_ANSI_9;
 case SDL_SCANCODE_MINUS: return kVK_ANSI_Minus;
 case SDL_SCANCODE_EQUALS: return kVK_ANSI_Equal;
 case SDL_SCANCODE_LEFTBRACKET: return kVK_ANSI_LeftBracket;
 case SDL_SCANCODE_RIGHTBRACKET: return kVK_ANSI_RightBracket;
 case SDL_SCANCODE_BACKSLASH: return kVK_ANSI_Backslash;
 case SDL_SCANCODE_SEMICOLON: return kVK_ANSI_Semicolon;
 case SDL_SCANCODE_APOSTROPHE: return kVK_ANSI_Quote;
 case SDL_SCANCODE_COMMA: return kVK_ANSI_Comma;
 case SDL_SCANCODE_PERIOD: return kVK_ANSI_Period;
 case SDL_SCANCODE_SLASH: return kVK_ANSI_Slash;
 case SDL_SCANCODE_GRAVE: return kVK_ANSI_Grave;
 case SDL_SCANCODE_NONUSBACKSLASH: return kVK_ISO_Section;
 case SDL_SCANCODE_SPACE: return kVK_Space;
 case SDL_SCANCODE_RETURN: return kVK_Return;
 case SDL_SCANCODE_TAB: return kVK_Tab;
 case SDL_SCANCODE_BACKSPACE: return kVK_Delete;
 case SDL_SCANCODE_DELETE: return kVK_ForwardDelete;
 case SDL_SCANCODE_ESCAPE: return kVK_Escape;
 case SDL_SCANCODE_LSHIFT: return kVK_Shift;
 case SDL_SCANCODE_RSHIFT: return kVK_RightShift;
 case SDL_SCANCODE_LCTRL: return kVK_Control;
 case SDL_SCANCODE_RCTRL: return kVK_RightControl;
 case SDL_SCANCODE_LALT: return kVK_Option;
 case SDL_SCANCODE_RALT: return kVK_RightOption;
 case SDL_SCANCODE_LGUI: return kVK_Command;
 case SDL_SCANCODE_RGUI: return kVK_RightCommand;
 case SDL_SCANCODE_CAPSLOCK: return kVK_CapsLock;
 case SDL_SCANCODE_UP: return kVK_UpArrow;
 case SDL_SCANCODE_DOWN: return kVK_DownArrow;
 case SDL_SCANCODE_LEFT: return kVK_LeftArrow;
 case SDL_SCANCODE_RIGHT: return kVK_RightArrow;
 case SDL_SCANCODE_HOME: return kVK_Home;
 case SDL_SCANCODE_END: return kVK_End;
 case SDL_SCANCODE_PAGEUP: return kVK_PageUp;
 case SDL_SCANCODE_PAGEDOWN: return kVK_PageDown;
 case SDL_SCANCODE_KP_DECIMAL: return kVK_ANSI_KeypadDecimal;
 case SDL_SCANCODE_KP_PERIOD: return kVK_ANSI_KeypadDecimal;
 case SDL_SCANCODE_KP_MULTIPLY: return kVK_ANSI_KeypadMultiply;
 case SDL_SCANCODE_KP_PLUS: return kVK_ANSI_KeypadPlus;
 case SDL_SCANCODE_KP_MINUS: return kVK_ANSI_KeypadMinus;
 case SDL_SCANCODE_KP_DIVIDE: return kVK_ANSI_KeypadDivide;
 case SDL_SCANCODE_KP_ENTER: return kVK_ANSI_KeypadEnter;
 case SDL_SCANCODE_KP_EQUALS: return kVK_ANSI_KeypadEquals;
 case SDL_SCANCODE_NUMLOCKCLEAR: return kVK_ANSI_KeypadClear;
 case SDL_SCANCODE_F1: return kVK_F1;
 case SDL_SCANCODE_F2: return kVK_F2;
 case SDL_SCANCODE_F3: return kVK_F3;
 case SDL_SCANCODE_F4: return kVK_F4;
 case SDL_SCANCODE_F5: return kVK_F5;
 case SDL_SCANCODE_F6: return kVK_F6;
 case SDL_SCANCODE_F7: return kVK_F7;
 case SDL_SCANCODE_F8: return kVK_F8;
 case SDL_SCANCODE_F9: return kVK_F9;
 case SDL_SCANCODE_F10: return kVK_F10;
 case SDL_SCANCODE_F11: return kVK_F11;
 case SDL_SCANCODE_F12: return kVK_F12;
 case SDL_SCANCODE_F13: return kVK_F13;
 case SDL_SCANCODE_F14: return kVK_F14;
 case SDL_SCANCODE_F15: return kVK_F15;
 case SDL_SCANCODE_F16: return kVK_F16;
 case SDL_SCANCODE_KP_0: return kVK_ANSI_Keypad0;
 case SDL_SCANCODE_KP_1: return kVK_ANSI_Keypad1;
 case SDL_SCANCODE_KP_2: return kVK_ANSI_Keypad2;
 case SDL_SCANCODE_KP_3: return kVK_ANSI_Keypad3;
 case SDL_SCANCODE_KP_4: return kVK_ANSI_Keypad4;
 case SDL_SCANCODE_KP_5: return kVK_ANSI_Keypad5;
 case SDL_SCANCODE_KP_6: return kVK_ANSI_Keypad6;
 case SDL_SCANCODE_KP_7: return kVK_ANSI_Keypad7;
 case SDL_SCANCODE_KP_8: return kVK_ANSI_Keypad8;
 case SDL_SCANCODE_KP_9: return kVK_ANSI_Keypad9;
 default:return -1;
 }
}
void set_touch(bool down,float x,float y){std::lock_guard lk(g_mu);g_touch=down;g_tx=x;g_ty=y;}
void release_keys(){std::lock_guard lk(g_mu);memset(g_keys,0,sizeof g_keys);g_touch=false;}
void held_keys(bool* keys){std::lock_guard lk(g_mu);for(int i=0;i<256;i++)keys[i]=g_keys[i]||g_script_keys[i];}
void controller_values(float* out){std::lock_guard lk(g_mu);std::copy(std::begin(g_values),std::end(g_values),out);}
void controller_values(int player,float* out){std::lock_guard lk(g_mu);player=std::clamp(player,0,input::kMaxPlayers-1);std::copy(std::begin(g_player_values[player]),std::end(g_player_values[player]),out);}
void host_controller_values(float* out){controller_values(out);}
void host_controller_values(int player,float* out){controller_values(player,out);}
bool controller_connected(int player){
 if(player<0||player>=input::kMaxPlayers)return false;
 std::lock_guard lk(g_pads_mu);
 return g_slot[player]!=0;
}
std::string controller_name(int player){
 if(player<0||player>=input::kMaxPlayers)return {};
 std::lock_guard lk(g_pads_mu);
 return g_slot_name[player];
}

static int overlay_mods(SDL_Keymod m){
 return (m&SDL_KMOD_SHIFT?overlay::kShift:0)|(m&SDL_KMOD_CTRL?overlay::kCtrl:0)|(m&SDL_KMOD_ALT?overlay::kAlt:0)|(m&SDL_KMOD_GUI?overlay::kSuper:0);
}
static bool overlay_event(const SDL_Event& event){
 if(!g_prompt_window)return false;
 const SDL_WindowID tv=SDL_GetWindowID(g_prompt_window);
 switch(event.type){
 case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:{
  if(event.key.windowID!=tv)return false;
  int code=keycode(event.key.scancode);
  return code>=0&&overlay::key(code,event.type==SDL_EVENT_KEY_DOWN,event.key.repeat,overlay_mods(event.key.mod));
 }
 case SDL_EVENT_MOUSE_MOTION:{
  if(event.motion.windowID!=tv)return false;
  int w=1,h=1;SDL_GetWindowSize(g_prompt_window,&w,&h);
  return overlay::mouse_move(event.motion.x/std::max(1,w),event.motion.y/std::max(1,h));
 }
 case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:{
  if(event.button.windowID!=tv)return false;
  int b=event.button.button==SDL_BUTTON_LEFT?0:event.button.button==SDL_BUTTON_RIGHT?1:event.button.button==SDL_BUTTON_MIDDLE?2:-1;
  if(b<0)return overlay::captures();
  int w=1,h=1;SDL_GetWindowSize(g_prompt_window,&w,&h);
  overlay::mouse_move(event.button.x/std::max(1,w),event.button.y/std::max(1,h));
  return overlay::mouse_button(b,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN);
 }
 case SDL_EVENT_MOUSE_WHEEL:{
  if(event.wheel.windowID!=tv)return false;
  float y=event.wheel.y,x=event.wheel.x;if(event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED){x=-x;y=-y;}
  return overlay::mouse_wheel(x,y);
 }
 case SDL_EVENT_TEXT_INPUT:{
  if(event.text.windowID!=tv)return false;
  if(!overlay::is_open())return false;
  return overlay::text(event.text.text);
 }
 default:return false;
 }
}
static PadState keyboard_state(bool host){bool keys[256];for(int i=0;i<256;i++)keys[i]=(host&&g_keys[i])||g_script_keys[i];return input_map::keyboard_state(input_map::current(),keys);}

constexpr Uint32 kRumbleRefreshMs=150,kRumbleStallMs=300;
static std::map<SDL_JoystickID,Uint16> g_rumble_sent;
static std::atomic<bool> g_rumble_quit{false};
static std::atomic<Uint64> g_rumble_update_ms{0};
static void rumble_stop_locked(){

 if(!SDL_WasInit(SDL_INIT_GAMEPAD))return;
 for(auto id:g_rumble_controllers){
  auto i=g_controllers.find(id);
  if(i==g_controllers.end()||!SDL_GamepadConnected(i->second))continue;
  SDL_RumbleGamepad(i->second,0,0,0);
  g_rumble_sent[id]=0;
 }
}
#ifdef __ANDROID__
static void phone_rumble_level(Uint16 level){
 static phone_rumble::Refresh refresh;
 if(!refresh.send(level,SDL_GetTicks()))return;
 auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
 if(!env)return;
 jobject activity=static_cast<jobject>(SDL_GetAndroidActivity());
 if(!activity)return;
 if(jclass cls=env->GetObjectClass(activity)){
  if(jmethodID method=env->GetStaticMethodID(cls,"phoneRumble","(I)V"))env->CallStaticVoidMethod(cls,method,(jint)level);
  env->DeleteLocalRef(cls);
 }
 if(env->ExceptionCheck())env->ExceptionClear();
 env->DeleteLocalRef(activity);
}
#endif
static void apply_rumble(){
 g_rumble_update_ms=SDL_GetTicks();
#ifdef __ANDROID__
 const auto mode=phone_rumble::choice.load();
 const bool touch=phone_rumble::touch_selected(mode!=phone_rumble::kOff&&touch_buttons::controls,
                  phone_rumble::last_input_touch.load())&&touch_buttons::visible_now();
 const bool phone=phone_rumble::allowed(mode,touch,!g_rumble_controllers.empty());
 if(!phone)phone_rumble_level(0);
 if((g_rumble_controllers.empty()&&!phone)||g_rumble_quit)return;
#else
 if(g_rumble_controllers.empty()||g_rumble_quit)return;
#endif

 const bool silent=overlay::is_open()||!SDL_GetKeyboardFocus();
 float levels[input::kMaxPlayers];
 for(int i=0;i<input::kMaxPlayers;i++)levels[i]=silent?0.f:rumble::host_level(i,1000000/60);
#ifdef __ANDROID__
 if(phone)phone_rumble_level((Uint16)(std::clamp(levels[0],0.f,1.f)*0xFFFFu));
#endif
 std::lock_guard lk(g_pads_mu);
 if(!SDL_WasInit(SDL_INIT_GAMEPAD))return;
 for(int slot=0;slot<input::kMaxPlayers;slot++){
  const SDL_JoystickID id=g_slot[slot];
  if(!id||!g_rumble_controllers.contains(id))continue;
  auto i=g_controllers.find(id);
  if(i==g_controllers.end()||!SDL_GamepadConnected(i->second))continue;
  const Uint16 level=(Uint16)(std::clamp(levels[slot],0.f,1.f)*0xFFFFu);
  Uint16& sent=g_rumble_sent[id];
  if(level||sent)SDL_RumbleGamepad(i->second,level,level,level?kRumbleRefreshMs:0);
  if(rumble::log_enabled()&&level!=sent)LOG("[rumble] host: player %d controller %u motor %.2f",slot+1,(unsigned)id,level/65535.0);
  sent=level;
 }
}
static void rumble_watchdog(){
 while(!g_rumble_quit){
  SDL_Delay(100);
  if(SDL_GetTicks()-g_rumble_update_ms<kRumbleStallMs)continue;
  std::lock_guard lk(g_pads_mu);
  if(std::any_of(g_rumble_sent.begin(),g_rumble_sent.end(),[](auto& s){return s.second!=0;})){
   LOG("[rumble] the main loop stalls: motors stopped");
   rumble_stop_locked();
  }
 }
}
void stop_rumble_now(){
 g_rumble_quit=true;

 std::unique_lock lk(g_pads_mu,std::try_to_lock);
 if(lk.owns_lock())rumble_stop_locked();
}

static bool rumble_quit_watch(void*,SDL_Event* event){
 if(event->type==SDL_EVENT_QUIT||event->type==SDL_EVENT_TERMINATING||
    (event->type==SDL_EVENT_WINDOW_CLOSE_REQUESTED&&g_prompt_window&&event->window.windowID==SDL_GetWindowID(g_prompt_window)))
  stop_rumble_now();
 return true;
}
bool has_rumble(){return true;}

static bool g_sensors_on=false;
static std::map<SDL_JoystickID,std::array<float,3>> g_accel;

static std::map<SDL_JoystickID,uint64_t> g_sensor_seen,g_sensor_kick;
static bool has_motion(SDL_Gamepad* pad){return SDL_GamepadHasSensor(pad,SDL_SENSOR_GYRO)&&SDL_GamepadHasSensor(pad,SDL_SENSOR_ACCEL);}
static void set_sensors_locked(SDL_Gamepad* pad,bool on){
 if(!has_motion(pad))return;
 const bool g=SDL_SetGamepadSensorEnabled(pad,SDL_SENSOR_GYRO,on),a=SDL_SetGamepadSensorEnabled(pad,SDL_SENSOR_ACCEL,on);
 if(on&&!(g&&a))LOG("[gyro] %s: turning its motion sensors on failed: %s",SDL_GetGamepadName(pad),SDL_GetError());
 if(on)g_sensor_seen[SDL_GetGamepadID(pad)]=SDL_GetTicks();
}
static void update_sensors(){
 const bool want=motion::wants_controller_sensors();
 std::lock_guard lk(g_pads_mu);
 int n=0;for(auto [id,pad]:g_controllers)if(SDL_GamepadConnected(pad)&&has_motion(pad))n++;
 motion::set_gyro_controllers(n);
 static bool focused=true;
 const bool focus=SDL_GetKeyboardFocus()!=nullptr;
 if(focus!=focused){
  focused=focus;
  if(focus)for(auto& [id,t]:g_sensor_seen)t=SDL_GetTicks();
  if(g_sensors_on)LOG("[gyro] the game window %s the focus%s",focus?"has":"lost",focus?"":": SDL pauses controller events until it is back");
 }
 if(want==g_sensors_on){
  if(!want||!focus||env::NO_HOST_INPUT())return;
  const uint64_t now=SDL_GetTicks();
  for(auto [id,pad]:g_controllers){
   if(!SDL_GamepadConnected(pad)||!has_motion(pad))continue;
   uint64_t& seen=g_sensor_seen[id];uint64_t& kick=g_sensor_kick[id];
   if(now-seen<2000||now-kick<5000)continue;
   if(seen>=kick)LOG("[gyro] %s: no motion samples for %.1f s, turning its sensors off and on again (every 5 s until they come)",SDL_GetGamepadName(pad),(now-seen)/1000.0);
   kick=now;
   const uint64_t before=seen;set_sensors_locked(pad,false);set_sensors_locked(pad,true);seen=before;
  }
  return;
 }
 g_sensors_on=want;g_accel.clear();
 for(auto [id,pad]:g_controllers)set_sensors_locked(pad,want);
 if(want)LOG("[gyro] controller motion sensors on (%d controller(s) with a gyro)",n);
}
static void sensor_event(const SDL_GamepadSensorEvent& e){
 if(e.sensor==SDL_SENSOR_ACCEL){g_accel[e.which]={e.data[0],e.data[1],e.data[2]};return;}
 if(e.sensor!=SDL_SENSOR_GYRO)return;
 auto a=g_accel.find(e.which);if(a==g_accel.end())return;
 motion::controller_sample((uint64_t)e.which,e.sensor_timestamp,e.data,a->second.data());
}
static int slot_of_locked(SDL_JoystickID id){
 for(int i=0;i<input::kMaxPlayers;i++)if(g_slot[i]==id)return i;
 return -1;
}
static int free_slot_locked(){
 for(int i=0;i<input::kMaxPlayers;i++)if(!g_slot[i])return i;
 return -1;
}
static void open_controller(SDL_JoystickID id){
#ifdef __ANDROID__
 if(!SDL_IsJoystickVirtual(id))phone_rumble::last_input_touch=false;
#endif
 std::lock_guard lk(g_pads_mu);
 if(g_controllers.contains(id))return;
#ifdef __ANDROID__
 const uint16_t vendor=SDL_GetGamepadVendorForID(id);
 const uint16_t product=SDL_GetGamepadProductForID(id);
 if(android_controller::ayaneo(vendor)) {
  if(char* original=SDL_GetGamepadMappingForID(id)) {
   LOG("[input] AYANEO SDL mapping before correction: %s",original);
   const auto corrected=android_controller::mapping(vendor,product,original);
   if(SDL_AddGamepadMapping(corrected.c_str())>=0)LOG("[input] applied AYANEO %04X:%04X gamepad mapping",(unsigned)vendor,(unsigned)product);
   else LOG("[input] AYANEO mapping correction failed: %s",SDL_GetError());
   SDL_free(original);
  }
 }
#endif
 if(auto* pad=SDL_OpenGamepad(id)){g_controllers[id]=pad;
  const int slot=free_slot_locked();
  if(slot>=0){g_slot[slot]=id;const char* name=SDL_GetGamepadName(pad);g_slot_name[slot]=name?name:"";}
#ifdef __ANDROID__
  LOG("[input] controller connected: %s (id %u, vendor %04X, product %04X, type %d)",SDL_GetGamepadName(pad)?SDL_GetGamepadName(pad):"?",(unsigned)id,
      SDL_GetGamepadVendor(pad),SDL_GetGamepadProduct(pad),(int)SDL_GetGamepadType(pad));
  if(char* map=SDL_GetGamepadMapping(pad)){
   LOG("[input] SDL gamepad mapping: %s",map);
   SDL_free(map);
  }
#endif
  if(SDL_RumbleGamepad(pad,0,0,0))g_rumble_controllers.insert(id);
  if(g_sensors_on)set_sensors_locked(pad,true);}
}
void init(){
 input_map::load_startup();

 if(!env::NO_CONTROLLERS()) {
  if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD)){LOG("[input] SDL gamepad initialization: %s",SDL_GetError());return;}
  int count=0;auto* ids=SDL_GetGamepads(&count);for(int i=0;i<count;i++)open_controller(ids[i]);SDL_free(ids);
  SDL_AddEventWatch(rumble_quit_watch,nullptr);
  atexit(stop_rumble_now);
  std::thread(rumble_watchdog).detach();
 }
}
static std::string utf8(const std::u16string& text){
 std::string out;
 for(size_t i=0;i<text.size();i++){
  uint32_t c=text[i];if(c>=0xD800&&c<=0xDBFF&&i+1<text.size()&&text[i+1]>=0xDC00&&text[i+1]<=0xDFFF)c=0x10000+((c-0xD800)<<10)+(text[++i]-0xDC00);
  if(c<0x80)out.push_back((char)c);
  else if(c<0x800){out.push_back((char)(0xC0|(c>>6)));out.push_back((char)(0x80|(c&63)));}
  else if(c<0x10000){out.push_back((char)(0xE0|(c>>12)));out.push_back((char)(0x80|((c>>6)&63)));out.push_back((char)(0x80|(c&63)));}
  else{out.push_back((char)(0xF0|(c>>18)));out.push_back((char)(0x80|((c>>12)&63)));out.push_back((char)(0x80|((c>>6)&63)));out.push_back((char)(0x80|(c&63)));}
 }return out;
}

static std::vector<std::pair<SDL_Window*,std::string>> g_other_titles;
static void other_windows_text_input(bool on){
 if(on){g_other_titles.clear();int n=0;if(SDL_Window** ws=SDL_GetWindows(&n)){for(int i=0;i<n;i++)if(ws[i]!=g_prompt_window){g_other_titles.push_back({ws[i],SDL_GetWindowTitle(ws[i])});SDL_StartTextInput(ws[i]);}SDL_free(ws);}}
 else{for(auto& [w,t]:g_other_titles){SDL_StopTextInput(w);SDL_SetWindowTitle(w,t.c_str());}g_other_titles.clear();}
}
static void show_prompt(){
 const std::string title="Enter text (Enter confirms, Escape cancels): "+utf8(g_text);
 if(g_prompt_window)SDL_SetWindowTitle(g_prompt_window,title.c_str());
 for(auto& [w,t]:g_other_titles)SDL_SetWindowTitle(w,title.c_str());
}

static void finish_prompt(bool ok){
 auto done=std::exchange(g_done,nullptr);auto text=std::move(g_text);
 if(g_prompt_window){SDL_StopTextInput(g_prompt_window);SDL_SetWindowTitle(g_prompt_window,g_previous_title.c_str());}
 other_windows_text_input(false);
 release_keys();if(done)done(ok,std::move(text));
}

bool touch_from_controller(SDL_TouchID id){
#ifdef __ANDROID__
 static std::map<SDL_TouchID,bool> known;
 if(auto it=known.find(id);it!=known.end())return it->second;
 const char* name=SDL_GetTouchDeviceName(id);bool fromPad=false;
 if(name)for(auto [pid,pad]:g_controllers)if(const char* pn=SDL_GetGamepadName(pad))if(*pn&&!strncmp(name,pn,strlen(pn)))fromPad=true;
 LOG("[input] touch device %s%s",name?name:"?",fromPad?": a controller's, ignored":"");
 known[id]=fromPad;return fromPad;
#else
 (void)id;return false;
#endif
}

static constexpr Uint64 kTestEventTime=4242;
static bool test_event(const SDL_Event& e){return e.common.timestamp==kTestEventTime;}
static void post_test_keys(){
 struct Post{uint64_t frame;SDL_Scancode code;bool down;std::string text;};
 static std::vector<Post> posts=[]{
  std::vector<Post> v;const char* e=env::TEST_POST_KEYS();
  for(const char* p=e;p&&*p;){
   char* end;uint64_t f=strtoull(p,&end,10);if(*end!=':')break;p=end+1;
   size_t len=strcspn(p,",");std::string k(p,len);p+=len+(p[len]==',');
   if(k.rfind("Text=",0)==0){v.push_back({f,SDL_SCANCODE_UNKNOWN,true,k.substr(5)});continue;}
   uint64_t hold=0;if(size_t sl=k.find('/');sl!=std::string::npos){hold=strtoull(k.c_str()+sl+1,nullptr,10);k.resize(sl);}
   int code=input_map::key_from_id(k);SDL_Scancode sc=SDL_SCANCODE_UNKNOWN;
   for(int i=0;i<SDL_SCANCODE_COUNT&&code!=input_map::kNoKey;i++)if(keycode((SDL_Scancode)i)==code){sc=(SDL_Scancode)i;break;}
   if(sc==SDL_SCANCODE_UNKNOWN){LOG("[input] NSMBU_TEST_POST_KEYS: unknown key %s",k.c_str());continue;}
   v.push_back({f,sc,true,{}});v.push_back({f+hold,sc,false,{}});
  }
  std::stable_sort(v.begin(),v.end(),[](const Post& a,const Post& b){return a.frame<b.frame;});
  return v;
 }();
 static size_t next=0;static std::deque<std::string> texts;
 for(;next<posts.size()&&render::frame_count()>=posts[next].frame;next++){
  const Post& p=posts[next];SDL_Event e{};e.common.timestamp=kTestEventTime;
  if(!p.text.empty()){texts.push_back(p.text);e.type=SDL_EVENT_TEXT_INPUT;e.text.windowID=g_prompt_window?SDL_GetWindowID(g_prompt_window):0;e.text.text=texts.back().c_str();}
  else{e.type=p.down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=p.code;e.key.down=p.down;e.key.windowID=g_prompt_window?SDL_GetWindowID(g_prompt_window):0;}
  LOG("[input] test %s %s%s at frame %llu",p.text.empty()?"key":"text",p.text.empty()?SDL_GetScancodeName(p.code):p.text.c_str(),p.text.empty()?(p.down?" down":" up"):"",(unsigned long long)render::frame_count());
  SDL_PushEvent(&e);
 }
}

static bool text_entry_event(const SDL_Event& event){
 if(!text_entry::active()||overlay::is_open())return false;
 if(env::NO_HOST_INPUT()&&!test_event(event))return false;
 switch(event.type){
 case SDL_EVENT_TEXT_INPUT:text_entry::text(event.text.text);return true;
 case SDL_EVENT_TEXT_EDITING:text_entry::preedit(event.edit.text);return true;
 case SDL_EVENT_KEY_DOWN:case SDL_EVENT_KEY_UP:{
  int code=keycode(event.key.scancode);
  return code>=0&&overlay::key(code,event.type==SDL_EVENT_KEY_DOWN,event.key.repeat,overlay_mods(event.key.mod));
 }
 default:return false;
 }
}
void handle_event(const SDL_Event& event){
#ifdef __ANDROID__
 if(event.type==SDL_EVENT_KEY_DOWN||
    (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&!SDL_IsJoystickVirtual(event.gbutton.which))||
    (event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&std::abs(int(event.gaxis.value))>8192&&!SDL_IsJoystickVirtual(event.gaxis.which)))
  phone_rumble::last_input_touch=false;
#endif
 if(overlay::captures())mods::update_mouse();
 else if(mods::handle_mouse_event(event))return;
 if(event.type==SDL_EVENT_GAMEPAD_ADDED&&!env::NO_CONTROLLERS())open_controller(event.gdevice.which);
 if(event.type==SDL_EVENT_GAMEPAD_REMOVED){std::lock_guard lk(g_pads_mu);auto i=g_controllers.find(event.gdevice.which);if(i!=g_controllers.end()){SDL_CloseGamepad(i->second);g_controllers.erase(i);}const int slot=slot_of_locked(event.gdevice.which);if(slot>=0){g_slot[slot]=0;g_slot_name[slot].clear();}g_rumble_controllers.erase(event.gdevice.which);g_rumble_sent.erase(event.gdevice.which);g_accel.erase(event.gdevice.which);g_sensor_seen.erase(event.gdevice.which);g_sensor_kick.erase(event.gdevice.which);motion::controller_gone(event.gdevice.which);}
 if(event.type==SDL_EVENT_GAMEPAD_SENSOR_UPDATE){g_sensor_seen[event.gsensor.which]=SDL_GetTicks();if(!env::NO_HOST_INPUT()&&!overlay::blocks_input())sensor_event(event.gsensor);return;}
 if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST)release_keys();
 if(text_entry_event(event))return;
 if(g_done){
  if(event.type==SDL_EVENT_KEY_DOWN){
   if(event.key.scancode==SDL_SCANCODE_RETURN){finish_prompt(true);return;}
   if(event.key.scancode==SDL_SCANCODE_ESCAPE){finish_prompt(false);return;}
   if(event.key.scancode==SDL_SCANCODE_BACKSPACE&&!g_text.empty()){char16_t c=g_text.back();g_text.pop_back();if(c>=0xDC00&&c<=0xDFFF&&!g_text.empty()&&g_text.back()>=0xD800&&g_text.back()<=0xDBFF)g_text.pop_back();show_prompt();}
  }
  if(event.type==SDL_EVENT_TEXT_INPUT){
   const unsigned char* p=(const unsigned char*)event.text.text;
   while(*p){uint32_t c=*p++;int rest=0;if(c>=0xF0){c&=7;rest=3;}else if(c>=0xE0){c&=15;rest=2;}else if(c>=0xC0){c&=31;rest=1;}while(rest--&&*p)c=(c<<6)|(*p++&63);
    size_t units=c>0xFFFF?2:1;if(g_text.size()+units>(size_t)g_max_len)break;
    if(c>0xFFFF){c-=0x10000;g_text.push_back((char16_t)(0xD800+(c>>10)));g_text.push_back((char16_t)(0xDC00+(c&1023)));}else g_text.push_back((char16_t)c);
   }show_prompt();
  }return;
 }
 if(env::NO_HOST_INPUT()&&!test_event(event))return;
#ifdef __ANDROID__
 if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&event.key.scancode==SDL_SCANCODE_AC_BACK){
  if(overlay::is_open())overlay::key(kVK_Escape,event.type==SDL_EVENT_KEY_DOWN,event.key.repeat,0);
  else if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&!text_entry::active())overlay::set_open(true);
  return;
 }
#endif
 if(overlay_event(event))return;

 if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat){int code=keycode(event.key.scancode);if(code>=0&&screenshot::key_down(code))return;}
 if(test_event(event))return;

 if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP) &&
    g_prompt_window && event.key.windowID==SDL_GetWindowID(g_prompt_window) &&
    event.key.scancode>=SDL_SCANCODE_F1 && event.key.scancode<=SDL_SCANCODE_F5){
  if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat){
   const int slot=int(event.key.scancode-SDL_SCANCODE_F1)+1;
   if(event.key.mod&SDL_KMOD_SHIFT)ss::request_save(slot);
   else ss::request_load(slot);
  }
  return;
 }

 if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP) && g_prompt_window &&
    event.key.windowID==SDL_GetWindowID(g_prompt_window) &&
    !(event.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_ALT|SDL_KMOD_GUI))){
  char action=0;
  switch(event.key.scancode){
   case SDL_SCANCODE_R: action='R';break; case SDL_SCANCODE_O: action='O';break;
   case SDL_SCANCODE_M: action='M';break; case SDL_SCANCODE_N: action='N';break;
   case SDL_SCANCODE_8: action='8';break; case SDL_SCANCODE_6: action='6';break;
   case SDL_SCANCODE_7: action='7';break; case SDL_SCANCODE_T: action='T';break; default:break;
  }
  const bool activate=event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat;
  if(action && gfxvk::graphics_hotkey(action,activate)){if(activate)hostui::graphics_changed();return;}
 }
 #ifdef __ANDROID__

 static const bool keyboardPad=env::ANDROID_KEYBOARD()!=nullptr;
 if(!keyboardPad&&(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP))return;
 #endif

 if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP){int code=keycode(event.key.scancode);if(code>=0){std::lock_guard lk(g_mu);g_keys[code]=event.type==SDL_EVENT_KEY_DOWN&&(g_keys[code]||!event.key.repeat);}}
}
void update(){
 mods::update_mouse();
 apply_rumble();
 update_sensors();
 std::function<void(bool,std::u16string)> cancelled;
 {std::lock_guard lk(g_mu);if(g_pending&&!g_done){g_done=std::exchange(g_pending,nullptr);g_text=std::move(g_initial);g_max_len=g_pending_max_len;memset(g_keys,0,sizeof g_keys);if(!g_prompt_window)g_prompt_window=SDL_GetKeyboardFocus();if(g_prompt_window){g_previous_title=SDL_GetWindowTitle(g_prompt_window);if(SDL_StartTextInput(g_prompt_window)){other_windows_text_input(true);show_prompt();LOG("[input] the game asks for text: type it in a game window (shown in the window title), Enter confirms, Escape cancels; NSMBU_SWKBD_TEXT=<text> answers automatically");}else{LOG("[input] text input unavailable (%s); set NSMBU_SWKBD_TEXT=<text>",SDL_GetError());cancelled=std::exchange(g_done,nullptr);}}else{LOG("[input] text input: no window to type in; set NSMBU_SWKBD_TEXT=<text>");cancelled=std::exchange(g_done,nullptr);}}}
 if(cancelled)cancelled(false,{});

 static bool entry_text=false;
 if(text_entry::active()!=entry_text&&!g_done){
  entry_text=!entry_text;int n=0;
  if(SDL_Window** ws=SDL_GetWindows(&n)){
   for(int i=0;i<n;i++){
    if(!entry_text){SDL_StopTextInput(ws[i]);continue;}
    int w=1,h=1;SDL_GetWindowSize(ws[i],&w,&h);SDL_Rect area{w/2-160,h/2-60,320,30};
    SDL_SetTextInputArea(ws[i],&area,0);SDL_StartTextInput(ws[i]);
   }
   SDL_free(ws);
  }
 }
 static bool overlay_text=false;
 const bool overlay_wants_text=overlay::is_open()&&overlay::wants_text_input()&&!text_entry::active()&&!g_done;
 if(overlay_wants_text!=overlay_text){
  overlay_text=overlay_wants_text;
  if(g_prompt_window){
   if(overlay_text)SDL_StartTextInput(g_prompt_window);
   else if(!text_entry::active()&&!g_done)SDL_StopTextInput(g_prompt_window);
  }
 }
 post_test_keys();
 float v[input_map::kPadCount]={};float pv[input::kMaxPlayers][input_map::kPadCount]={};
 using namespace input_map;
 {
  std::lock_guard lk(g_pads_mu);
  for(auto [id,pad]:g_controllers){
   if(!SDL_GamepadConnected(pad))continue;
   const int slot=slot_of_locked(id);
   if(slot<0)continue;
   auto put=[&](int p,float x){pv[slot][p]=std::max(pv[slot][p],x);v[p]=std::max(v[p],x);};
   auto button=[&](SDL_GamepadButton b,int p){put(p,SDL_GetGamepadButton(pad,b)?1.f:0.f);};
   button(SDL_GAMEPAD_BUTTON_SOUTH,kPadA);button(SDL_GAMEPAD_BUTTON_EAST,kPadB);button(SDL_GAMEPAD_BUTTON_WEST,kPadX);button(SDL_GAMEPAD_BUTTON_NORTH,kPadY);
   button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,kPadLB);button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,kPadRB);
   button(SDL_GAMEPAD_BUTTON_START,kPadMenu);button(SDL_GAMEPAD_BUTTON_BACK,kPadOptions);button(SDL_GAMEPAD_BUTTON_GUIDE,kPadHome);
   button(SDL_GAMEPAD_BUTTON_LEFT_STICK,kPadL3);button(SDL_GAMEPAD_BUTTON_RIGHT_STICK,kPadR3);
   button(SDL_GAMEPAD_BUTTON_DPAD_UP,kPadDUp);button(SDL_GAMEPAD_BUTTON_DPAD_DOWN,kPadDDown);button(SDL_GAMEPAD_BUTTON_DPAD_LEFT,kPadDLeft);button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,kPadDRight);
   put(kPadLT,std::max(0.f,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)/32767.f));put(kPadRT,std::max(0.f,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)/32767.f));
   auto stick=[&](SDL_GamepadAxis ax,SDL_GamepadAxis ay,int up,int down,int left,int right){float x=SDL_GetGamepadAxis(pad,ax)/32768.f,y=SDL_GetGamepadAxis(pad,ay)/32768.f;put(right,std::max(x,0.f));put(left,std::max(-x,0.f));put(up,std::max(-y,0.f));put(down,std::max(y,0.f));};
   stick(SDL_GAMEPAD_AXIS_LEFTX,SDL_GAMEPAD_AXIS_LEFTY,kPadLSUp,kPadLSDown,kPadLSLeft,kPadLSRight);stick(SDL_GAMEPAD_AXIS_RIGHTX,SDL_GAMEPAD_AXIS_RIGHTY,kPadRSUp,kPadRSDown,kPadRSLeft,kPadRSRight);
  }
 }
 PadState states[input::kMaxPlayers];
 for(int i=0;i<input::kMaxPlayers;i++)states[i]=input_map::controller_state(input_map::player(i),pv[i]);

 {static bool plus=false;const bool now=(states[0].buttons&input::kPlus)!=0;if(now&&!plus&&!overlay::blocks_input())gfx::display_plus_pressed();plus=now;}
 std::lock_guard lk(g_mu);std::copy(std::begin(v),std::end(v),g_values);
 for(int i=0;i<input::kMaxPlayers;i++){std::copy(std::begin(pv[i]),std::end(pv[i]),g_player_values[i]);g_pads[i]=states[i];}
 if(!overlay::blocks_input()&&!env::NO_HOST_INPUT()){motion::poll_recalibrate(v,g_keys);screenshot::poll_controller(v);}
}
void prompt_text(const std::u16string& initial,int max_len,std::function<void(bool,std::u16string)> done){std::lock_guard lk(g_mu);g_initial=initial;g_pending_max_len=std::max(0,max_len);if(g_initial.size()>(size_t)g_pending_max_len)g_initial.resize(g_pending_max_len);g_pending=std::move(done);}

struct Press { uint64_t from, to; uint32_t bits; };
static std::vector<Press> scripted() {
    std::vector<Press> v;
    if (const char* e = env::PRESS()) {
        unsigned long long a, b; unsigned bits; int n;
        while (sscanf(e, "%llu-%llu:%x%n", &a, &b, &bits, &n) == 3) {
            v.push_back({a, b, bits});
            e += n;
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

struct KeyPress { uint64_t from, to; std::vector<int> codes; };
static std::vector<KeyPress> scripted_keys() {
    std::vector<KeyPress> v;
    if (const char* e = env::KEYS()) {
        unsigned long long a, b; int n;
        while (sscanf(e, "%llu-%llu:%n", &a, &b, &n) == 2) {
            e += n;
            KeyPress kp{a, b, {}};
            for (;;) {
                size_t len = strcspn(e, "+,");
                int code = input_map::key_from_id(std::string(e, len));
                if (code == input_map::kNoKey) LOG("[input] NSMBU_KEYS: unknown key %.*s", (int)len, e);
                else kp.codes.push_back(code);
                e += len;
                if (*e != '+') break;
                e++;
            }
            v.push_back(kp);
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

struct Stick { uint64_t from, to; float x, y; };
static std::vector<Stick> scripted_stick(env::Var var = env::STICK) {
    std::vector<Stick> v;
    if (const char* e = (var)()) {
        unsigned long long a, b; float x, y; int n;
        while (sscanf(e, "%llu-%llu:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
            v.push_back({a, b, x, y});
            e += n;
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

namespace {
struct TimedStick { double from, to; float x, y; };
struct TimedPress { double from, to; uint32_t bits; };
struct Scenario {
    uint64_t origin = 0;
    std::vector<TimedStick> sticks, rsticks;
    std::vector<TimedPress> presses;
    int mode = -1;
    double mode_at = 0, end = 0;
    Scenario() {
        if (const char* e = env::TEST_ORIGIN()) origin = strtoull(e, nullptr, 10);
        auto timed_sticks = [](const char* e, std::vector<TimedStick>& out) {
            double a, b; float x, y; int n;
            while (e && sscanf(e, "%lf-%lf:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
                out.push_back({a, b, x, y});
                e += n;
                if (*e != ',') break;
                e++;
            }
        };
        timed_sticks(env::TEST_STICK(), sticks);
        timed_sticks(env::TEST_RSTICK(), rsticks);
        if (const char* e = env::TEST_PRESS()) {
            double a, b; unsigned bits; int n;
            while (sscanf(e, "%lf-%lf:%x%n", &a, &b, &bits, &n) == 3) {
                presses.push_back({a, b, bits});
                e += n;
                if (*e != ',') break;
                e++;
            }
        }
        if (const char* e = env::TEST_MODE()) sscanf(e, "%d@%lf", &mode, &mode_at);
        if (const char* e = env::TEST_END()) end = atof(e);
    }
};
}
}
namespace interp {
void set_mode(int m);
uint64_t logic_steps();
bool course_play_active();
bool input_enabled();
bool player_witness(float& x, float& y, float& z);
}
namespace input {
static bool pad_same(const PadState& a, const PadState& b) {
    return a.buttons == b.buttons && a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry
        && a.touch == b.touch && a.tx == b.tx && a.ty == b.ty;
}

static bool g_course_clock_armed = false;
static uint64_t g_course_clock_origin = 0;
static bool g_replay_start_wait = false;
static uint64_t g_replay_start_wait_from = 0;
static uint64_t g_replay_wait_ticks = 0;
static bool g_clock_from_control = false;
static bool g_replay_mode_applied = false;
static uint64_t g_witness_checked = ~0ull;

static bool replay_course_clock_ready();

static void reset_course_input_clock() {
    g_course_clock_armed = false;
    g_course_clock_origin = 0;
    g_replay_start_wait = false;
    g_replay_start_wait_from = 0;
    g_replay_mode_applied = false;
    g_witness_checked = ~0ull;
}

static void trace_clock(uint64_t now, bool armed) {
    static const bool on = env::REPLAY_TRACE() != nullptr;
    if (!on) return;
    static int budget = 1500;
    if (budget <= 0) return;
    budget--;
    float x = 0, y = 0, z = 0;
    const bool have = interp::player_witness(x, y, z);
    if (have)
        LOG("[replay] step %llu armed %d input %d pos %.3f %.3f %.3f", (unsigned long long)now, armed ? 1 : 0,
            interp::input_enabled() ? 1 : 0, x, y, z);
    else
        LOG("[replay] step %llu armed %d input %d pos none", (unsigned long long)now, armed ? 1 : 0,
            interp::input_enabled() ? 1 : 0);
}

static bool course_relative_step(uint64_t& step) {
    const bool active = interp::course_play_active();
    if (!active) {
        g_course_clock_armed = false;
        g_replay_start_wait = false;
        g_witness_checked = ~0ull;
        return false;
    }
    const uint64_t now = interp::logic_steps();
    if (!g_course_clock_armed) {
        trace_clock(now, false);
        if (!replay_course_clock_ready()) return false;
        g_course_clock_armed = true;
        g_course_clock_origin = now;
    } else if (now - g_course_clock_origin < 90) {
        trace_clock(now, true);
    }
    step = now - g_course_clock_origin;
    return true;
}

static std::string replay_course_from_path(const char* path) {
    if (!path || !path[0]) return {};
    const char* base = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    std::string name(base);
    const char* dot = strrchr(name.c_str(), '.');
    if (dot && dot != name.c_str()) name.resize((size_t)(dot - name.c_str()));
    return name;
}

struct ReplayStart {
    bool have = false;
    bool have_pos = false;
    interp::PlayerSnapshot expect{};
};

struct ReplaySample {
    uint64_t step = 0;
    PadState pad{};
    float wx = 0;
    float wy = 0;
    float wz = 0;
    bool witness = false;
};

static const char* const kPlayerModes[] = {
    "small", "normal", "fire", "mini", "propeller", "penguin", "ice", "squirrel", "psquirrel",
};

static bool same_text(const char* a, const char* b) {
    while (*a && *b) {
        const char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + ('a' - 'A')) : *a;
        const char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + ('a' - 'A')) : *b;
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static int player_mode_from_text(const char* text) {
    if (!text || !*text) return -1;
    char* end = nullptr;
    const long value = strtol(text, &end, 10);
    if (end && end != text && !*end) return value >= 0 && value <= 8 ? (int)value : -1;
    for (int i = 0; i < 9; i++)
        if (same_text(text, kPlayerModes[i])) return i;
    return -1;
}

static const char* player_mode_text(int mode) { return mode >= 0 && mode <= 8 ? kPlayerModes[mode] : nullptr; }

static bool hex_text(const char* text) {
    if (!text) return false;
    const bool prefixed = text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    if (prefixed) text += 2;
    const size_t len = strlen(text);
    if (len < (prefixed ? 1u : 2u))
        return !prefixed && len == 1 && text[0] >= '0' && text[0] <= '9';
    for (const char* p = text; *p; ++p) {
        const char c = *p;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static uint32_t action_bit_for(const char* text) {
    for (int a = 0; a < input_map::kActionCount; a++)
        if (same_text(text, input_map::action_id(a))) return input_map::action_bit(a);
    return 0;
}

static bool parse_buttons(const char* text, uint32_t& buttons) {
    buttons = 0;
    if (!text || !*text) return false;
    if (strcmp(text, "-") == 0 || same_text(text, "none")) return true;
    const char* p = text;
    while (*p) {
        const char* end = p;
        while (*end && *end != '+') ++end;
        const std::string token(p, (size_t)(end - p));
        if (token.empty()) return false;
        uint32_t bit = 0;
        if (hex_text(token.c_str())) {
            char* stop = nullptr;
            bit = (uint32_t)strtoul(token.c_str(), &stop, 16);
            if (!stop || *stop) return false;
        } else {
            bit = action_bit_for(token.c_str());
            if (!bit) return false;
        }
        buttons |= bit;
        p = *end ? end + 1 : end;
    }
    return true;
}

static std::string buttons_text(uint32_t buttons) {
    if (!buttons) return "-";
    std::string text;
    uint32_t left = buttons;
    for (int a = 0; a < input_map::kActionCount; a++) {
        const uint32_t bit = input_map::action_bit(a);
        if (!bit || !(left & bit)) continue;
        if (!text.empty()) text += '+';
        text += input_map::action_id(a);
        left &= ~bit;
    }
    if (left) {
        char hex[16];
        snprintf(hex, sizeof hex, "%08X", buttons);
        return hex;
    }
    return text;
}

static bool parse_start_line(const char* p, ReplayStart& start) {
    if (strncmp(p, "start", 5) != 0) return false;
    p += 5;
    int player = 0;
    int32_t lives = 0, player_coins = 0, coins = 0;
    char mode_text[16] = {};
    float x = 0, y = 0, z = 0;
    if (sscanf(p, " player=%d lives=%d player_coins=%d coins=%d mode=%15s pos=%f %f %f", &player, &lives, &player_coins,
            &coins, mode_text, &x, &y, &z) != 8
        && sscanf(p, " player %d lives %d player_coins %d coins %d mode %15s pos %f %f %f", &player, &lives,
            &player_coins, &coins, mode_text, &x, &y, &z) != 8)
        return false;
    const int mode = player_mode_from_text(mode_text);
    if (mode < 0) return false;
    start.have = true;
    start.have_pos = true;
    start.expect.player = player;
    start.expect.lives = lives;
    start.expect.coins = player_coins;
    start.expect.course_coins = coins;
    start.expect.mode = mode;
    start.expect.x = x;
    start.expect.y = y;
    start.expect.z = z;
    return true;
}

static void parse_start_fields(const char* p, ReplayStart& start) {
    if (parse_start_line(p, start)) return;
    if (strncmp(p, "start", 5) != 0) return;
    const char* f = p + 5;
    int value = 0;
    char mode_text[16] = {};
    float x = 0, y = 0, z = 0;
    if (sscanf(f, " player=%d", &value) == 1)
        start.expect.player = value;
    else if (sscanf(f, " lives=%d", &value) == 1)
        start.expect.lives = value;
    else if (sscanf(f, " player_coins=%d", &value) == 1)
        start.expect.coins = value;
    else if (sscanf(f, " coins=%d", &value) == 1)
        start.expect.course_coins = value;
    else if (sscanf(f, " mode=%15s", mode_text) == 1 || sscanf(f, " mode %15s", mode_text) == 1) {
        const int mode = player_mode_from_text(mode_text);
        if (mode < 0) return;
        start.expect.mode = mode;
    } else if (sscanf(f, " pos=%f %f %f", &x, &y, &z) == 3) {
        start.expect.x = x;
        start.expect.y = y;
        start.expect.z = z;
        start.have_pos = true;
    } else {
        return;
    }
    start.have = true;
}

static bool parse_start_field_line(const char* line, ReplayStart& start) {
    int value = 0;
    char mode_text[16] = {};
    float x = 0, y = 0, z = 0;
    if (sscanf(line, "player=%d", &value) == 1)
        start.expect.player = value;
    else if (sscanf(line, "lives=%d", &value) == 1)
        start.expect.lives = value;
    else if (sscanf(line, "player_coins=%d", &value) == 1)
        start.expect.coins = value;
    else if (sscanf(line, "coins=%d", &value) == 1)
        start.expect.course_coins = value;
    else if (sscanf(line, "mode=%15s", mode_text) == 1) {
        const int mode = player_mode_from_text(mode_text);
        if (mode >= 0) start.expect.mode = mode;
    } else if (sscanf(line, "pos=%f %f %f", &x, &y, &z) == 3) {
        start.expect.x = x;
        start.expect.y = y;
        start.expect.z = z;
        start.have_pos = true;
    } else
        return false;
    start.have = true;
    return true;
}

static float pos_gap(float a, float b) { return std::fabs(a - b); }
static bool pos_near(float a, float b) { return pos_gap(a, b) <= 0.05f; }
static constexpr float kReplayStopGap = 4.f;
static bool pos_stops(float a, float b) { return pos_gap(a, b) > kReplayStopGap; }

static FILE* open_record_file(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) { LOG("[input] can't write %s", path); return nullptr; }
    setvbuf(f, nullptr, _IONBF, 0);
    const char* given = env::RECORD_COURSE();
    const std::string course = given && given[0] ? std::string(given) : replay_course_from_path(path);
    fprintf(f, "version=1\n");
    if (!course.empty()) fprintf(f, "course=%s\n", course.c_str());
    fprintf(f, "origin=control\n");
    LOG("[input] recording %s from in-level play", path);
    return f;
}

static void record_pad(const PadState& s) {
    const char* path = env::path(env::RECORD_INPUT);
    if (!path || !path[0] || env::REPLAY_INPUT()) return;
    g_clock_from_control = true;
    uint64_t step = 0;
    if (!course_relative_step(step)) return;
    static FILE* f = nullptr;
    static bool opened = false;
    static bool start_written = false;
    static bool legend_written = false;
    static PadState prev{};
    static bool have = false;
    if (!opened) {
        opened = true;
        f = open_record_file(path);
    }
    if (!f) return;
    if (have && pad_same(prev, s)) return;
    prev = s;
    have = true;
    interp::PlayerSnapshot snap;
    const bool have_snap = interp::player_snapshot(snap, 0);
    if (!start_written && have_snap) {
        fprintf(f, "player=%d\n", snap.player);
        fprintf(f, "lives=%d\n", snap.lives);
        fprintf(f, "player_coins=%d\n", snap.coins);
        fprintf(f, "coins=%d\n", snap.course_coins);
        if (const char* mode = player_mode_text(snap.mode))
            fprintf(f, "mode=%s\n", mode);
        else
            fprintf(f, "mode=%d\n", snap.mode);
        start_written = true;
    }
    if (!legend_written) {
        fprintf(f, "# step buttons lx ly rx ry touch tx ty x y z\n");
        legend_written = true;
    }
    const std::string buttons = buttons_text(s.buttons);
    if (have_snap)
        fprintf(f, "%llu %s %.9g %.9g %.9g %.9g %d %.9g %.9g %.9g %.9g %.9g\n", (unsigned long long)step,
            buttons.c_str(), s.lx, s.ly, s.rx, s.ry, s.touch ? 1 : 0, s.tx, s.ty, snap.x, snap.y, snap.z);
    else
        fprintf(f, "%llu %s %.9g %.9g %.9g %.9g %d %.9g %.9g\n", (unsigned long long)step, buttons.c_str(), s.lx,
            s.ly, s.rx, s.ry, s.touch ? 1 : 0, s.tx, s.ty);
}

static int g_replay_version = 2;
static bool g_replay_modern = false;
static bool g_replay_exit_on_input = true;
static bool g_replay_live = false;
static ReplayStart g_replay_start;
static std::string g_replay_course;
static size_t g_replay_index = 0;
static int g_witness_miss = 0;
static uint64_t g_witness_first = 0;
static bool g_replay_start_checked = false;
static int g_start_miss = 0;

static const std::vector<ReplaySample>& replay_samples() {
    static std::vector<ReplaySample> samples;
    static bool loaded = false;
    if (loaded) return samples;
    const char* path = env::path(env::REPLAY_INPUT);
    if (!path || !path[0]) return samples;
    loaded = true;
    g_replay_start = {};
    g_replay_course.clear();
    g_replay_modern = false;
    g_clock_from_control = false;
    g_replay_exit_on_input = true;
    g_replay_live = false;
    FILE* f = fopen(path, "r");
    if (!f) { LOG("[input] can't read %s", path); return samples; }
    char line[512];
    uint64_t pending_step = 0;
    float pending_x = 0, pending_y = 0, pending_z = 0;
    bool have_pending = false;
    size_t witness_count = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "version=", 8) == 0) {
            g_replay_modern = true;
            g_replay_version = 3;
            continue;
        }
        if (strncmp(line, "clock=", 6) == 0) continue;
        if (strncmp(line, "origin=", 7) == 0) {
            char value[16];
            if (sscanf(line, "origin=%15s", value) == 1) g_clock_from_control = same_text(value, "control");
            continue;
        }
        if (strncmp(line, "exit=", 5) == 0) {
            char value[16];
            if (sscanf(line, "exit=%15s", value) == 1) g_replay_exit_on_input = strcmp(value, "none") != 0;
            continue;
        }
        if (strncmp(line, "course=", 7) == 0) {
            char course[128];
            if (sscanf(line, "course=%127s", course) == 1) g_replay_course = course;
            continue;
        }
        if (parse_start_field_line(line, g_replay_start)) continue;
        if (strncmp(line, "start", 5) == 0) {
            parse_start_fields(line, g_replay_start);
            continue;
        }
        if (line[0] == '#' || line[0] == '!') {
            const char* p = line + 1;
            while (*p == ' ') p++;
            if (strncmp(p, "nsmbu-replay", 12) == 0) {
                g_replay_modern = true;
                g_replay_version = 3;
            }
            int ver = 0;
            if (sscanf(p, "nsmbu-input %d", &ver) == 1) g_replay_version = ver;
            char course[128];
            if (sscanf(p, "course %127s", course) == 1) g_replay_course = course;
            char origin[16];
            if (sscanf(p, "origin %15s", origin) == 1) g_clock_from_control = same_text(origin, "control");
            parse_start_fields(p, g_replay_start);
            continue;
        }
        if (line[0] == '\n') continue;
        if (line[0] == '@') {
            unsigned long long step;
            float x, y, z;
            if (sscanf(line, "@ %llu %f %f %f", &step, &x, &y, &z) == 4) {
                pending_step = step;
                pending_x = x;
                pending_y = y;
                pending_z = z;
                have_pending = true;
            }
            continue;
        }
        unsigned long long step;
        char buttons_field[128] = {};
        int used = 0;
        if (sscanf(line, "%llu %127s %n", &step, buttons_field, &used) != 2 || used <= 0) continue;
        float lx, ly, rx, ry, tx, ty;
        float wx = 0, wy = 0, wz = 0;
        int touch;
        const int n = sscanf(line + used, "%f %f %f %f %d %f %f %f %f %f", &lx, &ly, &rx, &ry, &touch, &tx, &ty, &wx,
            &wy, &wz);
        if (n != 7 && n != 10) continue;
        uint32_t buttons = 0;
        if (!parse_buttons(buttons_field, buttons)) continue;
        PadState p;
        p.buttons = buttons;
        p.lx = lx;
        p.ly = ly;
        p.rx = rx;
        p.ry = ry;
        p.touch = touch != 0;
        p.tx = tx;
        p.ty = ty;
        ReplaySample sample{step, p, 0, 0, 0, false};
        if (n == 10) {
            sample.wx = wx;
            sample.wy = wy;
            sample.wz = wz;
            sample.witness = true;
            witness_count++;
        } else if (have_pending && pending_step == step) {
            sample.wx = pending_x;
            sample.wy = pending_y;
            sample.wz = pending_z;
            sample.witness = true;
            witness_count++;
            have_pending = false;
        }
        samples.push_back(sample);
    }
    fclose(f);
    if (const char* exit = env::REPLAY_EXIT(); exit && *exit) g_replay_exit_on_input = strcmp(exit, "none") != 0;
    g_replay_index = 0;
    g_witness_miss = 0;
    g_witness_first = 0;
    g_replay_start_checked = false;
    g_start_miss = 0;
    reset_course_input_clock();
    LOG("[input] replaying %zu samples from %s (%zu position checks)", samples.size(), path, witness_count);
    if (!g_replay_course.empty()) LOG("[input] replay course %s", g_replay_course.c_str());
    if (g_replay_start.have)
        LOG("[input] replay start player %d mode %d", g_replay_start.expect.player, g_replay_start.expect.mode);
    if (g_replay_start.have_pos)
        LOG("[input] replay start pos %.3f %.3f %.3f", g_replay_start.expect.x, g_replay_start.expect.y,
            g_replay_start.expect.z);
    return samples;
}

static void replay_finished(uint64_t step) {
    if (!g_replay_exit_on_input) {
        if (g_replay_live) return;
        g_replay_live = true;
        LOG("[input] replay stopped at step %llu; live input from now on", (unsigned long long)step);
        return;
    }
    static std::atomic<bool> sent{false};
    if (sent.exchange(true)) return;
    const int misses = g_witness_miss + g_start_miss;
    if (!misses)
        LOG("[input] replay finished at step %llu, player state matched", (unsigned long long)step);
    else if (g_start_miss && !g_witness_miss)
        LOG("[input] replay finished at step %llu, start state mismatch", (unsigned long long)step);
    else
        LOG("[input] replay finished at step %llu, %d position mismatches, first at step %llu",
            (unsigned long long)step, misses, (unsigned long long)g_witness_first);
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

static void apply_replay_start_mode() {
    if (g_replay_mode_applied || !g_replay_start.have) return;
    g_replay_mode_applied = true;
    const int mode = g_replay_start.expect.mode;
    if (mode < 0 || mode > 8) return;
    interp::PlayerSnapshot now{};
    const bool have = interp::player_snapshot(now, g_replay_start.expect.player);
    if (have && now.mode == mode) return;
    if (!interp::set_player_mode(g_replay_start.expect.player, mode)) return;
    if (have)
        LOG("[input] replay start mode %d -> %d", now.mode, mode);
    else
        LOG("[input] replay start mode %d", mode);
}

static bool replay_course_clock_ready() {
    if (!g_replay_start.have_pos && !g_clock_from_control) return true;
    interp::PlayerSnapshot snap;
    if (g_clock_from_control) {
        if (interp::input_enabled()) {
            g_replay_start_wait = false;
            apply_replay_start_mode();
            return true;
        }
        const uint64_t now = timebase::now();
        if (!g_replay_start_wait) {
            g_replay_start_wait = true;
            g_replay_wait_ticks = now;
            return false;
        }
        if (now - g_replay_wait_ticks < 20ull * timebase::kTicksPerSec) return false;
    } else {
        const bool positioned = interp::player_snapshot(snap, g_replay_start.expect.player)
            && pos_near(snap.x, g_replay_start.expect.x) && pos_near(snap.y, g_replay_start.expect.y)
            && pos_near(snap.z, g_replay_start.expect.z);
        if (positioned) {
            g_replay_start_wait = false;
            apply_replay_start_mode();
            return true;
        }
        const uint64_t now = interp::logic_steps();
        if (!g_replay_start_wait) {
            g_replay_start_wait = true;
            g_replay_start_wait_from = now;
            return false;
        }
        if (now - g_replay_start_wait_from < 180) return false;
    }
    if (!g_start_miss) {
        g_start_miss = 1;
        g_witness_first = 0;
        if (interp::player_snapshot(snap, g_replay_start.expect.player))
            LOG("[input] replay diverged before the course clock: recorded lives %d mode %d pos %.3f %.3f %.3f now lives %d mode %d pos %.3f %.3f %.3f",
                g_replay_start.expect.lives, g_replay_start.expect.mode, g_replay_start.expect.x, g_replay_start.expect.y,
                g_replay_start.expect.z, snap.lives, snap.mode, snap.x, snap.y, snap.z);
        else
            LOG("[input] replay diverged before the course clock: recorded %.3f %.3f %.3f now unavailable",
                g_replay_start.expect.x, g_replay_start.expect.y, g_replay_start.expect.z);
        replay_finished(0);
    }
    return false;
}

static bool replay_step(uint64_t& step) {
    if (g_replay_version < 2) {
        step = interp::logic_steps();
        return true;
    }
    return course_relative_step(step);
}

static void check_replay_start(uint64_t step) {
    if (g_replay_start_checked || !g_replay_start.have_pos || step != 0) return;
    g_replay_start_checked = true;
    interp::PlayerSnapshot now;
    if (!interp::player_snapshot(now, g_replay_start.expect.player)) return;
    if (pos_near(now.x, g_replay_start.expect.x) && pos_near(now.y, g_replay_start.expect.y)
        && pos_near(now.z, g_replay_start.expect.z))
        return;
    g_start_miss = 1;
    if (!g_witness_first) g_witness_first = step;
    LOG("[input] replay start mismatch: recorded player %d lives %d player_coins %d coins %d mode %d pos %.3f %.3f %.3f now "
        "player %d lives %d player_coins %d coins %d mode %d pos %.3f %.3f %.3f",
        g_replay_start.expect.player, g_replay_start.expect.lives, g_replay_start.expect.coins,
        g_replay_start.expect.course_coins, g_replay_start.expect.mode, g_replay_start.expect.x, g_replay_start.expect.y,
        g_replay_start.expect.z, now.player, now.lives, now.coins, now.course_coins, now.mode, now.x, now.y, now.z);
}

static bool apply_replay(PadState& s) {
    if (g_replay_live) return false;
    const std::vector<ReplaySample>& samples = replay_samples();
    if (samples.empty()) return false;
    uint64_t step = 0;
    if (!replay_step(step)) {
        s = PadState{};
        return true;
    }
    check_replay_start(step);
    while (g_replay_index + 1 < samples.size() && samples[g_replay_index + 1].step <= step) g_replay_index++;
    const ReplaySample& cur = samples[g_replay_index];
    if (cur.step <= step) s = cur.pad;
    if (cur.step == step && cur.witness && g_witness_checked != step) {
        interp::PlayerSnapshot now;
        if (interp::player_snapshot(now, 0)) {
            g_witness_checked = step;
            if (!pos_near(now.x, cur.wx) || !pos_near(now.y, cur.wy) || !pos_near(now.z, cur.wz)) {
                const bool stop = pos_stops(now.x, cur.wx) || pos_stops(now.y, cur.wy) || pos_stops(now.z, cur.wz);
                const bool first = g_witness_miss == 0;
                if (first || stop)
                    LOG("[input] replay diverged at step %llu: recorded %.3f %.3f %.3f now %.3f %.3f %.3f",
                        (unsigned long long)step, cur.wx, cur.wy, cur.wz, now.x, now.y, now.z);
                if (!g_witness_first) g_witness_first = step;
                g_witness_miss++;
                if (stop) replay_finished(step);
            }
        }
    }
    if (step > samples.back().step) replay_finished(step);
    return !g_replay_live;
}

static void apply_scenario(PadState& s) {
    static Scenario sc;

    if (render::frame_count() < sc.origin) return;

    static const uint64_t s0 = [] {
        LOG("[test] origin at guest time %.4f s, logic step %llu", (double)timebase::now() / timebase::kTicksPerSec,
            (unsigned long long)interp::logic_steps());
        return interp::logic_steps();
    }();
    double t = (double)(interp::logic_steps() - s0) / 30.0;
    static std::atomic<bool> mode_set{false}, ended{false};
    static std::atomic<int> dbg{0};
    if (env::TEST_DEBUG() && dbg++ % 30 == 0) LOG("[test] t=%.3f frame %llu", t, (unsigned long long)render::frame_count());
    if (sc.mode >= 0 && t >= sc.mode_at && !mode_set.exchange(true)) {
        LOG("[test] t=%.3f s: 60 fps mode %d", t, sc.mode);
        interp::set_mode(sc.mode);
    }
    if (sc.end > 0 && t >= sc.end && !ended.exchange(true)) {
        LOG("[test] t=%.3f s: end", t);
        if (FILE* f = fopen("test_done", "w")) fclose(f);
    }
    for (auto& p : sc.sticks)
        if (t >= p.from && t < p.to) { s.lx = p.x; s.ly = p.y; }
    for (auto& p : sc.rsticks)
        if (t >= p.from && t < p.to) { s.rx = p.x; s.ry = p.y; }
    for (auto& p : sc.presses)
        if (t >= p.from && t < p.to) s.buttons |= p.bits;
}

static std::atomic<bool> g_pro{env::PRO_CONTROLLER() != nullptr};
bool pro_controller() { return g_pro.load(std::memory_order_relaxed); }
void set_pro_controller(bool on) { g_pro = on; LOG("[input] keyboard/controller act as %s", on ? "Pro Controller" : "GamePad"); }

PadState read(int player) {
    player = std::clamp(player, 0, input::kMaxPlayers - 1);
    if (player == 0 && env::REPLAY_INPUT()) {
        PadState s{};
        if (apply_replay(s)) return s;
    }
    static const std::vector<Press> script = scripted();
    static const std::vector<Stick> sticks = scripted_stick();
    static const std::vector<KeyPress> keys = scripted_keys();
    static const std::vector<Stick> rsticks = scripted_stick(env::RSTICK);
    std::lock_guard<std::mutex> lk(g_mu);

    static const bool no_host = env::NO_HOST_INPUT() != nullptr;
    PadState s = g_pads[player];
    if (no_host) s = PadState{};
    if (player == 0) {
        const uint64_t sf = script_frame::frame(render::frame_count());
        if (!keys.empty()) {
            memset(g_script_keys, 0, sizeof g_script_keys);
            for (auto& p : keys)
                if (sf >= p.from && sf <= p.to)
                    for (int c : p.codes) g_script_keys[c] = true;
        }
        PadState k = keyboard_state(!no_host);
        for (auto& p : script)
            if (sf >= p.from && sf <= p.to) s.buttons |= p.bits;
        for (auto& p : sticks)
            if (sf >= p.from && sf <= p.to) { s.lx = p.x; s.ly = p.y; }
        for (auto& p : rsticks)
            if (sf >= p.from && sf <= p.to) { s.rx = p.x; s.ry = p.y; }
        apply_scenario(s);
        s.buttons |= k.buttons;
        if (k.lx || k.ly) { s.lx = k.lx; s.ly = k.ly; }
        if (k.rx || k.ry) { s.rx = k.rx; s.ry = k.ry; }
        agent_ctl::apply_pad(s);
        s.touch = g_touch;
        s.tx = g_tx;
        s.ty = g_ty;

        static const bool log_buttons = env::LOG_BUTTONS() != nullptr;
        static uint32_t last_buttons = 0;
        if (log_buttons && s.buttons != last_buttons) {
            LOG("[input] frame %llu buttons %04X", (unsigned long long)render::frame_count(), s.buttons);
            last_buttons = s.buttons;
        }
    }
    if (overlay::blocks_input()) s = PadState{};
    if (player == 0) record_pad(s);
    return s;
}

PadState read() { return read(0); }

}
