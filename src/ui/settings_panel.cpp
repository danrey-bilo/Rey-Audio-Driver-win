#include "../config/settings.hpp"
#include "iasiodrv.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <cwchar>
#include "resource.h"
namespace piaoip {
namespace {
constexpr COLORREF panel_bg=RGB(20,23,27), panel_surface=RGB(29,34,40),panel_text=RGB(237,242,246),panel_muted=RGB(166,178,188),panel_accent=RGB(111,224,196);
constexpr int menu_device=3400,menu_settings=3401,menu_diagnostics=3402;
constexpr int page_audio=3410,page_connection=3411,page_advanced=3412,page_diagnostics=3413;
int panel_height(PanelPage page) { return page==PanelPage::audio || page==PanelPage::advanced ? 196 : 276; }
void dark_caption(HWND window) {
  // Shared resources keep the ASIO-hosted panel and standalone app consistent.
  for(int size:{ICON_SMALL,ICON_BIG}) {
    const int pixels=GetSystemMetrics(size==ICON_SMALL ? SM_CXSMICON : SM_CXICON);
    auto icon=reinterpret_cast<HICON>(LoadImageW(g_module,MAKEINTRESOURCEW(IDI_PIAOIP),IMAGE_ICON,pixels,pixels,LR_SHARED));
    if(icon) SendMessageW(window,WM_SETICON,size,reinterpret_cast<LPARAM>(icon));
  }
  HMODULE dwm=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!dwm) return;
  using SetAttribute=HRESULT (WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
  auto set=reinterpret_cast<SetAttribute>(GetProcAddress(dwm,"DwmSetWindowAttribute"));
  const BOOL dark=TRUE;
  if(set) set(window,20,&dark,sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE, Windows 11
  FreeLibrary(dwm);
}
void scroll_panel(HWND window,SettingsDialog& state,int x,int y) {
  ScrollWindowEx(window,state.scroll_x-x,state.scroll_y-y,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);
  state.scroll_x=x; state.scroll_y=y;
  SetScrollPos(window,SB_HORZ,x,TRUE); SetScrollPos(window,SB_VERT,y,TRUE);
}
void arrange_scroll(HWND window,SettingsDialog& state) {
  if(state.preview || state.arranging) return;
  state.arranging=true;
  RECT content{0,0,320,panel_height(state.page)}; MapDialogRect(window,&content);
  for(int pass=0;pass<2;++pass) {
    RECT client{}; GetClientRect(window,&client);
    SCROLLINFO horizontal{sizeof(SCROLLINFO),SIF_RANGE|SIF_PAGE,0,content.right-1,UINT(client.right),0,0};
    SCROLLINFO vertical{sizeof(SCROLLINFO),SIF_RANGE|SIF_PAGE,0,content.bottom-1,UINT(client.bottom),0,0};
    SetScrollInfo(window,SB_HORZ,&horizontal,TRUE); SetScrollInfo(window,SB_VERT,&vertical,TRUE);
    scroll_panel(window,state,std::min(state.scroll_x,std::max(0,int(content.right-client.right))),std::min(state.scroll_y,std::max(0,int(content.bottom-client.bottom))));
  }
  state.arranging=false;
}
void panel_page(HWND window,SettingsDialog& state) {
  scroll_panel(window,state,0,0);
  // Remove the old page's scrollbars before calculating an exact client size.
  // Otherwise their own width/height can force bars even on a large monitor.
  state.arranging=true;
  ShowScrollBar(window,SB_BOTH,FALSE);
  for(size_t page=0;page<state.page_controls.size();++page)
    for(auto control:state.page_controls[page]) ShowWindow(control,page==size_t(state.page) ? SW_SHOW : SW_HIDE);
  constexpr const wchar_t* names[]{L"Audio",L"Connection",L"Advanced",L"Diagnostics"};
  SetDlgItemTextW(window,3335,names[size_t(state.page)]);
  ShowWindow(GetDlgItem(window,3340),state.page==PanelPage::audio ? SW_HIDE : SW_SHOW);
  const int height=panel_height(state.page);
  for(int id:{3340,IDCANCEL,IDOK}) {
    const int x=id==3340 ? 12 : id==IDCANCEL ? 148 : 230;
    RECT r{x,height-30,x+(id==3340 ? 108 : 78),height-8}; MapDialogRect(window,&r);
    SetWindowPos(GetDlgItem(window,id),nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
  }
  RECT desired{0,0,320,height}; MapDialogRect(window,&desired);
  AdjustWindowRectExForDpi(&desired,DWORD(GetWindowLongPtrW(window,GWL_STYLE))&~(WS_HSCROLL|WS_VSCROLL),FALSE,
    DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),GetDpiForWindow(window));
  int width=desired.right-desired.left,outer_height=desired.bottom-desired.top;
  if(!state.preview) {
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    width=std::min(width,int(monitor.rcWork.right-monitor.rcWork.left-24));
    outer_height=std::min(outer_height,int(monitor.rcWork.bottom-monitor.rcWork.top-24));
    RECT outer{}; GetWindowRect(window,&outer);
    const int x=std::clamp(int(outer.left),int(monitor.rcWork.left),int(monitor.rcWork.right)-width);
    const int y=std::clamp(int(outer.top),int(monitor.rcWork.top),int(monitor.rcWork.bottom)-outer_height);
    SetWindowPos(window,nullptr,x,y,width,outer_height,SWP_NOZORDER|SWP_NOACTIVATE);
  } else SetWindowPos(window,nullptr,0,0,width,outer_height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  state.arranging=false;
  arrange_scroll(window,state);
  InvalidateRect(window,nullptr,TRUE);
}
void panel_menu(HWND window,SettingsDialog& state,int id) {
  HMENU menu=CreatePopupMenu();
  const bool selected=state.selected_device>=0 && size_t(state.selected_device)<state.devices.size();
  if(id==menu_device) {
    AppendMenuW(menu,MF_STRING,page_connection,L"Find and connect…");
    const bool channels=selected && state.devices[state.selected_device].v3 && !state.scanning;
    AppendMenuW(menu,MF_STRING|(channels ? 0 : MF_GRAYED),3111,L"Channels…");
  } else if(id==menu_settings) {
    AppendMenuW(menu,MF_STRING,page_audio,L"Audio settings");
    AppendMenuW(menu,MF_STRING,page_advanced,L"Advanced…");
  } else {
    AppendMenuW(menu,MF_STRING,page_diagnostics,L"Stream counters and RTT…");
  }
  RECT r{}; GetWindowRect(GetDlgItem(window,id),&r);
  const auto command=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,r.left,r.bottom,window,nullptr);
  DestroyMenu(menu);
  if(command) SendMessageW(window,WM_COMMAND,command,0);
}
void paint_card(HWND window,HDC dc,int x,int y,int width,int height) {
  RECT r{x,y,x+width,y+height}; MapDialogRect(window,&r);
  auto brush=CreateSolidBrush(panel_surface); auto pen=CreatePen(PS_SOLID,1,RGB(49,57,65));
  auto old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);
  RoundRect(dc,r.left,r.top,r.right,r.bottom,12,12);
  SelectObject(dc,old_brush); SelectObject(dc,old_pen); DeleteObject(brush); DeleteObject(pen);
}
bool draw_panel_button(const DRAWITEMSTRUCT& draw) {
  if(draw.CtlType==ODT_COMBOBOX) {
    auto brush=CreateSolidBrush((draw.itemState&ODS_SELECTED) && !(draw.itemState&ODS_COMBOBOXEDIT) ? RGB(40,89,79) : panel_surface);
    FillRect(draw.hDC,&draw.rcItem,brush); DeleteObject(brush);
    wchar_t text[64]{};
    if(draw.itemID!=UINT(-1)) SendMessageW(draw.hwndItem,CB_GETLBTEXT,draw.itemID,reinterpret_cast<LPARAM>(text));
    SetTextColor(draw.hDC,(draw.itemState&ODS_DISABLED) ? panel_muted : panel_text); SetBkMode(draw.hDC,TRANSPARENT);
    auto font=SelectObject(draw.hDC,reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem,WM_GETFONT,0,0)));
    RECT r=draw.rcItem; r.left+=7; DrawTextW(draw.hDC,text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    SelectObject(draw.hDC,font); return true;
  }
  if(draw.CtlType!=ODT_BUTTON) return false;
  const bool primary=draw.CtlID==IDOK;
  const bool menu=draw.CtlID>=menu_device && draw.CtlID<=menu_diagnostics;
  const bool disabled=(draw.itemState&ODS_DISABLED)!=0;
  COLORREF color=menu ? panel_bg : primary ? panel_accent : RGB(45,53,61);
  if(disabled) color=RGB(39,45,51);
  else if(draw.itemState&ODS_SELECTED) color=primary ? RGB(84,189,164) : RGB(62,72,81);
  auto brush=CreateSolidBrush(color); auto old_brush=SelectObject(draw.hDC,brush);
  auto pen=CreatePen(PS_SOLID,1,(primary || menu) ? color : RGB(75,87,98)); auto old_pen=SelectObject(draw.hDC,pen);
  RoundRect(draw.hDC,draw.rcItem.left,draw.rcItem.top,draw.rcItem.right,draw.rcItem.bottom,8,8);
  SetBkMode(draw.hDC,TRANSPARENT); SetTextColor(draw.hDC,disabled ? RGB(113,124,134) : primary ? RGB(17,37,32) : panel_text);
  wchar_t label[128]{}; GetWindowTextW(draw.hwndItem,label,128);
  auto font=SelectObject(draw.hDC,reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem,WM_GETFONT,0,0)));
  RECT text=draw.rcItem; InflateRect(&text,-5,-2);
  const auto ui_state=SendMessageW(GetParent(draw.hwndItem),WM_QUERYUISTATE,0,0);
  DrawTextW(draw.hDC,label,-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|((draw.itemState&ODS_NOACCEL) || (ui_state&UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0));
  if((draw.itemState&ODS_FOCUS) && !(draw.itemState&ODS_NOFOCUSRECT) && !(ui_state&UISF_HIDEFOCUS)) { InflateRect(&text,-2,-2); DrawFocusRect(draw.hDC,&text); }
  SelectObject(draw.hDC,font); SelectObject(draw.hDC,old_brush); SelectObject(draw.hDC,old_pen); DeleteObject(brush); DeleteObject(pen);
  return true;
}
struct ChannelDialog {
  Config selected; unsigned inputs,outputs; HBRUSH background=nullptr;
  unsigned columns() const { return std::max(inputs,outputs)>16 ? 8 : 4; }
  int bank_width() const { return int(columns())*34; }
  int width() const { return 2*bank_width()+36; }
  int bottom() const { return 54+int((std::max(inputs,outputs)+columns()-1)/columns())*18; }
};
INT_PTR CALLBACK channel_dialog_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  auto* state=reinterpret_cast<ChannelDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_INITDIALOG) {
    state=reinterpret_cast<ChannelDialog*>(lparam);
    state->background=CreateSolidBrush(panel_surface);
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    SetWindowTextW(window,L"Pi AoIP channels");
    dark_caption(window);
    auto add=[&](const wchar_t* cls,const wchar_t* caption,DWORD style,int x,int y,int width,int height,int id) {
      RECT r{x,y,x+width,y+height}; MapDialogRect(window,&r);
      HWND child=CreateWindowW(cls,caption,WS_CHILD|WS_VISIBLE|style,r.left,r.top,r.right-r.left,r.bottom-r.top,
        window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_module,nullptr);
      SendMessageW(child,WM_SETFONT,SendMessageW(window,WM_GETFONT,0,0),TRUE);
      SetWindowTheme(child,L"",nullptr);
      return child;
    };
    add(L"STATIC",L"Uncheck a channel to stop sending it over LAN.",0,12,9,state->width()-24,14,0);
    add(L"STATIC",L"Inputs — Pi → PC",0,12,31,state->bank_width(),14,0);
    add(L"STATIC",L"Outputs — PC → Pi",0,24+state->bank_width(),31,state->bank_width(),14,0);
    for(unsigned direction=0;direction<2;++direction) {
      const unsigned count=direction ? state->outputs : state->inputs;
      const uint64_t mask=direction ? state->selected.output_mask : state->selected.input_mask;
      for(unsigned ch=0;ch<count;++ch) {
        wchar_t label[8]{}; std::swprintf(label,8,L"%u",ch+1);
        const int id=(direction ? 4100 : 4000)+int(ch);
        add(L"BUTTON",label,WS_TABSTOP|BS_AUTOCHECKBOX,12+int(direction)*(state->bank_width()+12)+int(ch%state->columns())*34,
          52+int(ch/state->columns())*18,34,16,id);
        CheckDlgButton(window,id,(mask & (uint64_t(1)<<ch)) ? BST_CHECKED : BST_UNCHECKED);
      }
    }
    const int bottom=state->bottom(),right=24+state->bank_width();
    add(L"BUTTON",L"All inputs",WS_TABSTOP|BS_OWNERDRAW,12,bottom,64,22,4200);
    add(L"BUTTON",L"No inputs",WS_TABSTOP|BS_OWNERDRAW,84,bottom,64,22,4201);
    add(L"BUTTON",L"All outputs",WS_TABSTOP|BS_OWNERDRAW,right,bottom,64,22,4202);
    add(L"BUTTON",L"No outputs",WS_TABSTOP|BS_OWNERDRAW,right+72,bottom,64,22,4203);
    add(L"BUTTON",L"Cancel",WS_TABSTOP|BS_OWNERDRAW,state->width()-178,bottom+32,78,23,IDCANCEL);
    add(L"BUTTON",L"Done",WS_TABSTOP|BS_OWNERDRAW,state->width()-90,bottom+32,78,23,IDOK);
    return TRUE;
  }
  if(state && (message==WM_CTLCOLORDLG || message==WM_CTLCOLORSTATIC || message==WM_CTLCOLORBTN)) {
    SetTextColor(reinterpret_cast<HDC>(wparam),panel_text); SetBkColor(reinterpret_cast<HDC>(wparam),panel_surface);
    return reinterpret_cast<INT_PTR>(state->background);
  }
  if(message==WM_DRAWITEM && lparam && draw_panel_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
  if(message==WM_DESTROY && state) { DeleteObject(state->background); state->background=nullptr; }
  if(message==WM_CLOSE) { EndDialog(window,IDCANCEL); return TRUE; }
  if(message!=WM_COMMAND || !state) return FALSE;
  const int id=LOWORD(wparam);
  if(id>=4200 && id<=4203) {
    const bool output=id>=4202,enable=(id%2)==0;
    for(unsigned ch=0;ch<(output ? state->outputs : state->inputs);++ch)
      CheckDlgButton(window,(output ? 4100 : 4000)+int(ch),enable ? BST_CHECKED : BST_UNCHECKED);
    return TRUE;
  }
  if(id==IDOK) {
    for(unsigned direction=0;direction<2;++direction) {
      const unsigned count=direction ? state->outputs : state->inputs;
      uint64_t& mask=direction ? state->selected.output_mask : state->selected.input_mask;
      mask &= ~aoip::channel_mask(count);
      for(unsigned ch=0;ch<count;++ch) if(IsDlgButtonChecked(window,(direction ? 4100 : 4000)+int(ch))==BST_CHECKED)
        mask |= uint64_t(1)<<ch;
    }
  }
  if(id==IDOK || id==IDCANCEL) { EndDialog(window,id); return TRUE; }
  return FALSE;
}
void edit_channels(HWND owner,SettingsDialog& state) {
  if(state.selected_device<0 || state.selected_device>=int(state.devices.size())) return;
  const auto& device=state.devices[state.selected_device];
  ChannelDialog selected{state.current,device.channels,device.outputs};
  selected.inputs=std::min(64u,selected.inputs); selected.outputs=std::min(64u,selected.outputs);
  alignas(DWORD) uint8_t storage[256]{};
  auto* dialog=reinterpret_cast<DLGTEMPLATE*>(storage);
  dialog->style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;
  dialog->cx=WORD(selected.width()); dialog->cy=WORD(selected.bottom()+64);
  auto* extra=reinterpret_cast<WORD*>(storage+sizeof(DLGTEMPLATE));
  *extra++=0; *extra++=0; *extra++=0; *extra++=10;
  const wchar_t face[]=L"Segoe UI"; std::memcpy(extra,face,sizeof(face));
  if(DialogBoxIndirectParamW(g_module,dialog,owner,channel_dialog_proc,reinterpret_cast<LPARAM>(&selected))==IDOK) {
    state.current.input_mask=selected.selected.input_mask; state.current.output_mask=selected.selected.output_mask;
  }
}
}
void refresh_rates(HWND window, SettingsDialog& state, unsigned preferred) {
  if (state.selected_device < 0 || state.selected_device >= static_cast<int>(state.devices.size())) return;
  const auto& device = state.devices[state.selected_device];
  const unsigned channels=device.channels;
  BOOL valid_bits = FALSE;
  unsigned bits = GetDlgItemInt(window, 3003, &valid_bits, FALSE);
  if (!valid_bits || !channels || !bits) return;
  unsigned max_frames = (1472 - 40) / (channels * (bits / 8));
  unsigned frames = max_frames;
  HWND combo = GetDlgItem(window, 3002);
  SendMessageA(combo, CB_RESETCONTENT, 0, 0);
  LRESULT selection = CB_ERR;
  for (unsigned value : device.rates) {
    if ((state.is_live && value > 192000) ||
        !profile_fits_link(channels, value, bits, frames,
                           device.max_pps, device.link_mbps)) continue;
    char label[32]{};
    std::snprintf(label, sizeof(label), "%u", value);
    LRESULT item = SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    if (value == preferred) selection = item;
  }
  if (selection == CB_ERR) {
    LRESULT count = SendMessageA(combo, CB_GETCOUNT, 0, 0);
    if (count > 0) selection = count - 1;
  }
  if (selection != CB_ERR) SendMessageA(combo, CB_SETCURSEL, selection, 0);
}

void show_device(HWND window, SettingsDialog& state, int index) {
  if (index < 0 || index >= static_cast<int>(state.devices.size())) return;
  state.selected_device = index;
  const auto& device = state.devices[index];
  SetDlgItemTextA(window, 3000, device.ip);
  auto fill = [&](int id, const std::vector<unsigned>& options, unsigned selected, bool editable=false) {
    HWND combo = GetDlgItem(window, id);
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    LRESULT chosen = CB_ERR;
    for (unsigned value : options) {
      char label[32]{};
      std::snprintf(label, sizeof(label), "%u", value);
      LRESULT item = SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
      if (value == selected) chosen = item;
    }
    if(chosen==CB_ERR && editable) {
      char label[32]{}; std::snprintf(label,sizeof(label),"%u",selected);
      SetWindowTextA(combo,label); return;
    }
    if (chosen == CB_ERR && !options.empty()) chosen = 0;
    if (chosen != CB_ERR) SendMessageA(combo, CB_SETCURSEL, chosen, 0);
  };
  fill(3010,{0,16,32,64,96,128,192,224,256,384,448,480,512,1024,2048},state.current.safety,true);
  fill(3003, device.supported_bits, device.bits);
  fill(3004, device.buffers, state.current.block);
  refresh_rates(window, state, device.rate);
  wchar_t status[240]{};
  std::swprintf(status,240,L"Connected · %u Mbps\n%u inputs / %u outputs · %.1f kHz",device.link_mbps,device.channels,device.outputs,device.rate/1000.0);
  SetDlgItemTextW(window,3103,status);
  std::swprintf(status,240,L"%hs · %u × %u",device.ip,device.channels,device.outputs);
  SetDlgItemTextW(window,3124,status);
  EnableWindow(GetDlgItem(window,3110),device.v3);
  unsigned channel=std::min(device.channels,device.outputs);
  std::swprintf(status,240,L"RTT: route input %u to output %u in your DAW (PCM32).",channel,channel);
  SetDlgItemTextW(window,3105,status);
}

void refresh_measured_latency(HWND window, const SettingsDialog& state) {
  if (state.selected_device < 0 ||
      state.selected_device >= static_cast<int>(state.devices.size())) return;
  char reply[512]{};
  sockaddr_in responder{};
  if (!control_request(state.devices[state.selected_device].ip, nullptr,
                       "PIAOIP_MEASURE_STATUS_V1", reply, responder, 100)) {
    SetDlgItemTextW(window,3105,L"RTT: device did not respond.");
    return;
  }
  unsigned long long count = 0, minimum = 0, maximum = 0;
  unsigned p50 = 0, p95 = 0, p99 = 0, active = 0;
  if (std::sscanf(reply,
      "PIAOIP_MEASURE_V1 count=%llu min_us=%llu p50_us=%u p95_us=%u "
      "p99_us=%u max_us=%llu active=%u",
      &count, &minimum, &p50, &p95, &p99, &maximum, &active) != 7) {
    SetDlgItemTextW(window,3105,L"RTT: measurement unavailable.");
    return;
  }
  wchar_t label[512]{};
  if (!count)
    std::swprintf(label,512,L"RTT: waiting for input %u to return on output %u.",state.measure_channel,state.measure_channel);
  else if(p50>=20000 || p99>=20000)
    std::swprintf(label,512,L"RTT: min %llu us; max %llu us. Percentiles exceed 20 ms.",minimum,maximum);
  else
    std::swprintf(label,512,
        L"Pi → ASIO → Pi: median %u us · p99 %u · max %llu (%llu packets)",
        p50, p99, maximum, count);
  SetDlgItemTextW(window,3105,label);
}

INT_PTR CALLBACK settings_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  constexpr int kFirstEdit = 3000;
  constexpr int kScan = 3100, kDeviceList = 3101, kCheck = 3102,
                kStatus = 3103, kMeasure = 3104, kMeasured = 3105;
  auto* state = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrA(window, GWLP_USERDATA));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<SettingsDialog*>(lparam);
    SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    SetWindowTextA(window, "Pi AoIP configuration");
    dark_caption(window);
    state->background=CreateSolidBrush(panel_bg);
    state->surface=CreateSolidBrush(panel_surface);
    int page=-1, label_id=5000;
    auto add=[&](const wchar_t* cls,const wchar_t* caption,DWORD style,int x,int y,int width,int height,int id) {
      RECT r{x,y,x+width,y+height}; MapDialogRect(window,&r);
      if(!id) id=label_id++;
      HWND child=CreateWindowExW(0,cls,caption,WS_CHILD|WS_VISIBLE|style,r.left,r.top,r.right-r.left,r.bottom-r.top,
        window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_module,nullptr);
      SendMessageW(child,WM_SETFONT,SendMessageW(window,WM_GETFONT,0,0),TRUE);
      if(!std::wcscmp(cls,L"EDIT") || !std::wcscmp(cls,L"COMBOBOX") || !std::wcscmp(cls,L"LISTBOX"))
        SetWindowTheme(child,L"DarkMode_Explorer",nullptr);
      if(page>=0) state->page_controls[size_t(page)].push_back(child);
      return child;
    };
    auto button=[&](const wchar_t* text,int x,int y,int w,int id) {
      return add(L"BUTTON",text,WS_TABSTOP|BS_OWNERDRAW,x,y,w,22,id);
    };
    LOGFONTW font{}; GetObjectW(reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)),sizeof(font),&font);
    font.lfHeight=font.lfHeight*13/10; font.lfWeight=FW_SEMIBOLD; state->heading_font=CreateFontIndirectW(&font);
    state->value_font=CreateFontIndirectW(&font);
    HWND title=add(L"STATIC",L"Pi AoIP",0,12,10,66,20,3300);
    SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(state->heading_font),TRUE);
    button(L"&Device",80,8,72,menu_device);
    button(L"&Settings",152,8,72,menu_settings);
    button(L"D&iagnostics",224,8,84,menu_diagnostics);
    add(L"STATIC",L"Audio",0,16,40,150,15,3335);
    button(L"← Audio",12,194,108,3340);
    button(L"Close",148,194,78,IDCANCEL); button(L"Apply",230,194,78,IDOK);
    auto combo=[&](int id,const wchar_t* label,int x,int y,int width,unsigned value) {
      add(L"STATIC",label,0,x,y,width,12,0);
      HWND control=add(L"COMBOBOX",L"",WS_TABSTOP|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|(id==3010 ? CBS_DROPDOWN : CBS_DROPDOWNLIST)|WS_VSCROLL,
        x,y+15,width,150,id);
      LOGFONTW f{}; GetObjectW(reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)),sizeof(f),&f);
      SendMessageW(control,CB_SETITEMHEIGHT,WPARAM(-1),std::abs(f.lfHeight)+8);
      SendMessageW(control,CB_SETITEMHEIGHT,0,std::abs(f.lfHeight)+8);
      wchar_t text[24]{}; std::swprintf(text,24,L"%u",value);
      SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text)); SendMessageW(control,CB_SETCURSEL,0,0);
    };
    page=int(PanelPage::audio);
    add(L"STATIC",L"Searching…",SS_RIGHT,96,40,208,15,3124);
    combo(3002,L"Sample rate · Hz",24,64,128,state->current.rate);
    combo(3003,L"Bit depth",168,64,128,state->current.bits);
    combo(3004,L"ASIO buffer · samples",24,101,128,unsigned(state->current.block));
    combo(3010,L"LAN buffer · samples",168,101,128,state->current.safety);
    SendDlgItemMessageW(window,3010,CB_LIMITTEXT,4,0);
    add(L"STATIC",L"",0,24,141,128,13,3121);
    add(L"STATIC",L"",0,168,141,128,13,3122);
    page=int(PanelPage::connection);
    button(L"Find devices",24,76,128,kScan);
    add(L"STATIC",L"Wired network",SS_RIGHT,162,81,134,14,0);
    add(L"LISTBOX",L"",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|WS_BORDER,24,107,272,44,kDeviceList);
    add(L"STATIC",L"Device IPv4 address",0,24,160,180,12,0);
    add(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,24,175,172,22,3000);
    SetDlgItemTextA(window,3000,state->current.peer); button(L"Connect",208,175,88,kCheck);
    add(L"STATIC",L"Searching for devices on wired networks…",0,24,204,272,26,kStatus);
    page=int(PanelPage::advanced);
    HWND energy=add(L"BUTTON",L"Reduce traffic during digital silence",WS_TABSTOP|BS_AUTOCHECKBOX,24,80,272,20,3110);
    SetWindowTheme(energy,L"",nullptr);
    CheckDlgButton(window,3110,state->current.energy_saving ? BST_CHECKED : BST_UNCHECKED);
    add(L"STATIC",L"Disabled channels do not use network bandwidth. Select them in Device → Channels.",0,24,112,272,32,3112);
    page=int(PanelPage::diagnostics);
    add(L"STATIC",L"Stream counters",0,24,76,272,14,3352);
    add(L"STATIC",L"Open this panel from your running DAW to view its stream counters.",0,24,98,272,52,3120);
    add(L"STATIC",L"RTT requires PCM32 and a DAW route from an input to the matching output.",0,24,160,272,48,kMeasured);
    button(L"Measure RTT",24,205,136,kMeasure);
    panel_page(window,*state);
    SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEACCEL|UISF_HIDEFOCUS),0);
    if(!state->preview) {
      SetTimer(window,2,500,nullptr);
      PostMessageW(window,WM_COMMAND,MAKEWPARAM(kScan,BN_CLICKED),0);
    }
    return TRUE;
  }
  if((message==WM_PAINT || message==WM_PRINTCLIENT) && state) {
    PAINTSTRUCT paint{}; HDC dc=message==WM_PAINT ? BeginPaint(window,&paint) : reinterpret_cast<HDC>(wparam);
    RECT client{}; GetClientRect(window,&client); FillRect(dc,&client,state->background);
    SetViewportOrgEx(dc,-state->scroll_x,-state->scroll_y,nullptr);
    if(state->page==PanelPage::audio) paint_card(window,dc,12,56,296,104);
    else paint_card(window,dc,12,66,296,panel_height(state->page)-108);
    auto line=CreatePen(PS_SOLID,1,RGB(48,56,64)); auto old=SelectObject(dc,line);
    RECT divider{12,35,308,35}; MapDialogRect(window,&divider);
    MoveToEx(dc,divider.left,divider.top,nullptr); LineTo(dc,divider.right,divider.bottom);
    SelectObject(dc,old); DeleteObject(line);
    if(message==WM_PAINT) EndPaint(window,&paint); return TRUE;
  }
  if(message==WM_DRAWITEM && lparam && draw_panel_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
  if(message==WM_SIZE && state) { arrange_scroll(window,*state); return TRUE; }
  if((message==WM_VSCROLL || message==WM_HSCROLL || message==WM_MOUSEWHEEL) && state) {
    const int bar=message==WM_HSCROLL ? SB_HORZ : SB_VERT;
    SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(window,bar,&info);
    int position=info.nPos;
    if(message==WM_MOUSEWHEEL) position-=GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA*60;
    else switch(LOWORD(wparam)) {
      case SB_LINEUP: position-=24; break;
      case SB_LINEDOWN: position+=24; break;
      case SB_PAGEUP: position-=int(info.nPage); break;
      case SB_PAGEDOWN: position+=int(info.nPage); break;
      case SB_THUMBTRACK: case SB_THUMBPOSITION: position=info.nTrackPos; break;
      case SB_TOP: position=0; break;
      case SB_BOTTOM: position=info.nMax; break;
    }
    position=std::clamp(position,0,std::max(0,info.nMax-int(info.nPage)+1));
    scroll_panel(window,*state,bar==SB_HORZ ? position : state->scroll_x,bar==SB_VERT ? position : state->scroll_y); return TRUE;
  }
  if(message==WM_MEASUREITEM && lparam) {
    auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
    if(item->CtlType==ODT_COMBOBOX) { item->itemHeight=24; return TRUE; }
  }
  if(message==WM_CTLCOLORDLG || message==WM_CTLCOLORSTATIC || message==WM_CTLCOLORBTN || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX) {
    if(!state) return FALSE;
    auto dc=reinterpret_cast<HDC>(wparam); const int id=GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
    const bool header=message==WM_CTLCOLORDLG || id==3300 || id==3335 || id==3124;
    SetBkColor(dc,header ? panel_bg : panel_surface);
    const bool section=id==3352 || id==3335;
    SetTextColor(dc,section ? panel_accent : id==3124 || id==3121 || id==3122 ? panel_muted : panel_text);
    return reinterpret_cast<INT_PTR>(header ? state->background : state->surface);
  }
  if(message==WM_TIMER && wparam==2 && state) {
    wchar_t label[512]{};
    unsigned rate=GetDlgItemInt(window,3002,nullptr,FALSE),block=GetDlgItemInt(window,3004,nullptr,FALSE);
    unsigned guard=GetDlgItemInt(window,3010,nullptr,FALSE);
    if(rate) {
      std::swprintf(label,512,L"%.3f ms",block*1000.0/rate);
      SetDlgItemTextW(window,3121,label);
      std::swprintf(label,512,L"%.3f ms",guard*1000.0/rate);
      SetDlgItemTextW(window,3122,label);
    }
    if(state->driver) {
      aoip::Diagnostics d; aoip::StreamDiagnostics stream;
      if(static_cast<IASIO*>(state->driver)->future(aoip::stream_diagnostics_selector,&stream)==ASE_SUCCESS && stream.running &&
        static_cast<IASIO*>(state->driver)->future(aoip::diagnostics_selector,&d)==ASE_SUCCESS) {
        std::swprintf(label,512,L"Missing %llu · Late %llu · Deadline misses %llu\nTX errors %llu · TX expired %llu · Host overruns %llu",
          d.missing_frames,d.late_frames,d.deadline_misses,d.tx_errors,d.tx_expired_packets,d.host_overruns);
        SetDlgItemTextW(window,3120,label);
      } else {
        SetDlgItemTextW(window,3120,L"No ASIO stream in this panel. Open it from your DAW to view current counters.");
      }
    }
    return TRUE;
  }
  if (message == WM_TIMER && wparam == 1 && state && state->measuring) {
    refresh_measured_latency(window, *state);
    return TRUE;
  }
  if (message == WM_CLOSE) {
    if (state && state->measuring) {
      char reply[512]{};
      sockaddr_in responder{};
      control_request(state->measure_peer, nullptr,
                      "PIAOIP_MEASURE_STOP_V1", reply, responder, 100);
      KillTimer(window, 1);
    }
    EndDialog(window, IDCANCEL);
    return TRUE;
  }
  if(message==WM_APP+1 && state) {
    if(state->scanner.joinable()) state->scanner.join();
    state->scanning=false;
    state->devices=std::move(state->scan_results);
    EnableWindow(GetDlgItem(window,kScan),TRUE);
    EnableWindow(GetDlgItem(window,kCheck),TRUE);
    EnableWindow(GetDlgItem(window,IDOK),TRUE);
    HWND list = GetDlgItem(window, kDeviceList);
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    for (const auto& device : state->devices) {
      char label[128]{};
      std::snprintf(label,sizeof(label),"%s",device.ip);
      SendMessageA(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    if (state->devices.empty()) {
      state->selected_device = -1;
      SetDlgItemTextW(window,kStatus,L"No device found. Check the cable or enter its IPv4 address above.");
      SetDlgItemTextW(window,3124,L"No device · open Device menu");
    } else {
      int selected=0;
      for(size_t i=0;i<state->devices.size();++i) if(!std::strcmp(state->devices[i].ip,state->current.peer)) selected=int(i);
      SendMessageA(list, LB_SETCURSEL, selected, 0);
      show_device(window,*state,selected);
    }
    return TRUE;
  }
  if (message != WM_COMMAND) return FALSE;
  const int command=LOWORD(wparam);
  if(state && command>=menu_device && command<=menu_diagnostics) { panel_menu(window,*state,command); return TRUE; }
  if(state && (command==3340 || (command>=page_audio && command<=page_diagnostics))) {
    state->page=command==3340 ? PanelPage::audio : command==page_connection ? PanelPage::connection :
      command==page_advanced ? PanelPage::advanced : command==page_diagnostics ? PanelPage::diagnostics : PanelPage::audio;
    panel_page(window,*state);
    if(!state->preview) SetFocus(GetDlgItem(window,state->page==PanelPage::audio ? 3002 : command==page_connection ? kScan : 3340));
    return TRUE;
  }
  if(LOWORD(wparam)==3111 && state) { edit_channels(window,*state); return TRUE; }
  if(LOWORD(wparam)==kScan) {
    if(state->scanning) return TRUE;
    state->scanning=true;
    SetDlgItemTextW(window,kStatus,L"Searching wired networks…");
    SetDlgItemTextW(window,3124,L"Searching…");
    EnableWindow(GetDlgItem(window,kScan),FALSE);
    EnableWindow(GetDlgItem(window,kCheck),FALSE);
    EnableWindow(GetDlgItem(window,IDOK),FALSE);
    const Config scan_config=state->current;
    state->scanner=std::thread([state,window,scan_config] {
      SettingsDialog result{}; result.current=scan_config;
      try { discover_devices(result); } catch(...) { result.devices.clear(); }
      state->scan_results=std::move(result.devices);
      PostMessageA(window,WM_APP+1,0,0);
    });
    return TRUE;
  }
  if (LOWORD(wparam) == kDeviceList && HIWORD(wparam) == LBN_SELCHANGE) {
    show_device(window, *state, static_cast<int>(SendDlgItemMessageA(window, kDeviceList, LB_GETCURSEL, 0, 0)));
    return TRUE;
  }
  if (LOWORD(wparam) == 3003 && HIWORD(wparam) == CBN_SELCHANGE) {
    BOOL valid = FALSE;
    unsigned selected_rate = GetDlgItemInt(window, 3002, &valid, FALSE);
    refresh_rates(window, *state, valid ? selected_rate : 0);
    return TRUE;
  }
  if (LOWORD(wparam) == kCheck) {
    char peer[64]{};
    GetDlgItemTextA(window, kFirstEdit, peer, sizeof(peer));
    SettingsDialog::Device device{};
    if (!query_device(peer,device)) {
      state->selected_device = -1;
      SetDlgItemTextW(window,kStatus,L"Device did not respond. Check its address and cable.");
      SetDlgItemTextW(window,3124,L"Device not responding");
      return TRUE;
    }
    int selected=-1;
    for(size_t i=0;i<state->devices.size();++i) if(!std::strcmp(state->devices[i].ip,peer)) selected=int(i);
    if(selected<0) {
      state->devices.push_back(device); selected=int(state->devices.size()-1);
      SendDlgItemMessageA(window,kDeviceList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(device.ip));
    } else state->devices[selected]=device;
    SendDlgItemMessageW(window,kDeviceList,LB_SETCURSEL,selected,0);
    show_device(window,*state,selected);
    return TRUE;
  }
  if (LOWORD(wparam) == kMeasure) {
    if (!state || state->selected_device < 0 ||
        state->selected_device >= static_cast<int>(state->devices.size())) {
      MessageBoxW(window,L"Find and select a device first.",L"Pi AoIP", MB_OK | MB_ICONINFORMATION);
      return TRUE;
    }
    const char* ip = state->devices[state->selected_device].ip;
    char reply[512]{};
    sockaddr_in responder{};
    if (state->measuring) {
      control_request(state->measure_peer, nullptr, "PIAOIP_MEASURE_STOP_V1", reply, responder, 100);
      state->measuring = false;
      KillTimer(window, 1);
      SetDlgItemTextW(window,kMeasure,L"Measure RTT");
      refresh_measured_latency(window, *state);
    } else {
      const auto& device=state->devices[state->selected_device];
      uint64_t common=state->current.input_mask & state->current.output_mask & aoip::channel_mask(std::min(device.channels,device.outputs));
      aoip::StreamDiagnostics stream;
      if(state->driver && static_cast<IASIO*>(state->driver)->future(aoip::stream_diagnostics_selector,&stream)==ASE_SUCCESS && stream.running)
        common &= stream.input_mask & stream.output_mask;
      unsigned channel=0; for(unsigned ch=0;ch<64;++ch) if(common & (uint64_t(1)<<ch)) channel=ch+1;
      char request[96]{};
      std::snprintf(request,sizeof(request),device.v3 ? "PIAOIP_MEASURE_START_V2 channel=%u" : "PIAOIP_MEASURE_START_V1",channel);
      const bool started=channel && control_request(ip,nullptr,request,reply,responder,100) &&
        std::strncmp(reply,device.v3 ? "PIAOIP_MEASURE_STARTED_V2" : "PIAOIP_MEASURE_STARTED_V1",23)==0;
      if(started) {
      state->measuring = true;
      state->measure_channel=device.v3 ? channel : 32;
      std::snprintf(state->measure_peer, sizeof(state->measure_peer), "%s", ip);
      SetDlgItemTextW(window,kMeasure,L"Stop RTT");
      SetTimer(window, 1, 1000, nullptr);
      refresh_measured_latency(window, *state);
      } else {
      MessageBoxW(window,L"For RTT, run your DAW in PCM32 and route an enabled input to the matching output. Legacy firmware requires channel 32.",L"Pi AoIP", MB_OK | MB_ICONINFORMATION);
      }
    }
    return TRUE;
  }
  if (LOWORD(wparam) == IDCANCEL) {
    if (state && state->measuring) {
      char reply[512]{};
      sockaddr_in responder{};
      control_request(state->measure_peer, nullptr,
                      "PIAOIP_MEASURE_STOP_V1", reply, responder, 100);
      KillTimer(window, 1);
    }
    EndDialog(window, IDCANCEL);
    return TRUE;
  }
  if (LOWORD(wparam) != IDOK || !state) return FALSE;

  if (state->measuring) {
    char reply[512]{};
    sockaddr_in responder{};
    control_request(state->measure_peer, nullptr,
                    "PIAOIP_MEASURE_STOP_V1", reply, responder, 100);
    state->measuring = false;
    KillTimer(window, 1);
  }

  char peer[64]{};
  GetDlgItemTextA(window, kFirstEdit, peer, sizeof(peer));
  sockaddr_in address{};
  if (inet_pton(AF_INET, peer, &address.sin_addr) != 1) {
    MessageBoxW(window,L"Enter a valid device IPv4 address.",L"Pi AoIP", MB_OK | MB_ICONERROR);
    return TRUE;
  }
  unsigned values[3]{};
  for (int i = 0; i < 3; ++i) {
    BOOL valid = FALSE;
    values[i] = GetDlgItemInt(window, kFirstEdit + 2 + i, &valid, FALSE);
    if (!valid) {
      MessageBoxW(window,L"Select a valid positive integer in each audio format field.",L"Pi AoIP", MB_OK | MB_ICONERROR);
      return TRUE;
    }
  }
  unsigned rate = values[0], bits = values[1], block = values[2];
  if (state->selected_device < 0 ||
      state->selected_device >= static_cast<int>(state->devices.size()) ||
      std::strcmp(state->devices[state->selected_device].ip, peer) != 0) {
    MessageBoxW(window,L"Find the device or connect to its address before saving.",L"Pi AoIP", MB_OK | MB_ICONERROR);
    return TRUE;
  }
  const auto& device = state->devices[state->selected_device];
  BOOL valid_guard=FALSE;
  const unsigned inputs=device.channels, outputs=device.outputs;
  unsigned guard=GetDlgItemInt(window,3010,&valid_guard,FALSE);
  char guard_text[16]{}; GetDlgItemTextA(window,3010,guard_text,sizeof(guard_text));
  for(const char* digit=guard_text;*digit;++digit) if(*digit<'0' || *digit>'9') valid_guard=FALSE;
  if(!valid_guard || guard>2048) {
    MessageBoxW(window,L"LAN buffer must be an integer from 0 to 2048 samples.",L"Pi AoIP",MB_OK|MB_ICONERROR);
    SetFocus(GetDlgItem(window,3010)); return TRUE;
  }
  if(!aoip::valid_profile({inputs,outputs,rate,bits,block},device.link_mbps)) {
    MessageBoxW(window,L"The selected format exceeds the connection bandwidth.",L"Pi AoIP",MB_OK|MB_ICONERROR); return TRUE;
  }
  const unsigned channels=device.channels, wire_outputs=device.outputs;
  auto offered = [](const std::vector<unsigned>& list, unsigned value) {
    return std::find(list.begin(), list.end(), value) != list.end();
  };
  if (!offered(device.rates, rate) || !offered(device.supported_bits, bits) ||
      !offered(device.buffers, block)) {
    MessageBoxW(window,L"The device does not support this profile.",L"Pi AoIP", MB_OK | MB_ICONERROR);
    return TRUE;
  }
  if (state->is_live && rate > 192000) {
    MessageBoxW(window,L"For Ableton Live, select a sample rate of 192000 Hz or lower.",L"Pi AoIP", MB_OK | MB_ICONERROR);
    return TRUE;
  }
  if (channels < 1 || channels > 64 || rate < 8000 || rate > 768000 ||
      (bits != 16 && bits != 24 && bits != 32) || block < 16 || block > 2048 ||
      (block & (block - 1)) ||
      uint64_t(channels) * rate * bits >= 1000000000ull) {
    MessageBoxW(window,L"The profile exceeds supported limits or 1 Gbps of PCM traffic.",L"Pi AoIP", MB_OK | MB_ICONERROR);
    return TRUE;
  }
  wchar_t temporary[MAX_PATH + 40]{};
  std::swprintf(temporary,MAX_PATH+40,L"%ls.%lu.%lu.panel",state->path,GetCurrentProcessId(),GetCurrentThreadId());
  Config updated=state->current;
  std::snprintf(updated.peer,sizeof(updated.peer),"%s",peer);
  updated.rate=rate; updated.bits=uint16_t(bits); updated.block=block; updated.safety=guard;
  updated.energy_saving=IsDlgButtonChecked(window,3110)==BST_CHECKED;
  if(GetFileAttributesW(state->path)!=INVALID_FILE_ATTRIBUTES && !CopyFileW(state->path,temporary,FALSE)) {
    MessageBoxW(window,L"Could not create a copy of the current settings.",L"Pi AoIP",MB_OK|MB_ICONERROR); return TRUE;
  }
  if(!write_config_file(temporary,updated)) {
    DeleteFileW(temporary);
    MessageBoxW(window,L"Could not save settings. Check access to the profile folder.",L"Pi AoIP",MB_OK|MB_ICONERROR); return TRUE;
  }
  unsigned max_frames = (1472 - 40) / (channels * bits / 8);
  unsigned frames = device.v2 ? aoip::low_latency_frames(channels,rate,bits,block) : std::min(device.frames,max_frames);
  if (!profile_fits_link(channels, rate, bits, frames,
                         device.max_pps, device.link_mbps) ||
      !profile_fits_link(wire_outputs,rate,bits,aoip::packet_frames(wire_outputs,bits),device.max_pps,device.link_mbps)) {
    MessageBoxW(window,L"The format exceeds the device bandwidth or packet-rate limit.",L"Pi AoIP", MB_OK | MB_ICONERROR);
    DeleteFileW(temporary);
    return TRUE;
  }
  char request[128]{}, reply[512]{};
  sockaddr_in responder{};
  std::snprintf(request, sizeof(request),
      device.v2 ? "PIAOIP_SET_PROFILE_V2 channels=%u rate=%u bits=%u frames=%u outputs=%u" :
      "PIAOIP_SET_PROFILE_V1 channels=%u rate=%u bits=%u frames=%u",
      channels, rate, bits, frames, wire_outputs);
  SetDlgItemTextW(window,kStatus,L"Applying device profile…");
  const bool acknowledged=control_request(peer,nullptr,request,reply,responder,2000);
  if (acknowledged && (std::strcmp(reply,device.v2 ? "PIAOIP_PROFILE_PENDING_V2" : "PIAOIP_PROFILE_PENDING_V1") != 0 ||
      responder.sin_addr.s_addr != address.sin_addr.s_addr)) {
    DeleteFileW(temporary);
    MessageBoxW(window,L"Device rejected the profile. Windows settings are unchanged.",L"Pi AoIP", MB_OK | MB_ICONERROR);
    return TRUE;
  }
  // A lost UDP acknowledgement does not mean that the device rejected the
  // change. Reconcile the active profile before committing the Windows INI.
  bool active=false;
  for(unsigned attempt=0;attempt<24;++attempt) {
    SettingsDialog::Device applied{};
    if(query_device(peer,applied,200) && applied.channels==channels && applied.rate==rate &&
      applied.bits==bits && applied.outputs==wire_outputs && applied.frames==frames) { active=true; break; }
    Sleep(100);
  }
  if(!active) {
    DeleteFileW(temporary);
    std::snprintf(request,sizeof(request),device.v2 ? "PIAOIP_SET_PROFILE_V2 channels=%u rate=%u bits=%u frames=%u outputs=%u" :
      "PIAOIP_SET_PROFILE_V1 channels=%u rate=%u bits=%u frames=%u",device.channels,device.rate,device.bits,device.frames,device.outputs);
    control_request(peer,nullptr,request,reply,responder,500);
    MessageBoxW(window,L"Device did not confirm the profile. Restore of the previous profile was requested; check the connection.",L"Pi AoIP",MB_OK|MB_ICONERROR); return TRUE;
  }
  if (!MoveFileExW(temporary, state->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary);
    std::snprintf(request, sizeof(request),
        device.v2 ? "PIAOIP_SET_PROFILE_V2 channels=%u rate=%u bits=%u frames=%u outputs=%u" :
        "PIAOIP_SET_PROFILE_V1 channels=%u rate=%u bits=%u frames=%u",
        device.channels, device.rate, device.bits, device.frames, device.outputs);
    control_request(peer, nullptr, request, reply, responder, 500);
    MessageBoxW(window,L"Could not save the Windows profile. Restore of the previous Pi profile was requested.",L"Pi AoIP", MB_OK | MB_ICONERROR);
    return TRUE;
  }
  state->current=updated;
  state->changed = true;
  aoip::StreamDiagnostics stream;
  const bool local_stream=state->driver && static_cast<IASIO*>(state->driver)->future(aoip::stream_diagnostics_selector,&stream)==ASE_SUCCESS && stream.running;
  if(!local_stream) notify_settings_changed(state->path);
  EndDialog(window, IDOK);
  return TRUE;
}

bool show_settings_dialog(const Config& config, void* driver) {
  SettingsDialog state{}; state.current=config; state.driver=driver;
  if (!profile_path(state.path)) {
    MessageBoxA(nullptr, "Cannot find the ASIO profile path.", "Pi AoIP", MB_OK | MB_ICONERROR);
    return false;
  }
  char host[MAX_PATH]{};
  if (GetModuleFileNameA(nullptr, host, sizeof(host)))
    state.is_live = std::strstr(host, "Ableton Live") != nullptr;
  // A real dialog font fixes default system-font sizing and keeps layout in
  // dialog units at the host window's DPI. All controls retain keyboard access.
  alignas(DWORD) uint8_t storage[256]{};
  auto* dialog=reinterpret_cast<DLGTEMPLATE*>(storage);
  dialog->style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT|WS_VSCROLL|WS_HSCROLL;
  dialog->cx=320; dialog->cy=224;
  auto* extra=reinterpret_cast<WORD*>(storage+sizeof(DLGTEMPLATE));
  *extra++=0; *extra++=0; *extra++=0; *extra++=10;
  const wchar_t face[]=L"Segoe UI";
  std::memcpy(extra,face,sizeof(face));
  ACTCTXW context{}; context.cbSize=sizeof(context);
  context.dwFlags=ACTCTX_FLAG_HMODULE_VALID|ACTCTX_FLAG_RESOURCE_NAME_VALID;
  context.hModule=g_module; context.lpResourceName=MAKEINTRESOURCEW(2);
  HANDLE activation=CreateActCtxW(&context); ULONG_PTR cookie=0;
  if(activation!=INVALID_HANDLE_VALUE) ActivateActCtx(activation,&cookie);
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    if(cookie) DeactivateActCtx(0,cookie);
    if(activation!=INVALID_HANDLE_VALUE) ReleaseActCtx(activation);
    return false;
  }
  HWND owner=GetActiveWindow();
  // A hidden tray message window must not own a dialog that the user needs
  // to find in the taskbar. A visible ASIO host retains its normal ownership.
  if(owner && !IsWindowVisible(owner)) owner=nullptr;
  const auto result=DialogBoxIndirectParamW(g_module, dialog, owner, settings_dialog_proc,
      reinterpret_cast<LPARAM>(&state));
  if(result==-1) {
    char text[128]{}; std::snprintf(text,sizeof(text),"Cannot open Pi AoIP settings (Windows error %lu).",GetLastError());
    MessageBoxA(owner,text,"Pi AoIP",MB_OK|MB_ICONERROR);
  }
  if(state.scanner.joinable()) state.scanner.join();
  WSACleanup();
  if(state.heading_font) DeleteObject(state.heading_font);
  if(state.background) DeleteObject(state.background);
  if(state.surface) DeleteObject(state.surface);
  if(state.value_font) DeleteObject(state.value_font);
  if(cookie) DeactivateActCtx(0,cookie);
  if(activation!=INVALID_HANDLE_VALUE) ReleaseActCtx(activation);
  return state.changed;
}

#ifdef PIAOIP_PANEL_PREVIEW
// Render only our own invisible dialog into a memory bitmap. This does not
// activate a window, read the desktop, discover devices or change settings.
bool render_settings_preview(const wchar_t* path,unsigned scale,unsigned page) {
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
  SettingsDialog state{}; state.preview=true; state.current.block=64; state.current.safety=384;
  const bool channels=page>=4;
  ChannelDialog channel{state.current,page==4 ? 8u : 64u,page==4 ? 8u : 64u};
  alignas(DWORD) uint8_t storage[256]{};
  auto* dialog=reinterpret_cast<DLGTEMPLATE*>(storage);
  dialog->style=WS_POPUP|DS_SETFONT; dialog->cx=320; dialog->cy=224;
  if(channels) { dialog->cx=WORD(channel.width()); dialog->cy=WORD(channel.bottom()+64); }
  auto* extra=reinterpret_cast<WORD*>(storage+sizeof(DLGTEMPLATE));
  *extra++=0; *extra++=0; *extra++=0; *extra++=WORD(10*scale/100);
  const wchar_t face[]=L"Segoe UI"; std::memcpy(extra,face,sizeof(face));
  HWND window=CreateDialogIndirectParamW(g_module,dialog,nullptr,channels ? channel_dialog_proc : settings_dialog_proc,
    channels ? reinterpret_cast<LPARAM>(&channel) : reinterpret_cast<LPARAM>(&state));
  if(!window) return false;
  SendMessageW(window,WM_UPDATEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEACCEL|UISF_HIDEFOCUS),0);
  bool layout_ok=true;
  if(!channels) {
  SettingsDialog::Device device{}; std::strcpy(device.ip,"10.0.0.2"); device.channels=8; device.outputs=8;
  device.rate=192000; device.bits=32; device.frames=32; device.link_mbps=1000; device.max_pps=50000;
  device.v2=device.v3=true; device.rates={44100,48000,88200,96000,176400,192000}; device.supported_bits={16,24,32};
  device.buffers={16,32,64,128,256,512,1024,2048}; state.devices.push_back(device);
  SendDlgItemMessageW(window,3101,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"10.0.0.2"));
  SendDlgItemMessageW(window,3101,LB_SETCURSEL,0,0); show_device(window,state,0);
  SendMessageW(window,WM_TIMER,2,0);
  // Exercise the same navigation commands used by the menus, without opening
  // a popup or showing a window on the desktop.
  SendMessageW(window,WM_COMMAND,page_audio+std::min(page,unsigned(PanelPage::count)-1),0);
  layout_ok=state.page==PanelPage(page);
  for(size_t index=0;index<state.page_controls.size();++index)
    for(HWND control:state.page_controls[index])
      layout_ok &= bool(GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE)==(index==page);
  }
  RECT client{}; GetClientRect(window,&client);
  for(int id:{IDOK,IDCANCEL}) {
    HWND control=GetDlgItem(window,id); RECT r{};
    GetWindowRect(control,&r); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
    layout_ok &= control && (GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE) &&
      r.left>=0 && r.top>=0 && r.right<=client.right && r.bottom<=client.bottom;
  }
  BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=client.right;
  info.bmiHeader.biHeight=-client.bottom; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
  HDC dc=CreateCompatibleDC(nullptr); void* pixels=nullptr;
  HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
  if(!bitmap) { DeleteDC(dc); DestroyWindow(window); return false; }
  auto previous=SelectObject(dc,bitmap);
  FillRect(dc,&client,channels ? channel.background : state.background);
  SendMessageW(window,WM_PRINT, reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
  // Hidden native buttons can retain a stale clip region after switching pages
  // of the same size. Render visible owner-drawn buttons with their normal
  // painter and actual bounds, without showing the dialog to refresh clipping.
  for(HWND control=GetWindow(window,GW_CHILD);control;control=GetWindow(control,GW_HWNDNEXT)) {
    const auto style=GetWindowLongPtrW(control,GWL_STYLE);
    wchar_t cls[16]{}; GetClassNameW(control,cls,16);
    if(!(style&WS_VISIBLE) || std::wcscmp(cls,L"Button") || (style&BS_TYPEMASK)!=BS_OWNERDRAW) continue;
    RECT r{}; GetWindowRect(control,&r); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
    const int saved=SaveDC(dc);
    SetViewportOrgEx(dc,r.left,r.top,nullptr);
    DRAWITEMSTRUCT draw{}; draw.CtlType=ODT_BUTTON; draw.CtlID=GetDlgCtrlID(control);
    draw.itemAction=ODA_DRAWENTIRE; draw.itemState=(style&WS_DISABLED) ? ODS_DISABLED : 0;
    draw.hwndItem=control; draw.hDC=dc; draw.rcItem={0,0,r.right-r.left,r.bottom-r.top};
    draw_panel_button(draw); RestoreDC(dc,saved);
  }
  // Native EDIT/COMBOBOX omit their value when WM_PRINT targets an invisible
  // ancestor. Compose that text from the real control (selection, font, rect),
  // without ever showing the window or sampling pixels from the desktop.
  for(int id:{3000,3002,3003,3004,3010}) {
    HWND control=GetDlgItem(window,id);
    if(!(GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE)) continue;
    RECT r{}; GetWindowRect(control,&r); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
    wchar_t value[64]{}; GetWindowTextW(control,value,64);
    if(id==3000) {
      auto border=CreateSolidBrush(RGB(76,87,97)); FrameRect(dc,&r,border); DeleteObject(border);
    }
    InflateRect(&r,-3,-2); r.left+=4;
    if(id!=3000) r.right-=GetSystemMetrics(SM_CXVSCROLL)+2;
    FillRect(dc,&r,state.surface); SetTextColor(dc,panel_text); SetBkMode(dc,TRANSPARENT);
    auto font=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));
    DrawTextW(dc,value,-1,&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER); SelectObject(dc,font);
  }
  GdiFlush();
  BITMAPFILEHEADER header{}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
  const size_t bytes=size_t(client.right)*client.bottom*4; header.bfSize=DWORD(header.bfOffBits+bytes);
  FILE* output=_wfopen(path,L"wb"); bool okay=false;
  if(output) { okay=std::fwrite(&header,sizeof(header),1,output)==1 && std::fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,output)==1 &&
    std::fwrite(pixels,bytes,1,output)==1; std::fclose(output); }
  SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc); DestroyWindow(window);
  DeleteObject(state.heading_font); DeleteObject(state.value_font); DeleteObject(state.background); DeleteObject(state.surface);
  return okay && layout_ok;
}
#endif

}
