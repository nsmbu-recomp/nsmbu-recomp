#pragma once
#include "core/env.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <mach-o/ldsyms.h>
#include <mach-o/dyld.h>
#include <climits>
#include <pthread/qos.h>
#elif !defined(_WIN32)
#include <sys/resource.h>
#include <sys/syscall.h>
#endif
namespace host {
#if defined(__APPLE__) && defined(NSMBU_HAS_VULKAN)

void with_autorelease_pool(void (*fn)());
void with_autorelease_pool(void (*fn)(void*), void* context);
#else
inline void with_autorelease_pool(void (*fn)()) { fn(); }
inline void with_autorelease_pool(void (*fn)(void*), void* context) { fn(context); }
#endif

template<class Fn> void with_autorelease_pool(Fn&& fn) {
 auto call = [&] { fn(); };
 with_autorelease_pool([](void* context) {
  (*static_cast<decltype(call)*>(context))();
 }, &call);
}
inline thread_local std::string thread_label;
inline void set_thread_name(const char* name) {
 thread_label=name;
#ifdef __APPLE__
 pthread_setname_np(name);
#elif defined(_WIN32)
 using SetDescription=HRESULT(WINAPI*)(HANDLE,PCWSTR);
 auto f=(SetDescription)GetProcAddress(GetModuleHandleW(L"Kernel32.dll"),"SetThreadDescription");
 if(f) { std::wstring text; for(unsigned char c:thread_label)text.push_back(c); f(GetCurrentThread(),text.c_str()); }
#else
 pthread_setname_np(pthread_self(),thread_label.substr(0,15).c_str());
#endif
}

inline void boost_thread_priority() {
 if(env::NO_QOS()) return;
#ifdef __APPLE__
 pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
#elif defined(_WIN32)
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);

 struct PowerThrottling { ULONG Version, ControlMask, StateMask; };
 using SetInfo=BOOL(WINAPI*)(HANDLE,int,LPVOID,DWORD);
 static const auto set_info=(SetInfo)GetProcAddress(GetModuleHandleW(L"Kernel32.dll"),"SetThreadInformation");
 if(set_info) {
  PowerThrottling state{1 ,1 ,0 };
  set_info(GetCurrentThread(),3 ,&state,sizeof state);
 }
#else
 setpriority(PRIO_PROCESS,(id_t)syscall(SYS_gettid),-5);
#endif
}
inline void get_thread_name(char* out,size_t size) {
 if(!size)return;
#ifdef __APPLE__
 pthread_getname_np(pthread_self(),out,size);
#else
 snprintf(out,size,"%s",thread_label.empty()?"host":thread_label.c_str());
#endif
}
inline uintptr_t executable_base() {
#ifdef __APPLE__
 return (uintptr_t)&_mh_execute_header;
#elif defined(_WIN32)
 return (uintptr_t)GetModuleHandleW(nullptr);
#else
 static int anchor;
 Dl_info info{}; return dladdr(&anchor,&info)?(uintptr_t)info.dli_fbase:0;
#endif
}
inline std::string executable_path() {
#ifdef _WIN32
 char path[32768]; DWORD n=GetModuleFileNameA(nullptr,path,sizeof path);return std::string(path,n);
#elif !defined(__APPLE__)
 char path[4096];ssize_t n=readlink("/proc/self/exe",path,sizeof path);return n>0?std::string(path,n):std::string();
#else
 return {};
#endif
}
inline size_t page_size() {
#ifdef _WIN32
 SYSTEM_INFO info;GetSystemInfo(&info);return info.dwPageSize;
#else
 return (size_t)getpagesize();
#endif
}
inline bool memory_touched(void* p,size_t bytes) {
#ifdef _WIN32

 size_t n=(bytes+page_size()-1)/page_size(); std::vector<void*> pages(n);
 ULONG_PTR count=n; DWORD granularity=0;
 if(GetWriteWatch(0,p,bytes,pages.data(),&count,&granularity)!=0)return true;
 return count!=0;
#else
 std::vector<char> pages((bytes+page_size()-1)/page_size());
#ifdef __APPLE__
 char* vec=pages.data();
#else
 unsigned char* vec=reinterpret_cast<unsigned char*>(pages.data());
#endif
 if(mincore(p,bytes,vec))return true;
 for(auto page:pages)if(page!=0)return true;
 return false;
#endif
}
inline bool memory_resident_supported() {
#ifdef _WIN32
 return false;
#else
 static const bool on=env::STATE_RESIDENT_OFF()==nullptr;
 return on;
#endif
}
inline int memory_resident(void* p,size_t bytes,unsigned char* out) {
#ifdef _WIN32
 (void)p;(void)bytes;(void)out;return -1;
#elif defined(__APPLE__)
 return mincore(p,bytes,(char*)out);
#else
 return mincore(p,bytes,out);
#endif
}
inline bool replace_file(const std::string& from,const std::string& to) {
#ifdef _WIN32
 return MoveFileExA(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 return rename(from.c_str(),to.c_str())==0;
#endif
}

inline std::string exe_dir() {
 static const std::string dir=[]{
  std::string p;
#if defined(__APPLE__)
  char buf[4096]; uint32_t n=sizeof buf;
  if(_NSGetExecutablePath(buf,&n)==0){ char real[PATH_MAX]; p=realpath(buf,real)?real:buf; }
#elif defined(_WIN32)
  char buf[MAX_PATH*4]; DWORD n=GetModuleFileNameA(nullptr,buf,sizeof buf); if(n>0&&n<sizeof buf) p.assign(buf,n);
#else
  char buf[4096]; ssize_t n=readlink("/proc/self/exe",buf,sizeof buf-1); if(n>0) p.assign(buf,(size_t)n);
#endif
  size_t s=p.find_last_of("/\\");
  return s==std::string::npos?std::string():p.substr(0,s);
 }();
 return dir;
}
inline const std::string& portable_user_dir() {
 static const std::string dir=[]{
  std::string e=exe_dir();
  if(!e.empty()){
   FILE* f=fopen((e+"/portable.txt").c_str(),"rb");
   if(f){
    fclose(f);
    size_t s=e.find_last_of("/\\");
    return (s==std::string::npos?e:e.substr(0,s))+"/nsmbu";
   }
  }
  if(const char* override=env::USER_DIR())return std::string(override);
  return std::string();
 }();
 return dir;
}
inline bool portable() { return !portable_user_dir().empty(); }
inline std::string config_dir() {
 if(portable()) return portable_user_dir();
#if defined(__APPLE__) && TARGET_OS_IPHONE
 // in Documents, so the logs and settings show in the Files app
 const char* home=getenv("HOME");return std::string(home?home:".")+"/Documents/NSMBU";
#elif defined(__APPLE__)
 const char* home=getenv("HOME");return std::string(home?home:".")+"/Library/Application Support/NSMBU";
#elif defined(_WIN32)
 const char* root=getenv("APPDATA");return std::string(root?root:".")+"/NSMBU";
#else
 if(const char* xdg=getenv("XDG_CONFIG_HOME"))return std::string(xdg)+"/nsmbu";
 const char* home=getenv("HOME");return std::string(home?home:".")+"/.config/nsmbu";
#endif
}
inline std::string log_dir() { return config_dir() + "/logs"; }
inline void ensure_log_dir() {
 std::error_code ec;
 std::filesystem::create_directories(log_dir(), ec);
}
}
