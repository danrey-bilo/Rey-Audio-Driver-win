#include "firewall.hpp"
#include <windows.h>
#include <oleauto.h>
#include <cwchar>
namespace rey {
namespace {
HRESULT property(IDispatch *object, const wchar_t *name, WORD flags, VARIANT &result, VARIANT *argument = nullptr) {
  DISPID id; LPOLESTR names[] = {const_cast<wchar_t *>(name)};
  auto status = object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_INVARIANT, &id);
  if (FAILED(status)) return status;
  DISPID put = DISPID_PROPERTYPUT;
  DISPPARAMS parameters{argument, flags == DISPATCH_PROPERTYPUT ? &put : nullptr, argument ? 1u : 0u, flags == DISPATCH_PROPERTYPUT ? 1u : 0u};
  return object->Invoke(id, IID_NULL, LOCALE_INVARIANT, flags, &parameters, &result, nullptr, nullptr);
}
}
bool update_lan_firewall(uint16_t port, std::string &error) {
  const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IDispatch *policy = nullptr;
  const CLSID clsid = {0xe2b3c97f, 0x6ae1, 0x41ac, {0x81, 0x7a, 0xf6, 0xf9, 0x21, 0x66, 0xd7, 0xdd}};
  HRESULT status = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, reinterpret_cast<void **>(&policy));
  VARIANT rules{}, rule{}, application{}, argument{}, unused{};
  argument.vt = VT_BSTR; argument.bstrVal = SysAllocString(L"Rey Audio LAN transport");
  if (SUCCEEDED(status)) status = property(policy, L"Rules", DISPATCH_PROPERTYGET, rules);
  if (SUCCEEDED(status) && rules.vt != VT_DISPATCH) status = E_UNEXPECTED;
  if (SUCCEEDED(status)) status = property(rules.pdispVal, L"Item", DISPATCH_METHOD | DISPATCH_PROPERTYGET, rule, &argument);
  if (SUCCEEDED(status) && rule.vt != VT_DISPATCH) status = E_UNEXPECTED;
  if (SUCCEEDED(status)) status = property(rule.pdispVal, L"ApplicationName", DISPATCH_PROPERTYGET, application);
  wchar_t own[MAX_PATH]{}; GetModuleFileNameW(nullptr, own, MAX_PATH);
  if (SUCCEEDED(status) && (application.vt != VT_BSTR || !application.bstrVal || _wcsicmp(application.bstrVal, own))) status = E_ACCESSDENIED;
  if (SUCCEEDED(status)) {
    wchar_t text[16]{}; std::swprintf(text, 16, L"%u", unsigned(port));
    VariantClear(&argument); argument.vt = VT_BSTR; argument.bstrVal = SysAllocString(text);
    status = property(rule.pdispVal, L"LocalPorts", DISPATCH_PROPERTYPUT, unused, &argument);
  }
  for (auto *v : {&unused, &argument, &application, &rule, &rules}) VariantClear(v);
  if (policy) policy->Release();
  if (SUCCEEDED(initialized)) CoUninitialize();
  if (FAILED(status)) error = "Cannot update the installed LAN firewall rule: " + std::to_string(uint32_t(status));
  return SUCCEEDED(status);
}
}
