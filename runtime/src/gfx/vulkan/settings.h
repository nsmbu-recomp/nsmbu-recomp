#pragma once
struct SDL_Window;
namespace gfxvk {
float res_scale();
float requested_res_scale();
void set_res_scale(float scale);
bool aniso_enabled();
void set_aniso(bool enabled);
bool fxaa_enabled();
void set_fxaa(bool enabled);
int scale_filter();
void set_scale_filter(int filter);
enum class GraphicsFeature { Anisotropy, FXAA, ScaleFilter, Count };
bool graphics_feature_available(GraphicsFeature feature);
void set_graphics_feature_available(GraphicsFeature feature, bool available = true);

enum PresentMode { kPresentFifo, kPresentMailbox, kPresentImmediate, kPresentModes };
int present_mode();
void set_present_mode(int mode);
bool present_mode_user_set();
bool present_mode_from_env();
bool present_mode_offered(int mode);
void set_present_modes_offered(unsigned mask);
const char* present_mode_name(int mode);

int effective_present_mode();

bool graphics_hotkey(char key, bool activate);
#ifdef __APPLE__
void install_graphics_menu(SDL_Window* window);
#else
inline void install_graphics_menu(SDL_Window*) {}
#endif
}
