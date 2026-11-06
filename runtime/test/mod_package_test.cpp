#include "mods/packages.h"
#include "mods/content.h"
#include <cassert>
#include <cstdlib>
#include <string>

namespace {
void env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key,value,1); else unsetenv(key);
#endif
}
}
#include "mods/cemu_pack.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
namespace {
mods::packages::View view(const std::string& id) {for(auto v:mods::packages::list())if(v.id==id)return v;return {};}
}
int main(int argc, char** argv) {
    namespace fs=std::filesystem;
    using namespace mods::packages;
    if(argc==2&&(std::string(argv[1])=="--cemu-startup"||std::string(argv[1])=="--cemu-backend")){
        bool backend=std::string(argv[1])=="--cemu-backend";
        auto root=fs::temp_directory_path()/("nsmbu-cemu-startup-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto storage=root/"storage",pack=storage/"mods"/"cemu.test";fs::create_directories(pack);
        std::ofstream(pack/"manifest.json")<<R"({"format_version":1,"id":"cemu.test","name":"Test","version":"1.0.0","game_id":"nsmbu-usa","kind":"cemu","cemu_dir":""})";
        std::ofstream(pack/"rules.txt")<<"[Definition]\nname=Test\ntitleIds=0005000010101d00\nversion=4\n[Preset]\nname=Normal\n$scale=1\n[Preset]\nname=Double\n$scale=2\n[TextureRedefine]\nwidth=1280\nheight=720\noverwriteWidth=1280*$scale\n";
        std::ofstream(storage/"mods.json")<<R"({"format_version":1,"enabled":{"cemu.test":true},"config":{"cemu.test":{"preset-0":"Double"}}})";
        if(backend)std::ofstream(pack/"0000000000000001_0000000000000002_ps.txt")<<"#version 420\nvoid main(){}\n";
        env("NSMBU_NO_HOST_INPUT","1");env("NSMBU_MOD_MANAGER_DIR",storage.string().c_str());initialize();
        if(backend){
            for(const auto& view:list())if(view.id=="cemu.test")assert(!view.active&&!view.compatible&&view.enabled);
            std::string error;assert(enable("cemu.test",false,error));assert(remove("cemu.test",error));
            fs::remove_all(root);
            std::cout<<"Unavailable shader backend permits disabling shader packs\n";return 0;
        }
        uint32_t width=0,height=0;assert(mods::cemu::texture_extent(1280,720,0x80e,1,4,width,height)&&width==2560&&height==720);
        std::string error;assert(list().at(0).active&&!list().at(0).pending_restart);
        assert(configure("cemu.test","preset-0","Normal",error));assert(list().at(0).pending_restart);
        assert(mods::cemu::texture_extent(1280,720,0x80e,1,4,width,height)&&width==2560);
        assert(enable("cemu.test",false,error));frame(100);assert(list().at(0).active&&list().at(0).pending_restart);
        assert(!remove("cemu.test",error));assert(!install(pack.string(),error));fs::remove_all(root);
        std::cout<<"Cemu startup presets and restart-only immutable lifecycle passed\n";return 0;
    }
    assert(argc == 2);
    auto root=fs::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    assert(!fs::exists(root));
    fs::create_directories(root);
    env("NSMBU_NO_HOST_INPUT","1");
    env("NSMBU_MOD_MANAGER_DIR",(root/"storage").string().c_str());
    fs::create_directories(root/"storage");
    std::ofstream(root/"storage"/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{},"config":{},"builtins":{"direct-camera":true},"builtin_options":{"direct-camera.speed":1.5}}}})";
    auto migrate=root/"storage"/"mods"/"content.migrate";fs::create_directories(migrate/"content"/"Common");
    std::ofstream(migrate/"content"/"Common"/"m.bin")<<"m";
    std::ofstream(migrate/"manifest.json")<<R"({"format_version":1,"id":"content.migrate","name":"Migrate","version":"1.0.0","game_id":"nsmbu-usa","kind":"content","content_dir":"content"})";
    initialize();
    std::string error;
    {auto v=view("content.migrate");assert(!v.compatible&&v.reason.find("Content mods are not supported")!=std::string::npos);
        std::ifstream mf(migrate/"manifest.json");std::string text{std::istreambuf_iterator<char>(mf),{}};
        assert(text.find("\"kind\":\"content\"")!=std::string::npos);}
    assert(remove("content.migrate",error));
    auto cemu_package=[&](const char* id, const char* extra) {
        auto path=root/id;fs::create_directories(path);
        std::ofstream(path/"rules.txt")<<("[Definition]\nname="+std::string(id)+"\ntitleIds=0005000010101d00\nversion=4\n[Preset]\nname=Normal\n$scale=1\n[Preset]\nname=Double\n$scale=2\n[TextureRedefine]\nwidth=1280\nheight=720\noverwriteWidth=1280*$scale\n");
        std::ofstream(path/"manifest.json") << "{\"format_version\":1,\"id\":\"" << id
          << "\",\"name\":\"" << id << "\",\"version\":\"1.0.0\",\"game_id\":\"nsmbu-usa\","
          << "\"kind\":\"cemu\",\"cemu_dir\":\"\"" << extra << "}";
        return path.string();
    };
    auto rejected=root/"rejected";fs::create_directories(rejected);
    std::ofstream(rejected/"rules.txt")<<"[Definition]\nname=Rejected\ntitleIds=0005000010101d00\nversion=4\n[TextureRedefine]\nwidth=1280\nheight=720\noverwriteWidth=1280\n";
    std::ofstream(rejected/"manifest.json")<<R"({"format_version":1,"id":"rejected","name":"Rejected","version":"1.0.0","game_id":"nsmbu-usa","kind":"cemu","cemu_dir":"","dependencies":[{"id":"builtin:direct-camera"}]})";
    assert(!install(rejected.string(),error));
    assert(error.find("Unknown built-in mod: builtin:direct-camera")!=std::string::npos);
    for(const auto& [kind,message]:std::initializer_list<std::pair<const char*,const char*>>{
            {"native","Native mods are not supported"},
            {"guest","Guest mods are not supported"},
            {"settings","Settings mods are not supported"},
            {"lua","Lua mods are not supported"},
            {"content","Content mods are not supported"}}){
        auto rejected_kind=root/(std::string("rejected-")+kind);fs::create_directories(rejected_kind);
        std::ofstream(rejected_kind/"manifest.json")<<"{\"format_version\":1,\"id\":\"rejected-"<<kind<<"\",\"name\":\"Rejected\",\"version\":\"1.0.0\",\"game_id\":\"nsmbu-usa\",\"kind\":\""<<kind<<"\"}";
        assert(!install(rejected_kind.string(),error));
        assert(error.find(message)!=std::string::npos);
    }
    assert(install(cemu_package("climb-preset",""),error));
    assert(list().size()==1 && list()[0].id=="climb-preset");
    assert(list()[0].kind=="cemu" && list()[0].restart_required);
    assert(enable("climb-preset",true,error));frame(1);
    assert(view("climb-preset").enabled && !view("climb-preset").active && view("climb-preset").pending_restart);
    {
        assert(fs::exists(root/"storage"/"mods.json"));
        assert(!fs::exists(root/"storage"/"profiles.json"));
        std::ifstream saved(root/"storage"/"mods.json");
        std::string text{std::istreambuf_iterator<char>(saved),{}};
        assert(text.find("builtins")==std::string::npos);
        assert(text.find("builtin_options")==std::string::npos);
        assert(text.find("\"profiles\"")==std::string::npos);
        assert(text.find("\"active\"")==std::string::npos);
        assert(text.find("\n  \"enabled\"")!=std::string::npos);
    }
    assert(!remove("climb-preset",error));
    assert(enable("climb-preset",false,error));frame(2);assert(!view("climb-preset").enabled);
    assert(enable("climb-preset",true,error));frame(20);assert(view("climb-preset").enabled);
    assert(install(cemu_package("missing-dep",",\"dependencies\":[{\"id\":\"absent\"}]"),error));
    assert(!enable("missing-dep",true,error));
    assert(remove("missing-dep",error));
    assert(install(cemu_package("cycle-a",",\"dependencies\":[{\"id\":\"cycle-b\"}]"),error));
    assert(install(cemu_package("cycle-b",",\"dependencies\":[{\"id\":\"cycle-a\"}]"),error));
    assert(!enable("cycle-a",true,error));assert(error.find("cycle")!=std::string::npos);
    assert(remove("cycle-a",error));assert(remove("cycle-b",error));
    assert(install(cemu_package("conflicting",",\"conflicts\":[\"climb-preset\"]"),error));
    assert(!enable("conflicting",true,error));assert(remove("conflicting",error));
    assert(install(cemu_package("typed",""),error));
    assert(configure("typed","preset-0","Double",error));assert(!configure("typed","preset-0","Unknown",error));
    assert(install((root/"typed").string(),error));assert(remove("typed",error));
    auto bad=root/"bad.nsmbumod";std::ofstream(bad)<<"not a package";assert(!install(bad.string(),error));
    assert(enable("climb-preset",false,error));frame(30);assert(remove("climb-preset",error));
    auto plain=root/"plain";fs::create_directories(plain/"content"/"Common");
    std::ofstream(plain/"content"/"Common"/"a.bin")<<"x";
    std::ofstream(plain/"manifest.json")<<R"({"format_version":2,"id":"plain","name":"Plain","version":"1.0.0","game_id":"nsmbu-usa","kind":"lua","content_dir":"content"})";
    assert(!install(plain.string(),error));
    assert(error.find("Lua mods are not supported")!=std::string::npos);
    assert(list().empty());
    auto graphics=root/"CemuResolution";fs::create_directories(graphics);
    std::ofstream(graphics/"rules.txt")<<"[Definition]\nname=Resolution\ntitleIds=0005000010101d00\nversion=4\n[Preset]\nname=Normal\n$scale=1\n[Preset]\nname=Double\n$scale=2\n[TextureRedefine]\nwidth=1280\nheight=720\noverwriteWidth=1280*$scale\noverwriteHeight=720*$scale\n";
    assert(install(graphics.string(),error));auto graphicsView=list().at(0);
    assert(graphicsView.kind=="cemu"&&graphicsView.restart_required&&graphicsView.options.size()==1);
    assert(configure(graphicsView.id,"preset-0","Double",error));
    assert(!configure(graphicsView.id,"preset-0","Unknown",error));
    assert(enable(graphicsView.id,true,error));frame(45);
    assert(!list().at(0).active&&list().at(0).pending_restart);
    assert(enable(graphicsView.id,false,error));assert(remove(graphicsView.id,error));
    std::ofstream(graphics/"0000000000000001_0000000000000002_ps.txt")<<"#version 420\nvoid main(){}\n";
    assert(install(graphics.string(),error));assert(!list().at(0).compatible);
    assert(!enable(list().at(0).id,true,error));assert(remove(list().at(0).id,error));
    auto legacy=root/"LegacyModel";fs::create_directories(legacy/"content"/"Object");
    std::ofstream(legacy/"content"/"Object"/"test.arc")<<"synthetic model archive";
    assert(!install(legacy.string(),error));
    assert(error.find("Only Cemu graphics packs can be installed")!=std::string::npos);
    auto loose=root/"permanent_3d.pack";std::ofstream(loose)<<"SARCsynthetic-fixture";
    assert(!install(loose.string(),error));
    auto game=root/"game";fs::create_directories(game/"content"/"Common"/"Layout");
    std::ofstream(game/"content"/"Common"/"Layout"/"Title_00.szs")<<"original";
    mods::content::set_game_root(game);
    auto translation=root/"FanTranslation";fs::create_directories(translation);
    std::ofstream(translation/"permanent_2d_EuEnglish.pack")<<"SARCsynthetic translation";
    assert(!install(translation.string(),error));
    mods::content::set_game_root({});
    auto invalid=root/"CodeMod";fs::create_directories(invalid/"content");std::ofstream(invalid/"content"/"dummy")<<"fixture";std::ofstream(invalid/"patches.txt")<<"code";
    assert(!install(invalid.string(),error));
    assert(error.find("Only Cemu graphics packs can be installed")!=std::string::npos);

    auto cloudy=root/"storage"/"mods"/"Cloudy Level Pack";
    fs::create_directories(cloudy/"content"/"Common"/"Course");
    std::ofstream(cloudy/"content"/"Common"/"Course"/"1-1.szs")<<"level";
    std::ofstream(cloudy/"rules.txt")<<"[Definition]\nname = Cloudy Level Pack\ntitleIds = 0005000010101C00,0005000010101D00\ndescription = Test level pack\nversion = 6\n";
    assert(refresh(error));
    {auto v=view("Cloudy Level Pack");assert(!v.compatible);assert(v.reason.find("Only Cemu graphics packs can be installed")!=std::string::npos);}
    fs::remove_all(cloudy);

    auto large_dir=root/"LargeFile";fs::create_directories(large_dir/"content");
    auto large=large_dir/"content"/"big.bin";
    {std::ofstream(large)<<"x";}
    fs::resize_file(large,129ull*1024*1024);
    auto indexed=mods::content::index(large_dir/"content");
    assert(indexed.size()==1);
    fs::remove_all(large_dir);

    std::cout << "Package install, dependencies, and cemu lifecycle passed\n";
}
