
#include <SDL3/SDL.h>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "input/input.h"
#include "input/input_map.h"
#include "input/rumble.h"
#include "platform/input_sdl.h"
#include "core/env.h"
#include "hooks/interp.h"
static int savedSlot=0, loadedSlot=0, saveRequests=0, loadRequests=0;
namespace ss { void request_save(int slot){savedSlot=slot;++saveRequests;} void request_load(int slot){loadedSlot=slot;++loadRequests;} }
static int graphicsRequests=0; static char graphicsKey=0;
namespace render { uint64_t frame_count(){return 0;} }
namespace gfxvk { bool graphics_hotkey(char key,bool activate){if(activate){++graphicsRequests;graphicsKey=key;}return true;} }
namespace gfx { int plusPresses=0; void display_plus_pressed(){++plusPresses;} }
namespace mods { double game_time(){return 0;} }
namespace agent_ctl { void apply_pad(input::PadState&) {} }
static uint64_t g_logic_steps=0;
static bool g_course=true;
static bool g_input_enabled=true;
static interp::PlayerSnapshot g_snapshot{0,5,0,0,2,600,-848,3000};
static int g_mode_sets=0;
namespace interp {
void set_mode(int){}
uint64_t logic_steps(){return g_logic_steps;}
bool course_play_active(){return g_course;}
bool input_enabled(){return g_input_enabled;}
bool player_witness(float&, float&, float&){return false;}
bool player_snapshot(interp::PlayerSnapshot& out, int player){if(player!=0)return false;out=g_snapshot;return true;}
bool set_player_mode(int player, int mode){if(player!=0)return false;++g_mode_sets;g_snapshot.mode=mode;return true;}
}
namespace timebase { uint64_t now(){return 0;} }
void log_msg(const char*,...){}

static bool promptShown=false;static int promptKeys=0;static std::string promptText;
static bool overlayOpen=false;
namespace overlay { bool key(int,bool,bool,int){if(promptShown)++promptKeys;return promptShown;} bool is_open(){return overlayOpen;} bool blocks_input(){return promptShown;} bool wants_text_input(){return false;}
 bool captures(){return promptShown;}
 bool mouse_move(float,float,bool){return false;} bool mouse_button(int,bool,bool){return false;} bool mouse_wheel(float,float){return false;}
 bool text(const char*){return promptShown;} }
namespace text_entry { bool active(){return promptShown;} void text(const char* s){promptText+=s;} void preedit(const char*){} }
namespace hostui { void graphics_changed(){} }

static int shots=0;
namespace screenshot { bool key_down(int code){for(int k:input_map::current().keys[input_map::kScreenshot])if(k==code){++shots;return true;}return false;} void poll_controller(const float*){} }

static void set_env(const env::Var& name, const char* value) {
    if (!value) {
        env::unset(name);
        return;
    }
#ifdef _WIN32
    _putenv_s(name.name(), value);
#else
    setenv(name.name(), value, 1);
#endif
}
int main(){
 SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");
 assert(SDL_Init(SDL_INIT_EVENTS|SDL_INIT_VIDEO));
 input::init();input_map::set_current(input_map::Mapping::defaults(),false);
 auto key=[](SDL_Scancode code,bool down){SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=code;input::handle_event(e);};
 key(SDL_SCANCODE_K,true);assert(input::read().buttons&input::kA);
 key(SDL_SCANCODE_K,false);assert(!(input::read().buttons&input::kA));
 key(SDL_SCANCODE_W,true);assert(input::read().ly==1);
 key(SDL_SCANCODE_D,true);assert(input::read().lx>0.7f&&input::read().lx<0.71f);
 key(SDL_SCANCODE_LSHIFT,true);assert(input::read().buttons&input::kY);
 SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;input::handle_event(focus);assert(input::read().buttons==0&&input::read().lx==0);
 auto map=input_map::Mapping::defaults();map.keys[input_map::kA]={input_map::key_from_id("J"),input_map::kNoKey};map.keys[input_map::kB]={input_map::kNoKey,input_map::kNoKey};input_map::set_current(map,false);
 key(SDL_SCANCODE_J,true);assert(input::read().buttons==input::kA);
 input::set_touch(true,0.25f,0.75f);auto pad=input::read();assert(pad.touch&&pad.tx==0.25f&&pad.ty==0.75f);
 input::set_pro_controller(true);assert(input::pro_controller());input::set_pro_controller(false);
 bool held[256];input::held_keys(held);assert(held[input_map::key_from_id("J")]);
 input::release_keys();input::held_keys(held);assert(!held[input_map::key_from_id("J")]);
 input::update();float values[input_map::kPadCount];input::controller_values(values);assert(values[input_map::kPadA]==0);

 auto* game=SDL_CreateWindow("Game",32,32,SDL_WINDOW_HIDDEN);
 auto* controls=SDL_CreateWindow("Controls",32,32,SDL_WINDOW_HIDDEN);
 assert(game&&controls);input::set_prompt_window(game);
 auto stateKey=[&](int slot,SDL_Keymod mod,bool repeat=false,SDL_Window* window=nullptr,Uint32 type=SDL_EVENT_KEY_DOWN){
  SDL_Event e{};e.type=type;e.key.scancode=SDL_Scancode(SDL_SCANCODE_F1+slot-1);
  e.key.windowID=SDL_GetWindowID(window?window:game);e.key.mod=mod;e.key.repeat=repeat;input::handle_event(e);
 };
 for(int slot=1;slot<=5;++slot){
  stateKey(slot,SDL_KMOD_NONE);assert(loadedSlot==slot&&loadRequests==slot);
  stateKey(slot,SDL_KMOD_LSHIFT);assert(savedSlot==slot&&saveRequests==2*slot-1);
  stateKey(slot,SDL_KMOD_RSHIFT);assert(savedSlot==slot&&saveRequests==2*slot);
 }
 const int saves=saveRequests,loads=loadRequests;
 stateKey(1,SDL_KMOD_LSHIFT,true);stateKey(1,SDL_KMOD_NONE,false,controls);
 stateKey(1,SDL_KMOD_LSHIFT,false,nullptr,SDL_EVENT_KEY_UP);
 stateKey(6,SDL_KMOD_NONE);input::set_prompt_window(nullptr);stateKey(1,SDL_KMOD_NONE);
 assert(saveRequests==saves&&loadRequests==loads);
 input::set_prompt_window(game);input::release_keys();
 for(int slot=1;slot<=5;++slot)stateKey(slot,SDL_KMOD_NONE,true);
 input::held_keys(held);
 for(const char* id:{"F1","F2","F3","F4","F5"})assert(!held[input_map::key_from_id(id)]);
 auto graphicsEvent=[&](SDL_Scancode code,SDL_Window* window, bool repeat=false, Uint32 type=SDL_EVENT_KEY_DOWN,SDL_Keymod mod=SDL_KMOD_NONE){SDL_Event e{};e.type=type;e.key.scancode=code;e.key.windowID=SDL_GetWindowID(window);e.key.repeat=repeat;e.key.mod=mod;input::handle_event(e);};
 for(auto code:{SDL_SCANCODE_R,SDL_SCANCODE_O,SDL_SCANCODE_M,SDL_SCANCODE_N,SDL_SCANCODE_8,SDL_SCANCODE_6,SDL_SCANCODE_7,SDL_SCANCODE_T}){
  int before=graphicsRequests;graphicsEvent(code,game);assert(graphicsRequests==before+1);
  graphicsEvent(code,game,true);graphicsEvent(code,game,false,SDL_EVENT_KEY_UP);graphicsEvent(code,game,false,SDL_EVENT_KEY_DOWN,SDL_KMOD_CTRL);graphicsEvent(code,controls);assert(graphicsRequests==before+1);
 }
 assert(graphicsKey=='T');input::release_keys();

 graphicsEvent(SDL_SCANCODE_F10,game);graphicsEvent(SDL_SCANCODE_F10,game,true);assert(shots==1);
 input::held_keys(held);assert(!held[input_map::key_from_id("F10")]);
 graphicsEvent(SDL_SCANCODE_F10,game,false,SDL_EVENT_KEY_UP);
 {auto m=input_map::current();m.keys[input_map::kScreenshot]={input_map::key_from_id("F9"),input_map::kNoKey};input_map::set_current(m,false);
  graphicsEvent(SDL_SCANCODE_F10,game);assert(shots==1);graphicsEvent(SDL_SCANCODE_F9,game);assert(shots==2);
  m.keys[input_map::kScreenshot]={input_map::key_from_id("F10"),input_map::kNoKey};input_map::set_current(m,false);input::release_keys();}
 set_env(env::NO_HOST_INPUT,"1");graphicsEvent(SDL_SCANCODE_R,game);assert(graphicsRequests==8);set_env(env::NO_HOST_INPUT,nullptr);

 promptShown=true;input::update();
 SDL_Event typed{};typed.type=SDL_EVENT_TEXT_INPUT;typed.text.text="Mario";input::handle_event(typed);assert(promptText=="Mario");
 key(SDL_SCANCODE_K,true);graphicsEvent(SDL_SCANCODE_RETURN,controls);assert(promptKeys==2);
 input::held_keys(held);assert(!held[input_map::key_from_id("K")]&&!(input::read().buttons&input::kA));
 promptShown=false;input::update();

 graphicsEvent(SDL_SCANCODE_RETURN,game,true);input::held_keys(held);assert(!held[input_map::key_from_id("Return")]);
 graphicsEvent(SDL_SCANCODE_RETURN,game,false,SDL_EVENT_KEY_UP);

 static std::atomic<Uint16> motorLow{0},motorHigh{0};static std::atomic<int> motorCalls{0};
 SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
 desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.name="rumble test pad";
 desc.Rumble=[](void*,Uint16 low,Uint16 high){motorLow=low;motorHigh=high;++motorCalls;return true;};
 const SDL_JoystickID padId=SDL_AttachVirtualJoystick(&desc);assert(padId);
 for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
 SDL_ShowWindow(game);for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
 const bool focused=SDL_GetKeyboardFocus()!=nullptr;
 const uint8_t on[15]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
 rumble::set_enabled(true);rumble::gamepad_pattern(0,on,120);input::update();
 if(focused){
  assert(motorLow==0xFFFF&&motorHigh==0xFFFF);
  rumble::set_enabled(false);input::update();assert(motorLow==0);
  rumble::set_enabled(true);input::update();assert(motorLow==0xFFFF);
  overlayOpen=true;input::update();assert(motorLow==0);overlayOpen=false;
  input::update();assert(motorLow==0xFFFF);
  rumble::gamepad_stop(0);input::update();assert(motorLow==0);
  const int calls=motorCalls;input::update();input::update();assert(motorCalls==calls);
  rumble::pro_motor(0,true);input::update();assert(motorLow==0xFFFF);
  SDL_HideWindow(game);for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
  if(!SDL_GetKeyboardFocus()){input::update();assert(motorLow==0);SDL_ShowWindow(game);for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);}
  input::update();assert(motorLow==0xFFFF);
  SDL_Delay(600);assert(motorLow==0);
  rumble::pro_motor(0,true);input::update();assert(motorLow==0xFFFF);
  SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);assert(motorLow==0);
  input::update();assert(motorLow==0);
 }else{
  assert(motorLow==0);puts("input_sdl_test: no window has the keyboard here, rumble checked with the motors still only");
 }
 rumble::reset();
 {
  SDL_VirtualJoystickDesc second;SDL_INIT_INTERFACE(&second);
  second.type=SDL_JOYSTICK_TYPE_GAMEPAD;second.naxes=SDL_GAMEPAD_AXIS_COUNT;second.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;second.name="second test pad";
  const SDL_JoystickID pad2=SDL_AttachVirtualJoystick(&second);assert(pad2);
  for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
  assert(input::controller_connected(0)&&input::controller_connected(1));
  assert(!input::controller_name(1).empty());

  auto config=input_map::config();
  config.sync=false;
  config.players[0]=input_map::Mapping::defaults();
  config.players[1]=input_map::Mapping::defaults();
  config.players[0].pad[input_map::kA]=input_map::kPadA;
  config.players[0].pad[input_map::kB]=input_map::kPadNone;
  config.players[1].pad[input_map::kA]=input_map::kPadX;
  config.players[1].pad[input_map::kY]=input_map::kPadNone;
  input_map::set_config(config,false);

  SDL_Joystick* first=SDL_GetJoystickFromID(padId);
  SDL_Joystick* other=SDL_GetJoystickFromID(pad2);
  assert(first&&other);
  SDL_SetJoystickVirtualButton(first,SDL_GAMEPAD_BUTTON_SOUTH,true);
  SDL_SetJoystickVirtualButton(other,SDL_GAMEPAD_BUTTON_WEST,true);
  SDL_UpdateJoysticks();
  input::update();
  assert(input::read(0).buttons==input::kA);
  assert(input::read(1).buttons==input::kA);
  assert(input::read(2).buttons==0);
  input::release_keys();
  SDL_SetJoystickVirtualButton(first,SDL_GAMEPAD_BUTTON_SOUTH,false);
  SDL_SetJoystickVirtualButton(other,SDL_GAMEPAD_BUTTON_WEST,false);
  SDL_UpdateJoysticks();
  input::update();
  assert(input::read(0).buttons==0&&input::read(1).buttons==0);
  input_map::set_config(input_map::Config::defaults(),false);
  SDL_DetachVirtualJoystick(pad2);
  for(SDL_Event e;SDL_PollEvent(&e);)input::handle_event(e);
 }
 {
  const char* rec="input_record_test.txt";
  remove(rec);
  set_env(env::RECORD_INPUT,rec);
  set_env(env::RECORD_COURSE,"2-1");
  g_logic_steps=4;
  input::release_keys();
  input::read();
  set_env(env::RECORD_INPUT,nullptr);
  set_env(env::RECORD_COURSE,nullptr);
  FILE* rf=fopen(rec,"r");
  assert(rf);
  char buf[512]={};
  assert(fread(buf,1,sizeof buf-1,rf)>0);
  fclose(rf);
  assert(strstr(buf,"version=1"));
  assert(strstr(buf,"course=2-1"));
  assert(strstr(buf,"origin=control"));
  assert(strstr(buf,"mode=fire"));
  assert(strstr(buf,"player_coins=0"));
  assert(strstr(buf,"\ncoins=0"));
  assert(!strstr(buf,"pos="));
  assert(strstr(buf,"\n0 "));
  remove(rec);
  const char* rep="input_replay_test.txt";
  FILE* wf=fopen(rep,"w");
  assert(wf);
  fputs("version=1\ncourse=1-1\norigin=control\nplayer=0\nmode=fire\n"
        "# step buttons lx ly rx ry touch tx ty x y z\n"
        "0 - 0 0 0 0 0 0 0 600 -848 3000\n"
        "10 A+Y 0.5 -1 0 0 1 0.25 0.5 600 -848 3000\n"
        "12 00008000 0 0 0 0 0 0 0 600 -848 3000\n"
        "20 - 0 0 0 0 0 0 0 600 -848 3000\n",wf);
  fclose(wf);
  set_env(env::REPLAY_INPUT,rep);
  g_input_enabled=false;
  g_snapshot.mode=0;
  g_logic_steps=0;
  auto gated=input::read();
  assert(gated.buttons==0&&!gated.touch&&g_mode_sets==0);
  g_input_enabled=true;
  auto early=input::read();
  assert(early.buttons==0&&early.lx==0&&!early.touch);
  assert(g_mode_sets==1&&g_snapshot.mode==2);
  g_logic_steps=10;
  auto played=input::read();
  assert(played.buttons==(input::kA|input::kY)&&played.lx==0.5f&&played.ly==-1.f&&played.touch&&played.tx==0.25f&&played.ty==0.5f);
  g_snapshot.x=600.1f;
  auto again=input::read();
  assert(again.buttons==(input::kA|input::kY)&&again.lx==0.5f);
  g_snapshot.x=600;
  for(SDL_Event pending;SDL_PollEvent(&pending);) assert(pending.type!=SDL_EVENT_QUIT);
  g_logic_steps=11;
  assert(input::read().buttons==(input::kA|input::kY));
  g_snapshot.y=-847.f;
  g_logic_steps=12;
  assert(input::read().buttons==input::kA);
  g_snapshot.y=-848.f;
  for(SDL_Event pending;SDL_PollEvent(&pending);) assert(pending.type!=SDL_EVENT_QUIT);
  g_snapshot.y=-800.f;
  g_logic_steps=20;
  auto done=input::read();
  assert(done.buttons==0&&done.lx==0);
  bool quit=false;
  for(SDL_Event pending;SDL_PollEvent(&pending);) if(pending.type==SDL_EVENT_QUIT) quit=true;
  assert(quit);
  remove(rep);
 }
 SDL_DestroyWindow(controls);SDL_DestroyWindow(game);input::set_prompt_window(nullptr);
 SDL_Quit();puts("input_sdl_test: keyboard mapping, focus, touch, Pro mode, guarded save-state shortcuts, screenshot binding, text prompt routing, rumble passed");
}
