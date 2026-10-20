#include "content.h"
#include "cemu_pack.h"
#include "guest_addr.h"
#include "mod_archive.h"
#include "mod_json.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace mods::content {
namespace fs=std::filesystem;
namespace {
Files active;
std::atomic<bool> present{false};
std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+= 'a'-'A';return s;}
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void require(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);}
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
std::string unquote(std::string s){s=trim(s);if(s.size()>=2&&s.front()=='"'&&s.back()=='"')s=s.substr(1,s.size()-2);return s;}
bool id_ok(const std::string& s){return !s.empty()&&s.size()<=64&&s[0]!='.'&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';});}
std::string slug(std::string name){
    name=lower(name);for(char& c:name)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))c='-';
    while(!name.empty()&&name.front()=='-')name.erase(name.begin());
    while(!name.empty()&&name.back()=='-')name.pop_back();
    if(name.size()>55)name.resize(55);
    return name;
}
bool is_semver(const std::string& s){
    size_t p=0;
    for(int part=0;part<3;part++){
        size_t end=part==2?s.size():s.find('.',p);
        if(end==std::string::npos||end<=p)return false;
        for(size_t j=p;j<end;j++)if(s[j]<'0'||s[j]>'9')return false;
        if(part<2)p=end+1;
        else return end==s.size();
    }
    return false;
}
struct Definition {
    std::string name,description,version_raw,titleids;
};
Definition parse_definition(const std::string& text){
    Definition out;std::istringstream lines(text);std::string line;bool in_definition=false;bool saw_section=false;
    while(std::getline(lines,line)){
        auto start=line.find_first_not_of(" \t\r");
        if(start==std::string::npos)continue;
        if(line[start]=='#')continue;
        if(line[start]=='['){
            auto end=line.find(']',start);require(end!=std::string::npos,"Malformed Cemu section");
            auto section=lower(trim(line.substr(start+1,end-start-1)));
            require(section=="definition","Only Definition metadata is supported in file-only Cemu packs");
            require(!saw_section,"Multiple Cemu definitions");
            saw_section=true;in_definition=true;continue;
        }
        require(in_definition,"Only Definition metadata is supported in file-only Cemu packs");
        auto eq=line.find('=');require(eq!=std::string::npos,"Malformed Cemu rules assignment");
        auto key=lower(trim(line.substr(0,eq)));auto value=unquote(trim(line.substr(eq+1)));
        if(key=="name")out.name=value;
        else if(key=="description")out.description=value;
        else if(key=="version")out.version_raw=value;
        else if(key=="titleids")out.titleids=value;
    }
    require(saw_section,"Only Definition metadata is supported in file-only Cemu packs");
    if(!out.titleids.empty())require(cemu::targets_title(out.titleids,g_guest_build_title_id),std::string("Cemu pack does not target this version of the game (")+g_guest_build_name+", title "+g_guest_build_title_id+")");
    return out;
}
void validate_rules_file(const fs::path& path){
    require(path.filename().string().size()&&fs::file_size(path)<=1024*1024,"Oversized Cemu rules file");
    std::ifstream f(path);std::string text{std::istreambuf_iterator<char>(f),{}};
    auto rules=lower(text);
    require(rules.find("[texture")==std::string::npos&&rules.find("[control")==std::string::npos&&rules.find("[preset")==std::string::npos,"Cemu texture/control/preset rules require an adapter; this import supports file-only packs");
    parse_definition(text);
}
bool under_ignored(const fs::path& root,const fs::path& path){
    auto rel=path.lexically_relative(root);
    for(const auto& part:rel){auto name=lower(part.string());if(name=="meta")return true;}
    return false;
}
json::Value content_manifest_value(const fs::path& stage,const std::string& folder_name,const fs::path& content_dir,const Definition& def,const std::string& mapped,const std::string& ignored){
    auto name=def.name.empty()?fs::path(folder_name).stem().string():def.name;
    require(!name.empty(),"Missing mod name");if(name.size()>128)name.resize(128);
    std::string id=folder_name;
    if(fs::path(folder_name).stem().string()!=folder_name||!id_ok(id)){
        auto base=slug(name);require(!base.empty(),"Missing mod name");
        id="content."+base;
    }
    std::string version="1.0.0";
    std::string description=def.description;
    if(!def.version_raw.empty()&&is_semver(def.version_raw))version=def.version_raw;
    else if(!def.version_raw.empty()){
        if(!description.empty())description+=" ";
        description+="Cemu pack version "+def.version_raw+".";
    }
    if(description.empty())description="Imported local content replacement. Requires restart. Model and archive compatibility must be checked in game.";
    if(!mapped.empty())description+=" Loose files placed at: "+mapped+".";
    if(!ignored.empty())description+=" Not used: "+ignored+".";
    json::Value m;m["format_version"]=2;m["id"]=id;m["name"]=name;m["version"]=version;m["game_id"]="nsmbu-usa";m["kind"]="lua";m["minimum_manager_version"]="1.0.0";
    m["content_dir"]=content_dir.lexically_relative(stage).generic_string();
    m["description"]=description;
    return m;
}
void write_content_manifest(const fs::path& stage,const std::string& folder_name,const fs::path& content_dir,const Definition& def,const std::string& mapped,const std::string& ignored){
    auto m=content_manifest_value(stage,folder_name,content_dir,def,mapped,ignored);
    std::ofstream out(stage/"manifest.json");out<<json::dump(m)<<'\n';out.close();require(bool(out),"Cannot write imported manifest");
}
}

const char* const kLanguagePacks[]={"permanent_2d_usenglish.pack","permanent_2d_usfrench.pack","permanent_2d_usspanish.pack",
    "permanent_2d_euenglish.pack","permanent_2d_eufrench.pack","permanent_2d_eugerman.pack","permanent_2d_euitalian.pack",
    "permanent_2d_euspanish.pack","permanent_2d_jpjapanese.pack"};
bool known_pack(const std::string& filename){
    auto name=lower(filename);
    for(auto allowed:{"permanent_3d.pack","first_szs_permanent.pack","szs_permanent0.pack","szs_permanent1.pack","szs_permanent2.pack"})if(name==allowed)return true;
    for(auto allowed:kLanguagePacks)if(name==allowed)return true;
    return false;
}
std::string pack_language(const std::string& filename){
    auto name=lower(filename);
    for(auto known:kLanguagePacks)if(name==known)return name.substr(15,name.size()-15-5);
    return {};
}
namespace {
fs::path game_root;

std::multimap<std::string,std::string> game_names(){
    std::multimap<std::string,std::string> out;std::error_code ec;
    if(game_root.empty())return out;
    auto content=game_root/"content";if(!fs::is_directory(content,ec))return out;
    for(fs::recursive_directory_iterator it(content,ec),end;!ec&&it!=end;it.increment(ec))
        if(it->is_regular_file(ec))out.emplace(lower(it->path().filename().string()),it->path().lexically_relative(content).generic_string());
    return out;
}
}
void set_game_root(const fs::path& game){game_root=game;}
json::Value synthesize_cemu_content(const fs::path& pack,const std::string& folder_name){
    std::vector<fs::path> candidates;
    std::error_code ec;
    for(fs::recursive_directory_iterator it(pack,ec),end;!ec&&it!=end;it.increment(ec)){
        const auto& e=*it;
        auto name=lower(e.path().filename().string());
        if(e.is_directory()){
            if(name=="meta"){it.disable_recursion_pending();continue;}
            if(name=="content")candidates.push_back(e.path());
            require(name!="code"&&name!="aoc","Only content replacements are supported; code and DLC folders cannot be imported");
            if(name.size()==16&&name.starts_with("00050000"))require(name==lower(g_guest_build_title_id),(std::string("This SDCafiine pack targets another game or region (requires title ")+g_guest_build_title_id+", "+g_guest_build_name+")").c_str());
            continue;
        }
        if(under_ignored(pack,e.path()))continue;
        auto ext=lower(e.path().extension().string());
        require(ext!=".asm","This mod includes a Cemu code patch (.asm), which this port cannot apply. To use only its content files, select its content folder instead");
        require(name!="patches.txt"&&!name.ends_with("_vs.txt")&&!name.ends_with("_ps.txt")&&ext!=".glsl"&&ext!=".rpx"&&ext!=".rpl","This mod includes unsupported code patches or shaders; import the complete mod through a future adapter");
        if(name=="rules.txt")validate_rules_file(e.path());
    }
    require(candidates.size()==1,"Select a single mod pack folder containing exactly one content/ directory");
    Definition def;
    auto rules=pack/"rules.txt";
    if(fs::is_regular_file(rules)){
        std::ifstream f(rules);std::string text{std::istreambuf_iterator<char>(f),{}};
        def=parse_definition(text);
    }
    return content_manifest_value(pack,folder_name,candidates.front(),def,{},{});
}
void import_legacy(const fs::path& stage,const std::string& source_name){
    std::vector<fs::path> candidates,loose_packs;
    Definition def;
    std::error_code ec;
    for(fs::recursive_directory_iterator it(stage,ec),end;!ec&&it!=end;it.increment(ec)){
        const auto& e=*it;
        auto name=lower(e.path().filename().string());
        if(e.is_directory()){
            if(name=="meta"){it.disable_recursion_pending();continue;}
            if(name=="content")candidates.push_back(e.path());
            require(name!="code"&&name!="aoc","Only content replacements are supported; code and DLC folders cannot be imported");
            if(name.size()==16&&name.starts_with("00050000"))require(name==lower(g_guest_build_title_id),(std::string("This SDCafiine pack targets another game or region (requires title ")+g_guest_build_title_id+", "+g_guest_build_name+")").c_str());
            continue;
        }
        if(under_ignored(stage,e.path()))continue;
        auto ext=lower(e.path().extension().string());
        if(ext==".pack")loose_packs.push_back(e.path());
        require(ext!=".asm","This mod includes a Cemu code patch (.asm), which this port cannot apply. To use only its content files, select its content folder instead");
        require(name!="patches.txt"&&!name.ends_with("_vs.txt")&&!name.ends_with("_ps.txt")&&ext!=".glsl"&&ext!=".rpx"&&ext!=".rpl","This mod includes unsupported code patches or shaders; import the complete mod through a future adapter");
        if(name=="rules.txt"){
            validate_rules_file(e.path());
            std::ifstream f(e.path());std::string text{std::istreambuf_iterator<char>(f),{}};
            def=parse_definition(text);
        }
    }

    std::string mapped,ignored;
    if(candidates.empty()){
        for(const auto& file:loose_packs)require(known_pack(file.filename().string()),"Unknown loose pack filename; use a content/ tree with its game-relative path");
        auto names=game_names();std::vector<std::pair<fs::path,std::string>> moves;
        for(const auto& e:fs::recursive_directory_iterator(stage)){
            if(under_ignored(stage,e.path()))continue;
            if(!e.is_regular_file())continue;auto name=lower(e.path().filename().string());
            if(name=="rules.txt"||name=="manifest.json")continue;
            if(lower(e.path().extension().string())==".pack"){moves.emplace_back(e.path(),"Common/Pack/"+e.path().filename().string());continue;}
            auto hits=names.equal_range(name);auto n=std::distance(hits.first,hits.second);
            require(n<=1,"A loose file matches several game files; use a content/ tree with its game-relative path");
            if(n==1)moves.emplace_back(e.path(),hits.first->second);else ignored+=(ignored.empty()?"":", ")+e.path().filename().string();
        }
        require(!moves.empty(),"Select a single mod pack folder containing exactly one content/ directory");
        for(const auto& [from,to]:moves){
            auto dest=stage/"content"/fs::path(to);require(!fs::exists(dest),"Duplicate loose file name");
            fs::create_directories(dest.parent_path());fs::rename(from,dest);mapped+=(mapped.empty()?"":", ")+to;
        }
        candidates.push_back(stage/"content");
    }
    require(candidates.size()==1,"Select a single mod pack folder containing exactly one content/ directory");
    write_content_manifest(stage,source_name,candidates.front(),def,mapped,ignored);
}
Files index(const fs::path& directory){
    require(fs::is_directory(directory),"Content folder is missing");
    require(!fs::is_symlink(directory),"Content paths may not use symlinks");
    Files out;uint64_t total=0;size_t count=0;
    for(const auto& e:fs::recursive_directory_iterator(directory)){
        require(++count<=4096,"Too many content entries");require(!e.is_symlink(),"Content files may not use symlinks");
        if(e.is_directory())continue;
        require(e.is_regular_file(),"Unsupported content file type");require(!lower(e.path().filename().string()).starts_with(".deleted_"),"SDCafiine file hiding is not supported yet");auto n=e.file_size();total+=n;
        require(n<=archive::kFileLimit&&total<=archive::kTotalLimit,"Content size limit exceeded");
        auto relative=e.path().lexically_relative(directory).generic_string();
        require(archive::relative_path(relative),"Invalid content path");
        require(out.emplace(lower(relative),e.path().string()).second,"Duplicate content path (case insensitive)");
    }
    require(!out.empty(),"Content mod contains no files");return out;
}
void activate(Files files){require(!present.load(),"Content overrides already activated");active=std::move(files);present.store(!active.empty(),std::memory_order_release);}
std::string replacement(const std::string& guest,const std::string& mode){
    if(!present.load(std::memory_order_acquire))return {};
    if(mode!="r"&&mode!="rb")return {};
    auto relative=guest;
    if(relative.starts_with("/vol/content/"))relative.erase(0,13);
    else if(relative.empty()||relative.front()=='/')return {};
    if(!archive::relative_path(relative))return {};
    auto key=lower(relative);auto it=active.find(key);if(it!=active.end())return it->second;

    auto slash=key.rfind('/');auto file=slash==std::string::npos?key:key.substr(slash+1);
    auto language=pack_language(file);
    if(!language.empty()&&(key=="pack/"+file||key.ends_with("/pack/"+file))){
        std::vector<std::string> names{file};
        for(auto region:{"us","eu","jp"}){auto alt=std::string("permanent_2d_")+region+language+".pack";if(alt!=file)names.push_back(alt);}
        for(const auto& name:names){
            if(auto j=active.find("common/pack/"+name);j!=active.end()&&("common/pack/"+name)!=key){
                static std::atomic<bool> logged{false};
                if(!logged.exchange(true))fprintf(stderr,"[mod-manager] %s is read from the content mod's %s\n",relative.c_str(),j->first.c_str());
                return j->second;
            }
        }
    }
    return {};
}
}
