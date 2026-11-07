#include "packages.h"
#include "mod_archive.h"
#include "content.h"
#include "cemu_pack.h"
#include "../platform/host.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

#include "core/env.h"
namespace mods::packages {
namespace {
namespace fs=std::filesystem;
using json::Value;
struct Requirement {std::string id,version;};
struct Manifest {
    std::string id,name,version,author,description,kind,problem;
    std::vector<Requirement> dependencies;
    std::vector<std::string> conflicts;
    std::vector<Option> options;
    std::shared_ptr<cemu::Pack> graphics;
};
struct Record {Manifest manifest;fs::path path;bool active=false,loading=false;std::string status,error;Value startup_config;};
std::mutex mutex;
std::map<std::string,Record> records;
Value database;
std::string open_pack;
fs::path root;
bool ready=false;
std::string last_problem;
std::atomic<bool> dirty{false};
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
bool id_ok(const std::string& s){return !s.empty()&&s.size()<=64&&s[0]!='.'&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';});}
std::array<unsigned,3> version(const std::string& s) {
    std::array<unsigned,3> result{};size_t p=0;
    for(int i=0;i<3;i++){size_t end=i==2?s.size():s.find('.',p);require(end!=std::string::npos&&end>p,"Version must be major.minor.patch");auto parse=std::from_chars(s.data()+p,s.data()+end,result[i]);require(parse.ec==std::errc()&&parse.ptr==s.data()+end,"Invalid version");p=end+1;}
    return result;
}
std::string string_field(const Value& v,const char* key,bool optional=false,size_t limit=8192){const auto& f=v.get(key);if(optional&&f.type==Value::Null)return {};require(f.type==Value::String&&f.text.size()<=limit&&f.text.find('\0')==std::string::npos,"Invalid field: "+std::string(key));return f.text;}

std::string read_text(const fs::path& p){require(fs::is_regular_file(p)&&!fs::is_symlink(p)&&fs::file_size(p)<=1024*1024,"Missing or oversized JSON file: "+p.filename().string());std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
double snap_number(const Option& o,double value){
    if(!(o.step>0)||!std::isfinite(o.step)||!std::isfinite(value))return value;
    value=o.minimum+std::round((value-o.minimum)/o.step)*o.step;
    if(value<o.minimum)value=o.minimum;
    if(value>o.maximum)value=o.maximum;
    return value;
}
bool valid_option(const Option& o,const Value& v){
    if(o.type=="bool")return v.type==Value::Bool;
    if(o.type=="number")return v.type==Value::Number&&std::isfinite(v.number)&&v.number>=o.minimum&&v.number<=o.maximum;
    if(o.type=="string")return v.type==Value::String&&v.text.size()<=1024&&v.text.find('\0')==std::string::npos;
    if(o.type=="enum"||o.type=="radio")return v.type==Value::String&&std::find(o.choices.begin(),o.choices.end(),v.text)!=o.choices.end();
    return false;
}
bool content_lifecycle(const Manifest& m){return m.kind=="cemu";}
Manifest manifest(const fs::path& path,Value v){
    require(v.type==Value::Object,"Manifest must be an object");
    Manifest m;m.id=string_field(v,"id",false,64);require(id_ok(m.id),"Invalid mod ID");
    m.name=string_field(v,"name",false,128);require(!m.name.empty(),"Mod name is empty");
    m.version=string_field(v,"version",false,64);version(m.version);
    m.author=string_field(v,"author",true,256);m.description=string_field(v,"description",true);
    m.kind=string_field(v,"kind",false,32);
    require(m.kind!="guest","Guest mods are not supported");
    require(m.kind!="native","Native mods are not supported");
    require(m.kind!="settings","Settings mods are not supported");
    require(m.kind!="content","Content mods are not supported");
    require(m.kind=="cemu","Unsupported mod kind");
    const auto& fv=v.get("format_version");
    require(fv.type==Value::Number&&fv.number==1,"Unsupported manifest format");
    if(string_field(v,"game_id",false,64)!=kGameId)m.problem="This package targets another game";
    auto minimum=string_field(v,"minimum_manager_version",true,64);
    if(!minimum.empty()&&version(minimum)>version(kManagerVersion))m.problem="Requires mod manager "+minimum;
    if(m.kind=="cemu") {
        auto folder=string_field(v,"cemu_dir",false,512);
        require(folder.empty()||archive::relative_path(folder),"Invalid Cemu directory");
        auto checked=path;for(const auto& part:fs::path(folder)){checked/=part;require(!fs::is_symlink(checked),"Cemu paths may not use symlinks");}
        m.graphics=std::make_shared<cemu::Pack>(cemu::parse(path/folder));
        auto schema=cemu::options(*m.graphics);if(v.get("options").type==Value::Null)v["options"]=schema;else require(v.get("options")==schema,"Cemu preset options do not match rules.txt");
    }
    const auto& deps=v.get("dependencies");require(deps.type==Value::Null||deps.type==Value::Array,"Dependencies must be an array");
    for(const auto& dep:deps.array){Requirement r;r.id=string_field(dep,"id",false,80);r.version=string_field(dep,"minimum_version",true,64);if(r.version.empty())r.version="0.0.0";version(r.version);require(!r.id.starts_with("builtin:"),"Unknown built-in mod: "+r.id);require(id_ok(r.id),"Invalid dependency ID");m.dependencies.push_back(r);}
    const auto& conflicts=v.get("conflicts");require(conflicts.type==Value::Null||conflicts.type==Value::Array,"Conflicts must be an array");
    for(const auto& c:conflicts.array){require(c.type==Value::String,"Invalid conflict ID");require(!c.text.starts_with("builtin:"),"Unknown built-in mod: "+c.text);require(id_ok(c.text),"Invalid conflict ID");m.conflicts.push_back(c.text);}
    const auto& options=v.get("options");require(options.type==Value::Null||(options.type==Value::Array&&options.array.size()<=32),"Invalid options");std::set<std::string> option_ids;
    for(const auto& option:options.array){
        Option o;o.id=string_field(option,"id",false,64);require(id_ok(o.id)&&option_ids.insert(o.id).second,"Invalid or duplicate option ID");o.name=string_field(option,"name",false,128);o.description=string_field(option,"description",true);o.type=string_field(option,"type",false,32);o.default_value=option.get("default");
        if(o.type=="number"){const auto& lo=option.get("min");const auto& hi=option.get("max");const auto& step=option.get("step");require(lo.type==Value::Number&&hi.type==Value::Number&&lo.number<=hi.number,"Invalid numeric bounds");o.minimum=lo.number;o.maximum=hi.number;o.step=step.type==Value::Number?step.number:1;require(o.step>0&&std::isfinite(o.step),"Invalid numeric step");}
        if(o.type=="enum"||o.type=="radio"){const auto& choices=option.get("choices");require(choices.type==Value::Array&&!choices.array.empty()&&choices.array.size()<=64,"Invalid enum choices");for(const auto& c:choices.array){require(c.type==Value::String&&c.text.size()<=128,"Invalid enum value");o.choices.push_back(c.text);}}
        if(o.type=="radio"){const auto& labels=option.get("labels");require(labels.type==Value::Array&&labels.array.size()==o.choices.size(),"Invalid radio labels");for(const auto& label:labels.array){require(label.type==Value::String&&!label.text.empty()&&label.text.size()<=128,"Invalid radio label");o.labels.push_back(label.text);}}
        require(valid_option(o,o.default_value),"Invalid default for "+o.id);
        if(o.type=="number")o.default_value.number=snap_number(o,o.default_value.number);
        o.value=o.default_value;m.options.push_back(o);
    }
    return m;
}
Manifest manifest(const fs::path& path){return manifest(path,json::parse(read_text(path/"manifest.json")));}
bool wanted(const std::string& id){const auto& v=database.get("enabled").get(id);return v.type==Value::Bool&&v.boolean;}

Value config(const Manifest& m){Value out;out.type=Value::Object;for(const auto& o:m.options){const auto& saved=database.get("config").get(m.id).get(o.id);Value v=valid_option(o,saved)?saved:o.default_value;if(o.type=="number"&&v.type==Value::Number)v.number=snap_number(o,v.number);out[o.id]=v;}return out;}
void strip_legacy(){database.object.erase("active");database.object.erase("profiles");database.object.erase("builtins");database.object.erase("builtin_options");}
void save(){if(!ready)return;strip_legacy();fs::create_directories(root);auto tmp=root/"mods.json.tmp";std::ofstream f(tmp,std::ios::binary|std::ios::trunc);f<<json::dump(database,2)<<'\n';f.close();require(bool(f)&&host::replace_file(tmp.string(),(root/"mods.json").string()),"Cannot save mods.json");std::error_code ec;fs::remove(root/"profiles.json",ec);}
void defaults(){database=Value{};database["format_version"]=1;database["hot_reload"]=false;database["enabled"].type=Value::Object;}
void normalize_database(){
    require(database.get("format_version").type==Value::Number&&database.get("format_version").number==1,"Unsupported mods.json format");
    if(database.get("profiles").type==Value::Object&&!database.get("profiles").object.empty()){
        auto active=database.get("active").string("Default");
        if(!database.get("profiles").object.contains(active))active=database.get("profiles").object.begin()->first;
        const auto& src=database.get("profiles").get(active);
        if(database.get("enabled").type!=Value::Object){
            database["enabled"]=src.get("enabled");
            if(database.get("enabled").type!=Value::Object)database["enabled"].type=Value::Object;
        }
        if(database.get("config").type!=Value::Object){
            database["config"]=src.get("config");
            if(database.get("config").type!=Value::Object)database["config"].type=Value::Object;
        }
    }
    if(database.get("enabled").type!=Value::Object)database["enabled"].type=Value::Object;
    if(database.get("config").type!=Value::Object)database["config"].type=Value::Object;
    if(database.get("hot_reload").type!=Value::Bool)database["hot_reload"]=false;
    strip_legacy();
}
bool looks_like_graphics_rules(const std::string& text){
    auto rules=text;for(char& c:rules)if(c>='A'&&c<='Z')c+='a'-'A';
    return rules.find("[preset]")!=std::string::npos||rules.find("[textureredefine]")!=std::string::npos||rules.find("[control]")!=std::string::npos||rules.find("[default]")!=std::string::npos;
}
bool graphics_tree(const fs::path& path){
    std::error_code ec;
    for(const auto& file:fs::recursive_directory_iterator(path,ec)){
        if(ec||!file.is_regular_file())continue;
        auto name=file.path().filename().string();
        if(name.ends_with("_vs.txt")||name.ends_with("_ps.txt"))return true;
        if(name=="rules.txt"&&looks_like_graphics_rules(read_text(file.path())))return true;
    }
    return false;
}
Manifest load_manifest(const fs::path& path,const std::string& folder){
    if(fs::exists(path/"manifest.json"))return manifest(path);
    require(graphics_tree(path),"Only Cemu graphics packs can be installed");
    return manifest(path,cemu::synthesize_legacy_manifest(path,folder));
}
void scan(){records.clear();if(!ready)return;fs::create_directories(root/"mods");for(const auto& e:fs::directory_iterator(root/"mods")){std::error_code ec;auto folder=e.path().filename().string();if(!e.is_directory(ec)||ec||folder.empty()||folder[0]=='.')continue;Record r;r.path=fs::weakly_canonical(e.path(),ec);if(ec||r.path.empty())r.path=e.path();try{r.manifest=load_manifest(r.path,folder);}catch(const std::exception& ex){r.manifest.id=folder;r.manifest.name=folder;r.manifest.problem=ex.what();}if(records.contains(r.manifest.id)){if(r.manifest.problem.empty())r.manifest.problem="Duplicate package ID";if(r.manifest.id==folder)continue;r.manifest.id=folder;}records.emplace(r.manifest.id,std::move(r));}}
std::vector<std::string> order(const std::set<std::string>& enabled){
    std::map<std::string,int> mark;std::vector<std::string> result;
    std::function<void(const std::string&)> visit=[&](const std::string& id){require(mark[id]!=1,"Dependency cycle at "+id);if(mark[id]==2)return;mark[id]=1;auto it=records.find(id);require(it!=records.end(),"Missing dependency: "+id);const auto& m=it->second.manifest;require(m.problem.empty(),m.name+": "+m.problem);for(const auto& dep:m.dependencies){auto d=records.find(dep.id);require(d!=records.end(),"Missing dependency: "+dep.id);require(version(d->second.manifest.version)>=version(dep.version),"Dependency "+dep.id+" needs version "+dep.version);require(enabled.contains(dep.id),"Dependency is disabled: "+dep.id);visit(dep.id);}mark[id]=2;result.push_back(id);};
    for(const auto& id:enabled)visit(id);return result;
}
std::set<std::string> enabled_set(){std::set<std::string> result;for(const auto& [id,r]:records)if(wanted(id))result.insert(id);return result;}
void validate_conflicts(const std::set<std::string>& enabled){
    std::vector<cemu::Selection> graphics;
    for(const auto& id:enabled){const auto& m=records.at(id).manifest;if(m.graphics)graphics.push_back({id,*m.graphics,config(m)});}
    cemu::validate(graphics);
    for(const auto& id:enabled){const auto& m=records.at(id).manifest;for(const auto& conflict:m.conflicts){require(!enabled.contains(conflict),m.name+" conflicts with "+conflict);}}
}
template<class Fn> bool operation(std::string& error,Fn fn){try{std::lock_guard guard(mutex);require(ready,"Mod manager storage is unavailable");fn();error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}}
}


void set_open_pack(std::string pack){std::lock_guard guard(mutex);open_pack=std::move(pack);}
std::string open_pack_value(){std::lock_guard guard(mutex);return open_pack;}
void reinitialize(){
    {
        std::lock_guard guard(mutex);
        for(auto& [id,r]:records){
            (void)id;
            r.active=false;
            r.loading=false;
            r.status.clear();
            r.error.clear();
        }
        content::deactivate();
        cemu::deactivate();
        ready=false;
        last_problem.clear();
        dirty=true;
    }
    initialize();
}
void initialize(){
    std::lock_guard guard(mutex);if(ready)return;const char* override=env::MOD_MANAGER_DIR();if(env::NO_HOST_INPUT()&&!override)return;
    root=override?fs::path(override):fs::path(host::config_dir());
    ready=true;defaults();
    try{fs::create_directories(root);auto path=root/"mods.json";if(!fs::exists(path)&&fs::exists(root/"profiles.json"))path=root/"profiles.json";if(fs::exists(path)){database=json::parse(read_text(path));normalize_database();}
        scan();
        try { auto enabled=enabled_set();auto sequence=order(enabled);validate_conflicts(enabled);
            std::vector<cemu::Selection> graphics;
            for(const auto& id:sequence){auto& record=records.at(id);if(record.manifest.graphics&&(record.manifest.graphics->shaders.empty()||cemu::vulkan())){
                record.startup_config=config(record.manifest);graphics.push_back({id,*record.manifest.graphics,record.startup_config});
                record.active=true;record.status=std::to_string(record.manifest.graphics->textures.size())+" texture rules, "+std::to_string(record.manifest.graphics->shaders.size())+" shader candidates";
            }}
            cemu::activate(graphics);
            content::deactivate();
        } catch(const std::exception& e) {last_problem=e.what();}
        dirty=true;
    }catch(const std::exception& e){last_problem=e.what();ready=false;records.clear();fprintf(stderr,"[mod-manager] %s\n",e.what());}
}
std::string directory(){std::lock_guard guard(mutex);return ready?(root/"mods").string():"";}
std::vector<View> list(){std::lock_guard guard(mutex);std::vector<View> out;for(const auto& [id,r]:records){const auto& m=r.manifest;View v;v.id=id;v.name=m.name;v.version=m.version;v.author=m.author;v.description=m.description;v.kind=m.kind;v.restart_required=content_lifecycle(m);v.enabled=wanted(id);v.active=r.active;v.compatible=m.problem.empty();v.reason=m.problem.empty()?r.error:m.problem;v.status=r.status;if(m.graphics){auto diagnostics=cemu::runtime_status(id);if(!diagnostics.empty())v.status+=". "+diagnostics;}v.options=m.options;auto cfg=config(m);v.pending_restart=v.restart_required&&(v.enabled!=v.active||(m.graphics&&v.active&&!(r.startup_config==cfg)));if(m.graphics&&!m.graphics->shaders.empty()&&!cemu::vulkan()){v.active=false;v.compatible=false;v.reason="GLSL shader packs require Vulkan; choose it in Graphics and restart";}for(auto& o:v.options)o.value=cfg.get(o.id);for(const auto& dep:m.dependencies)v.dependencies.push_back(dep.id+">="+dep.version);v.conflicts=m.conflicts;out.push_back(std::move(v));}return out;}
bool install(const std::string& source,std::string& error,std::string* installed_id_out){return operation(error,[&]{
    auto nonce=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());auto stage=root/(".stage-"+nonce),backup=root/(".backup-"+nonce);fs::path target;bool backed=false,moved=false;
    try{archive::stage(fs::path(source),stage);if(!fs::exists(stage/"manifest.json")){
        require(graphics_tree(stage),"Only Cemu graphics packs can be installed");
        auto named=fs::path(source).lexically_normal();if(named.filename().empty())named=named.parent_path();
        cemu::import_legacy(stage,named.filename().string());
    }auto m=manifest(stage);require(m.problem.empty()||m.problem.starts_with("GLSL shader packs require Vulkan"),m.problem);target=root/"mods"/m.id;auto it=records.find(m.id);require(it==records.end()||(!wanted(m.id)&&!it->second.active&&!it->second.loading),it!=records.end()&&content_lifecycle(it->second.manifest)?"Disable this content mod and restart before updating":"Disable this mod and wait for it to unload before updating");fs::create_directories(target.parent_path());if(fs::exists(target)){fs::rename(target,backup);backed=true;}fs::rename(stage,target);moved=true;m=manifest(target);Record r;r.path=target;r.manifest=std::move(m);auto installed_id=r.manifest.id;records[installed_id]=std::move(r);if(installed_id_out)*installed_id_out=installed_id;if(backed){std::error_code cleanup;fs::remove_all(backup,cleanup);}dirty=true;
    }catch(...){if(moved)fs::remove_all(target);if(backed)fs::rename(backup,target);if(fs::exists(stage))fs::remove_all(stage);throw;}
});}
bool remove(const std::string& id,std::string& error){return operation(error,[&]{auto it=records.find(id);require(it!=records.end(),"Mod not found");require(!wanted(id)&&!it->second.active&&!it->second.loading,content_lifecycle(it->second.manifest)?"Disable this content mod and restart before removing":"Disable this mod and wait for it to unload before removing");for(const auto& [other,r]:records)if(wanted(other))for(const auto& d:r.manifest.dependencies)require(d.id!=id,r.manifest.name+" depends on this mod");auto previous=database;database["enabled"].object.erase(id);database["config"].object.erase(id);try{save();}catch(...){database=previous;throw;}fs::remove_all(it->second.path);records.erase(it);dirty=true;});}
bool enable(const std::string& id,bool on,std::string& error){return operation(error,[&]{require(records.contains(id),"Mod not found");auto enabled=enabled_set();if(on){
    std::set<std::string> visiting;
    std::function<void(const std::string&)> add=[&](const std::string& current){require(!visiting.contains(current),"Dependency cycle at "+current);require(records.contains(current),"Missing dependency: "+current);const auto& graphics=records.at(current).manifest.graphics;if(graphics&&!graphics->shaders.empty())require(cemu::vulkan(),"GLSL shader packs require Vulkan; choose it in Graphics and restart");if(enabled.contains(current))return;visiting.insert(current);for(const auto& d:records.at(current).manifest.dependencies)add(d.id);visiting.erase(current);enabled.insert(current);};add(id);
    }else enabled.erase(id);order(enabled);validate_conflicts(enabled);auto previous=database;for(const auto& [key,r]:records)database["enabled"][key]=enabled.contains(key);try{save();}catch(...){database=previous;throw;}for(const auto& key:enabled)records.at(key).error.clear();dirty=true;});}
bool configure(const std::string& id,const std::string& option_id,const Value& value,std::string& error){return operation(error,[&]{require(records.contains(id),"Mod not found");auto& opts=records.at(id).manifest.options;auto it=std::find_if(opts.begin(),opts.end(),[&](const Option& o){return o.id==option_id;});require(it!=opts.end()&&valid_option(*it,value),"Invalid configuration value");Value stored=value;if(it->type=="number"&&stored.type==Value::Number)stored.number=snap_number(*it,stored.number);auto previous=database;database["config"][id][option_id]=stored;try{if(records.at(id).manifest.graphics)cemu::validate({{id,*records.at(id).manifest.graphics,config(records.at(id).manifest)}});if(wanted(id))validate_conflicts(enabled_set());save();}catch(...){database=previous;throw;}dirty=true;});}
void disable_all(){std::string ignored;operation(ignored,[&]{auto previous=database;for(const auto& [id,r]:records)database["enabled"][id]=false;try{save();}catch(...){database=previous;throw;}dirty=true;});}
bool refresh(std::string& error){return operation(error,[&]{for(const auto& [id,r]:records)require(!r.active&&!r.loading&&!wanted(id),content_lifecycle(r.manifest)?"Disable content mods and restart before rescanning":"Disable installed mods before rescanning");scan();dirty=true;});}
void frame(uint64_t){}
void scene_change(uint32_t){}
}
