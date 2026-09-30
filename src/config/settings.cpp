#include "settings.hpp"
#include <shlobj.h>
#include <cwchar>
namespace piaoip {
namespace {
bool copy_path(const std::wstring& source, wchar_t (&path)[MAX_PATH]) {
  if (source.empty() || source.size() >= MAX_PATH) return false;
  std::wmemcpy(path, source.c_str(), source.size() + 1);
  return true;
}
bool beside_module(wchar_t (&path)[MAX_PATH]) {
  DWORD count = GetModuleFileNameW(g_module, path, MAX_PATH);
  if (!count || count >= MAX_PATH) return false;
  std::wstring value(path);
  auto slash = value.find_last_of(L"\\/");
  return slash != std::wstring::npos &&
      copy_path(value.substr(0, slash + 1) + L"PiAoipAsio.ini", path);
}
bool is_file(const wchar_t* path) {
  DWORD attributes = GetFileAttributesW(path);
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
bool ensure_parent(const wchar_t* path) {
  std::wstring value(path);
  auto slash = value.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return false;
  auto parent = value.substr(0, slash);
  int result = SHCreateDirectoryExW(nullptr, parent.c_str(), nullptr);
  return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}
void read_ini(const wchar_t* path, Config& c) {
  wchar_t peer[64]{};
  MultiByteToWideChar(CP_UTF8, 0, c.peer, -1, peer, 64);
  GetPrivateProfileStringW(L"AoIP", L"PeerIp", peer, peer, 64, path);
  if (!WideCharToMultiByte(CP_UTF8, 0, peer, -1, c.peer, sizeof(c.peer), nullptr, nullptr))
    c.peer[0] = 0;
  auto number = [&](const wchar_t* name, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(L"AoIP", name, fallback, path));
  };
  int rate = number(L"Rate", c.rate), bits = number(L"Bits", c.bits);
  int block = number(L"BufferFrames", c.block), guard = number(L"SafetyFrames", c.safety);
  if (rate > 0 && aoip::valid_rate(rate)) c.rate = rate;
  if (bits > 0 && aoip::valid_bits(bits)) c.bits = static_cast<uint16_t>(bits);
  if (block > 0 && aoip::valid_buffer(block)) c.block = block;
  if (guard >= 0 && guard <= 2048) c.safety = guard;
  // Physical I/O counts come exclusively from device discovery. Ignore old
  // Inputs/Outputs keys; use channel masks to disable individual channels.
  auto mask = [&](const wchar_t* name,uint64_t& selected) {
    wchar_t text[32]{},fallback[32]{};
    std::swprintf(fallback,32,L"%016llx",static_cast<unsigned long long>(selected));
    GetPrivateProfileStringW(L"AoIP",name,fallback,text,32,path);
    const size_t length=std::wcslen(text);
    if (!length || length>16) return;
    uint64_t value=0;
    for (size_t i=0;i<length;++i) {
      const wchar_t ch=text[i];
      unsigned digit=ch>=L'0' && ch<=L'9' ? unsigned(ch-L'0') : ch>=L'a' && ch<=L'f' ? unsigned(ch-L'a'+10) :
        ch>=L'A' && ch<=L'F' ? unsigned(ch-L'A'+10) : 16;
      if (digit>15) return;
      value=(value<<4)|digit;
    }
    selected=value;
  };
  mask(L"InputMask",c.input_mask); mask(L"OutputMask",c.output_mask);
  int energy=number(L"EnergySaving",c.energy_saving ? 1 : 0);
  if(energy==0 || energy==1) c.energy_saving=energy!=0;
  constexpr const wchar_t* cpu_keys[]={L"AudioCpu",L"ReceiveCpu",L"TransmitCpu"};
  for(unsigned role=0;role<c.realtime_cpus.size();++role) {
    wchar_t text[24]{},*end=nullptr;
    if(!GetPrivateProfileStringW(L"AoIP",cpu_keys[role],L"",text,24,path)) continue;
    const long cpu=std::wcstol(text,&end,10);
    if(end!=text && *end==L'\0' && cpu>=-1 && cpu<long(sizeof(ULONG_PTR)*8)) c.realtime_cpus[role]=int(cpu);
  }
  wchar_t spin[24]{},*spin_end=nullptr;
  if(GetPrivateProfileStringW(L"AoIP",L"AudioSpinUs",L"",spin,24,path)) {
    const long value=std::wcstol(spin,&spin_end,10);
    if(spin_end!=spin && *spin_end==L'\0' && value>=0 && value<=80) c.audio_spin_us=unsigned(value);
  }
}
void read_legacy_registry(Config& c) {
  constexpr auto key = L"Software\\PiAoIP\\ASIO";
  wchar_t peer[64]{};
  DWORD size = sizeof(peer);
  if (RegGetValueW(HKEY_CURRENT_USER, key, L"PeerIp", RRF_RT_REG_SZ,
                   nullptr, peer, &size) == ERROR_SUCCESS)
    WideCharToMultiByte(CP_UTF8, 0, peer, -1, c.peer, sizeof(c.peer), nullptr, nullptr);
  auto number = [&](const wchar_t* name, DWORD fallback) {
    DWORD value = fallback, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD,
                        nullptr, &value, &bytes) == ERROR_SUCCESS ? value : fallback;
  };
  auto rate = number(L"Rate", c.rate), bits = number(L"Bits", c.bits);
  auto block = number(L"BufferFrames", c.block);
  if (aoip::valid_rate(rate)) c.rate = rate;
  if (aoip::valid_bits(bits)) c.bits = static_cast<uint16_t>(bits);
  if (aoip::valid_buffer(block)) c.block = block;
}
void read_previous_install(Config& c) {
  wchar_t path[MAX_PATH]{};
  DWORD bytes = sizeof(path);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\PiAoIP", L"LegacyDriverPath",
                   RRF_RT_REG_SZ, nullptr, path, &bytes) != ERROR_SUCCESS) return;
  std::wstring value(path);
  auto slash = value.find_last_of(L"\\/");
  if (slash != std::wstring::npos &&
      copy_path(value.substr(0, slash + 1) + L"PiAoipAsio.ini", path) && is_file(path))
    read_ini(path, c);
}
}

bool profile_path(wchar_t (&path)[MAX_PATH]) {
  wchar_t override_path[MAX_PATH]{};
  DWORD count = GetEnvironmentVariableW(L"PIAOIP_CONFIG_PATH", override_path, MAX_PATH);
  if (count) {
    if (count >= MAX_PATH) return false;
    count = GetFullPathNameW(override_path, MAX_PATH, path, nullptr);
    return count && count < MAX_PATH && ensure_parent(path);
  }
  // Explicit portable profiles remain isolated from installed per-user settings.
  if (beside_module(path) && is_file(path)) return true;
  PWSTR folder = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &folder))) return false;
  std::wstring value = std::wstring(folder) + L"\\PiAoIP\\PiAoipAsio.ini";
  CoTaskMemFree(folder);
  return copy_path(value, path) && ensure_parent(path);
}

void read_config(Config& c) {
  read_legacy_registry(c);
  wchar_t path[MAX_PATH]{};
  if (!profile_path(path)) return;
  // First MSI launch can import a previous portable installation, but writes
  // always use the new user's own profile. Explicit test overrides never import.
  if (!is_file(path) && !GetEnvironmentVariableW(L"PIAOIP_CONFIG_PATH", nullptr, 0))
    read_previous_install(c);
  read_ini(path, c);
}
void read_config_file(const wchar_t* path,Config& c) { read_ini(path,c); }
bool write_config_file(const wchar_t* path,const Config& c) {
  if(!aoip::valid_rate(c.rate) || !aoip::valid_bits(c.bits) || !aoip::valid_buffer(unsigned(c.block)) || c.safety>2048) return false;
  wchar_t temporary[MAX_PATH]{};
  if(std::swprintf(temporary,MAX_PATH,L"%ls.%lu.tmp",path,GetCurrentProcessId())<0) return false;
  // Preserve unrelated INI sections and future settings. Commit with one rename.
  const bool exists=GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;
  if(exists && !CopyFileW(path,temporary,FALSE)) return false;
  if(!exists) {
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
  }
  bool okay=true;
  auto text=[&](const wchar_t* key,const wchar_t* value) { okay=WritePrivateProfileStringW(L"AoIP",key,value,temporary) && okay; };
  auto number=[&](const wchar_t* key,long long value) { wchar_t str[32]{}; std::swprintf(str,32,L"%lld",value); text(key,str); };
  wchar_t peer[64]{}; MultiByteToWideChar(CP_UTF8,0,c.peer,-1,peer,64); text(L"PeerIp",peer);
  number(L"Rate",c.rate); number(L"Bits",c.bits); number(L"BufferFrames",c.block); number(L"SafetyFrames",c.safety);
  number(L"EnergySaving",c.energy_saving ? 1 : 0);
  number(L"AudioCpu",c.realtime_cpus[0]); number(L"ReceiveCpu",c.realtime_cpus[1]); number(L"TransmitCpu",c.realtime_cpus[2]);
  number(L"AudioSpinUs",c.audio_spin_us);
  wchar_t mask[32]{}; std::swprintf(mask,32,L"%016llx",static_cast<unsigned long long>(c.input_mask)); text(L"InputMask",mask);
  std::swprintf(mask,32,L"%016llx",static_cast<unsigned long long>(c.output_mask)); text(L"OutputMask",mask);
  // The all-null cache-flush form does not report a settings write. Verify the
  // staged file directly instead of treating its return value as a failed key.
  WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary);
  HANDLE staged=CreateFileW(temporary,GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  if(staged==INVALID_HANDLE_VALUE) okay=false;
  else { okay=FlushFileBuffers(staged) && okay; CloseHandle(staged); }
  if(okay) okay=MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
  if(!okay) DeleteFileW(temporary);
  return okay;
}
HANDLE settings_changed_event(const wchar_t* profile) {
  std::wstring lower(profile); CharLowerBuffW(lower.data(),DWORD(lower.size()));
  uint64_t hash=14695981039346656037ull;
  for(wchar_t ch:lower) { hash^=uint16_t(ch); hash*=1099511628211ull; }
  wchar_t name[96]{}; std::swprintf(name,96,L"Local\\PiAoIP.Settings.%016llx",static_cast<unsigned long long>(hash));
  return CreateEventW(nullptr,FALSE,FALSE,name);
}
void notify_settings_changed(const wchar_t* profile) {
  HANDLE event=settings_changed_event(profile);
  if(event) { SetEvent(event); CloseHandle(event); }
}
}
