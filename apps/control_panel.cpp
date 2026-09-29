#include "../src/config/settings.hpp"
#include "iasiodrv.h"
#include <shellapi.h>
#include <cwchar>
namespace piaoip { HMODULE g_module=nullptr; std::atomic<long> g_objects{0}; }
namespace {
constexpr wchar_t kWindowClass[]=L"PiAoipTrayWindowV2";
constexpr UINT kTray=WM_APP+1,kDevice=WM_APP+2;
constexpr int kOpen=1,kExit=2;
HANDLE stop_event=nullptr;
std::thread monitor;
NOTIFYICONDATAW icon{};
bool present=false,connected=false,panel_open=false,quit_pending=false;
HWND tray_window=nullptr;
UINT taskbar_created=0;
HMODULE driver_module=nullptr;
IASIO* driver=nullptr;
void trace(const char* event,bool found,unsigned error=0) {
  if(const char* path=std::getenv("PIAOIP_TRAY_LOG")) if(FILE* output=std::fopen(path,"a")) {
    std::fprintf(output,"%s state=%u error=%u\n",event,found ? 1 : 0,error); std::fclose(output);
  }
}
HICON create_icon() {
  BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth=32; info.bmiHeader.biHeight=-32; info.bmiHeader.biPlanes=1;
  info.bmiHeader.biBitCount=32;
  uint32_t* pixels=nullptr;
  HBITMAP color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
  if(!color) return nullptr;
  for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x) pixels[y*32+x]=0xff1c4d92u;
  const int wave[]={16,16,16,9,23,16,16,6,26,16,16,11,21,16,16,16};
  for(int i=0;i<15;++i) for(int row=std::min(wave[i],wave[i+1]);row<=std::max(wave[i],wave[i+1]);++row)
    for(int thickness=0;thickness<2;++thickness) pixels[row*32+2+i*2+thickness]=0xffffffffu;
  uint8_t mask_bits[128]{}; HBITMAP mask=CreateBitmap(32,32,1,1,mask_bits);
  ICONINFO data{TRUE,0,0,mask,color}; HICON result=CreateIconIndirect(&data);
  DeleteObject(mask); DeleteObject(color); return result;
}
bool load_driver() {
  if(driver_module) { FreeLibrary(driver_module); driver_module=nullptr; }
  wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr,path,MAX_PATH);
  std::wstring file(path); auto slash=file.find_last_of(L"\\/");
  if(slash!=std::wstring::npos) driver_module=LoadLibraryW((file.substr(0,slash+1)+L"PiAoipAsio.dll").c_str());
  if(driver_module) {
    using GetClass=HRESULT(__stdcall*)(REFCLSID,REFIID,void**);
    auto get_class=reinterpret_cast<GetClass>(GetProcAddress(driver_module,"DllGetClassObject"));
    IClassFactory* factory=nullptr;
    if(get_class && SUCCEEDED(get_class(piaoip::kClsid,IID_IClassFactory,reinterpret_cast<void**>(&factory)))) {
      HRESULT result=factory->CreateInstance(nullptr,piaoip::kClsid,reinterpret_cast<void**>(&driver));
      factory->Release(); if(SUCCEEDED(result)) return true;
    }
    FreeLibrary(driver_module); driver_module=nullptr;
  }
  return SUCCEEDED(CoCreateInstance(piaoip::kClsid,nullptr,CLSCTX_INPROC_SERVER,piaoip::kClsid,reinterpret_cast<void**>(&driver)));
}
void show_panel() {
  if(panel_open) return;
  panel_open=true;
  if(driver) { driver->Release(); driver=nullptr; }
  if(load_driver()) driver->controlPanel();
  else MessageBoxW(nullptr,L"Установите PiAoIP или поместите PiAoipAsio.dll рядом с PiAoipControl.exe.",L"PiAoIP",MB_OK|MB_ICONERROR);
  panel_open=false;
  if(quit_pending) PostMessageW(tray_window,WM_CLOSE,0,0);
}
void update_icon() {
  if(connected) {
    if(!present) { present=Shell_NotifyIconW(NIM_ADD,&icon)!=FALSE; trace("NIM_ADD",present,GetLastError()); icon.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&icon); }
    else Shell_NotifyIconW(NIM_MODIFY,&icon);
  } else if(present) { Shell_NotifyIconW(NIM_DELETE,&icon); present=false; }
}
void device_monitor(HWND window) {
  std::string cached;
  while(WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0) {
    piaoip::Config config; piaoip::read_config(config);
    piaoip::SettingsDialog::Device device;
    bool found=false;
    const bool explicit_peer=config.peer[0] && std::strcmp(config.peer,"auto");
    if(explicit_peer) cached=config.peer;
    if(!cached.empty()) found=piaoip::query_device(cached.c_str(),device,100);
    if(!found && !explicit_peer) {
      cached.clear();
      piaoip::SettingsDialog state; piaoip::discover_devices(state);
      if(state.devices.size()==1) { device=state.devices[0]; cached=device.ip; found=true; }
    }
    trace(config.peer,found);
    auto* result=new piaoip::SettingsDialog::Device(device);
    if(!PostMessageW(window,kDevice,found ? 1 : 0,reinterpret_cast<LPARAM>(result))) delete result;
    if(WaitForSingleObject(stop_event,2000)!=WAIT_TIMEOUT) break;
  }
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  if(message==taskbar_created && taskbar_created) { present=false; update_icon(); return 0; }
  if(message==kDevice) {
    std::unique_ptr<piaoip::SettingsDialog::Device> device(reinterpret_cast<piaoip::SettingsDialog::Device*>(lparam));
    connected=wparam!=0;
    if(connected) std::swprintf(icon.szTip,128,L"PiAoIP · %u×%u · %u Hz · PCM%u",device->channels,device->outputs,device->rate,device->bits);
    update_icon(); return 0;
  }
  if(message==kTray) {
    const auto event=LOWORD(lparam);
    if(event==NIN_SELECT || event==NIN_KEYSELECT || event==WM_LBUTTONDBLCLK) PostMessageW(window,WM_COMMAND,kOpen,0);
    if(event==WM_CONTEXTMENU || event==WM_RBUTTONUP) {
      HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,kOpen,L"Настройки PiAoIP…");
      AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,kExit,L"Закрыть значок в трее");
      POINT at{}; GetCursorPos(&at); SetForegroundWindow(window);
      UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,at.x,at.y,0,window,nullptr);
      DestroyMenu(menu); if(command) PostMessageW(window,WM_COMMAND,command,0); PostMessageW(window,WM_NULL,0,0);
    }
    return 0;
  }
  if(message==WM_COMMAND) {
    if(LOWORD(wparam)==kOpen) show_panel(); if(LOWORD(wparam)==kExit) PostMessageW(window,WM_CLOSE,0,0); return 0;
  }
  if(message==WM_CLOSE) {
    if(panel_open) {
      quit_pending=true;
      EnumThreadWindows(GetCurrentThreadId(),[](HWND child,LPARAM)->BOOL {
        wchar_t title[128]{}; GetWindowTextW(child,title,128);
        if(!std::wcscmp(title,L"Pi AoIP configuration")) PostMessageW(child,WM_COMMAND,IDCANCEL,0);
        return TRUE;
      },0);
      return 0;
    }
    SetEvent(stop_event); if(monitor.joinable()) monitor.join();
    if(present) Shell_NotifyIconW(NIM_DELETE,&icon); DestroyWindow(window); return 0;
  }
  if(message==WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(window,message,wparam,lparam);
}
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,char* arguments,int) {
  const bool tray=std::strstr(arguments,"--tray")!=nullptr,quit=std::strstr(arguments,"--quit")!=nullptr;
  HWND existing=FindWindowW(kWindowClass,nullptr);
  if(existing) { if(quit) PostMessageW(existing,WM_CLOSE,0,0); else if(!tray) PostMessageW(existing,WM_COMMAND,kOpen,0); return 0; }
  if(quit) return 0;
  HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\PiAoIP.Tray");
  if(!singleton || GetLastError()==ERROR_ALREADY_EXISTS) { if(singleton) CloseHandle(singleton); return 0; }
  if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) { CloseHandle(singleton); return 1; }
  piaoip::g_module=instance; WSADATA wsa{};
  if(WSAStartup(MAKEWORD(2,2),&wsa)) { CoUninitialize(); CloseHandle(singleton); return 1; }
  WNDCLASSW cls{}; cls.lpfnWndProc=window_proc; cls.hInstance=instance; cls.lpszClassName=kWindowClass;
  RegisterClassW(&cls); taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");
  HWND window=CreateWindowW(kWindowClass,L"PiAoIP Tray",0,0,0,0,0,nullptr,nullptr,instance,nullptr);
  tray_window=window;
  stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!window || !stop_event) { WSACleanup(); CoUninitialize(); CloseHandle(singleton); return 1; }
  icon.cbSize=sizeof(icon); icon.hWnd=window; icon.uID=1;
  icon.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP; icon.uCallbackMessage=kTray; icon.hIcon=create_icon();
  monitor=std::thread(device_monitor,window); if(!tray) PostMessageW(window,WM_COMMAND,kOpen,0);
  MSG message{}; while(GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
  if(driver) driver->Release(); if(driver_module) FreeLibrary(driver_module);
  if(icon.hIcon) DestroyIcon(icon.hIcon); CloseHandle(stop_event);
  WSACleanup(); CoUninitialize(); ReleaseMutex(singleton); CloseHandle(singleton); return 0;
}
