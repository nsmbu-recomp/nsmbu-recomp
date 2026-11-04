
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include "settings.h"
#include "save/savestate.h"
#include "crash/crashrec.h"
#include <cmath>
#include "hooks/interp.h"
#include "gfx/res_presets.h"
@interface NsmbuVulkanStateMenu : NSObject <NSMenuDelegate>
@end
@implementation NsmbuVulkanStateMenu
- (void)save:(NSMenuItem*)item { ss::request_save((int)item.tag); }
- (void)load:(NSMenuItem*)item { ss::request_load((int)item.tag); }
- (void)toggleCrashRecovery:(NSMenuItem*)item { crashrec::set_enabled(!crashrec::enabled()); }
- (void)toggleFullStates:(NSMenuItem*)item { ss::set_full_states(!ss::full_states()); }
- (void)loadAuto:(NSMenuItem*)item { crashrec::request_load((int)item.tag); }
- (void)menuNeedsUpdate:(NSMenu*)menu {
    [menu removeAllItems];
    ss::SlotInfo slots[ss::kSlots + 1];
    for(int slot=1;slot<=ss::kSlots;++slot)slots[slot]=ss::slot_info(slot);
    auto label=[&](int slot) -> NSString* {
        const auto& info=slots[slot];
        if(!info.used)return @"empty";
        NSString* title=[NSString stringWithFormat:@"%s%s%s%s",info.when.c_str(),
            info.area.empty()?"":" · ",info.area.c_str(),info.portable?"":" · full"];
        if (!info.controller.empty()) title = [title stringByAppendingFormat:@" · %s", info.controller.c_str()];
        return info.compatible?title:[title stringByAppendingString:@" (incompatible)"];
    };
    for(int slot=1;slot<=ss::kSlots;++slot){
        NSMenuItem* item=[menu addItemWithTitle:[NSString stringWithFormat:@"Save to slot %d (%@)",slot,label(slot)]
            action:@selector(save:) keyEquivalent:@""];
        item.target=self;item.tag=slot;
        item.toolTip=[NSString stringWithFormat:@"Shortcut in game: Shift+F%d",slot];
    }
    [menu addItem:NSMenuItem.separatorItem];
    for(int slot=1;slot<=ss::kSlots;++slot){
        NSMenuItem* item=[menu addItemWithTitle:[NSString stringWithFormat:@"Load slot %d (%@)",slot,label(slot)]
            action:@selector(load:) keyEquivalent:@""];
        item.target=self;item.tag=slot;
        item.enabled=slots[slot].used&&slots[slot].compatible;
        item.toolTip=[NSString stringWithFormat:@"Shortcut in game: F%d",slot];
    }
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem* fs=[menu addItemWithTitle:@"Full Save States (large, contain game data, don't share)"
        action:@selector(toggleFullStates:) keyEquivalent:@""];
    fs.target=self;fs.state=ss::full_states()?NSControlStateValueOn:NSControlStateValueOff;
    fs.enabled=!ss::full_states_forced();
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem* cr=[menu addItemWithTitle:[NSString stringWithFormat:@"Crash Recovery (automatic state every %d min)",
        (crashrec::interval_seconds()+30)/60] action:@selector(toggleCrashRecovery:) keyEquivalent:@""];
    cr.target=self;cr.state=crashrec::enabled()?NSControlStateValueOn:NSControlStateValueOff;
    for(int i=1;i<=crashrec::kAutoSlots;++i){
        crashrec::AutoInfo a=crashrec::auto_info(i);
        NSString* d=a.used?[NSString stringWithFormat:@"%s%s%s",a.when.c_str(),a.area.empty()?"":" · ",a.area.c_str()]:@"empty";
        NSMenuItem* item=[menu addItemWithTitle:[NSString stringWithFormat:@"Load automatic state %d (%@)",i,d]
            action:@selector(loadAuto:) keyEquivalent:@""];
        item.target=self;item.tag=i;item.enabled=a.used;
    }
}
@end
@interface NsmbuVulkanGraphicsMenu : NSObject <NSMenuItemValidation>
@end
@implementation NsmbuVulkanGraphicsMenu
- (void)resolution:(NSMenuItem*)i { gfxvk::set_res_scale(gfx::kResPresets[i.tag].scale); }
- (void)aniso:(NSMenuItem*)i { gfxvk::set_aniso(!gfxvk::aniso_enabled()); }
- (void)fxaa:(NSMenuItem*)i { gfxvk::set_fxaa(!gfxvk::fxaa_enabled()); }
- (void)filter:(NSMenuItem*)i { gfxvk::set_scale_filter((int)i.tag); }

- (void)interpolation:(NSMenuItem*)i { interp::toggle_fps((int)i.tag); }
- (BOOL)validateMenuItem:(NSMenuItem*)i {
    BOOL on=NO; bool available=true;
    if(i.action==@selector(resolution:))on=std::fabs(gfxvk::requested_res_scale()-gfx::kResPresets[i.tag].scale)<0.01f;
    if(i.action==@selector(aniso:)){on=gfxvk::aniso_enabled();available=gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::Anisotropy);}
    if(i.action==@selector(fxaa:)){on=gfxvk::fxaa_enabled();available=gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::FXAA);}
    if(i.action==@selector(filter:)){on=gfxvk::scale_filter()==i.tag;available=gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::ScaleFilter);}
    if(i.action==@selector(interpolation:))on=interp::mode()==1&&interp::fps()==i.tag;
    i.state=available&&on?NSControlStateValueOn:NSControlStateValueOff;
    return available;
}
@end
namespace gfxvk {
void install_graphics_menu(SDL_Window* window) {

    if(!window || ![NSThread isMainThread])return;
    static NsmbuVulkanGraphicsMenu* target;
    if(target)return;
    target=[NsmbuVulkanGraphicsMenu new];
    NSMenu* bar=NSApp.mainMenu;
    if(!bar){bar=[[NSMenu alloc]initWithTitle:@""];NSApp.mainMenu=bar;}
    NSMenuItem* root=[[NSMenuItem alloc]initWithTitle:@"Graphics" action:nil keyEquivalent:@""];
    NSMenu* menu=[[NSMenu alloc]initWithTitle:@"Graphics"];root.submenu=menu;[bar addItem:root];
    auto add=[&](NSString* title,SEL action,NSInteger tag){NSMenuItem* i=[[NSMenuItem alloc]initWithTitle:title action:action keyEquivalent:@""];i.target=target;i.tag=tag;[menu addItem:i];};
    for(size_t i=0;i<gfx::res_preset_count();++i){
        NSString* title=@(gfx::kResPresets[i].label);
        add(title,@selector(resolution:),(NSInteger)i);
    }
    [menu addItem:NSMenuItem.separatorItem];
    add(@"16× anisotropic filtering — N",@selector(aniso:),0);
    add(@"FXAA — 8",@selector(fxaa:),0);
    [menu addItem:NSMenuItem.separatorItem];
    add(@"Scaling: Smooth",@selector(filter:),0);
    add(@"Scaling: Sharp",@selector(filter:),1);
    add(@"Scaling: Integer",@selector(filter:),2);
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem* fpsLabel=[[NSMenuItem alloc]initWithTitle:@"Frame rate (experimental)" action:nil keyEquivalent:@""];
    fpsLabel.enabled=NO;[menu addItem:fpsLabel];
    add(@"60 FPS interpolation — 6",@selector(interpolation:),60);
    add(@"120 FPS interpolation",@selector(interpolation:),120);
    add(@"240 FPS interpolation",@selector(interpolation:),240);
    static NsmbuVulkanStateMenu* stateTarget=[NsmbuVulkanStateMenu new];
    NSMenuItem* stateRoot=[[NSMenuItem alloc]initWithTitle:@"Save States" action:nil keyEquivalent:@""];
    NSMenu* stateMenu=[[NSMenu alloc]initWithTitle:@"Save States"];
    stateMenu.autoenablesItems=NO;
    stateMenu.delegate=stateTarget;
    stateRoot.submenu=stateMenu;
    [bar addItem:stateRoot];
}
}
