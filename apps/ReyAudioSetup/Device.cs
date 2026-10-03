using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;

namespace ReyAudio.Setup {
    internal sealed class Readiness {
        public bool QuerySucceeded, TestMode, Hvci, Administrator, Ready;
        public bool? SecureBoot;
        public int WindowsBuild;
        public uint CodeIntegrityOptions;
        public string Reason;
        [StructLayout(LayoutKind.Sequential)] struct Ci { public uint Length, Options; }
        [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int information, ref Ci data, uint length, IntPtr returned);
        public static Readiness Read() {
            var result = new Readiness();
            var ci = new Ci { Length = 8 };
            result.QuerySucceeded = NtQuerySystemInformation(103, ref ci, 8, IntPtr.Zero) == 0;
            result.CodeIntegrityOptions = ci.Options; result.TestMode = (ci.Options & 2) != 0; result.Hvci = (ci.Options & 0x400) != 0;
            using (var identity = System.Security.Principal.WindowsIdentity.GetCurrent())
                result.Administrator = new System.Security.Principal.WindowsPrincipal(identity).IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);
            using (var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)) {
                using (var key = machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\SecureBoot\State")) {
                    var enabled = key == null ? null : key.GetValue("UEFISecureBootEnabled");
                    if (enabled is int) result.SecureBoot = (int)enabled != 0;
                }
                using (var key = machine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion"))
                    int.TryParse(Convert.ToString(key == null ? null : key.GetValue("CurrentBuildNumber")), out result.WindowsBuild);
            }
            result.Ready = Environment.Is64BitProcess && result.WindowsBuild >= 22621 && result.QuerySucceeded && result.TestMode && result.SecureBoot != true;
            result.Reason = !Environment.Is64BitProcess || result.WindowsBuild < 22621 ? "Нужна Windows 11 x64, версия 22H2 или новее." :
                !result.QuerySucceeded ? "Не удалось проверить политику загрузки драйверов Windows." :
                result.SecureBoot == true ? "Отключите Secure Boot в BIOS/UEFI. Затем включите Test Mode и перезагрузите ПК." :
                !result.TestMode ? "Включите Test Mode и перезагрузите ПК. Платный сертификат не нужен." : "ПК подготовлен. Можно установить Rey Audio Driver.";
            return result;
        }
    }
    internal static class Device {
        public const string HardwareId = @"Root\ReyAudioAcx";
        static Guid Media = new Guid("4D36E96C-E325-11CE-BFC1-08002BE10318");
        [StructLayout(LayoutKind.Sequential)] struct Info { public uint Size; public Guid Class; public uint Instance; public IntPtr Reserved; }
        [StructLayout(LayoutKind.Sequential)] struct RemoveParams { public uint Size, Function, Scope, Profile; }
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr SetupDiGetClassDevsW(ref Guid guid, string enumerator, IntPtr window, uint flags);
        [DllImport("setupapi.dll", SetLastError=true)] static extern bool SetupDiEnumDeviceInfo(IntPtr set, uint index, ref Info info);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr set, ref Info info, uint property, out uint type, byte[] buffer, uint length, out uint required);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool SetupDiGetDeviceInstanceIdW(IntPtr set, ref Info info, StringBuilder id, uint size, out uint required);
        [DllImport("setupapi.dll", SetLastError=true)] static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid guid, IntPtr window);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool SetupDiCreateDeviceInfoW(IntPtr set, string name, ref Guid guid, string description, IntPtr window, uint flags, ref Info info);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool SetupDiSetDeviceRegistryPropertyW(IntPtr set, ref Info info, uint property, byte[] value, uint length);
        [DllImport("setupapi.dll", SetLastError=true)] static extern bool SetupDiCallClassInstaller(uint function, IntPtr set, ref Info info);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool SetupDiSetClassInstallParamsW(IntPtr set, ref Info info, ref RemoveParams parameters, uint length);
        [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
        [DllImport("newdev.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool UpdateDriverForPlugAndPlayDevicesW(IntPtr window, string hardwareId, string inf, uint flags, out bool reboot);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool SetupUninstallOEMInfW(string name, uint flags, IntPtr reserved);
        [DllImport("cfgmgr32.dll")] static extern uint CM_Get_DevNode_Status(out uint status, out uint problem, uint instance, uint flags);
        static Info NewInfo() { return new Info { Size = (uint)Marshal.SizeOf(typeof(Info)) }; }
        static void Check(bool ok) { if (!ok) throw new Win32Exception(Marshal.GetLastWin32Error()); }
        static string Property(IntPtr set, ref Info info, uint property) {
            var bytes = new byte[4096]; uint type, required;
            if (!SetupDiGetDeviceRegistryPropertyW(set, ref info, property, out type, bytes, (uint)bytes.Length, out required)) {
                if (Marshal.GetLastWin32Error() == 13) return "";
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            if (required > bytes.Length || required % 2 != 0) throw new InvalidOperationException("Invalid device property.");
            return Encoding.Unicode.GetString(bytes, 0, (int)required).TrimEnd('\0');
        }
        static string Id(IntPtr set, ref Info info) { var id = new StringBuilder(512); uint required; Check(SetupDiGetDeviceInstanceIdW(set, ref info, id, 512, out required)); return id.ToString(); }
        public static List<string> Roots() {
            // Include disconnected own roots to avoid duplicates on repair.
            var roots = new List<string>(); var set = SetupDiGetClassDevsW(ref Media, "ROOT", IntPtr.Zero, 0);
            if (set == new IntPtr(-1)) throw new Win32Exception();
            try {
                for (uint i = 0; ; ++i) {
                    var info = NewInfo();
                    if (!SetupDiEnumDeviceInfo(set, i, ref info)) { if (Marshal.GetLastWin32Error() == 259) break; throw new Win32Exception(); }
                    var hardware = Property(set, ref info, 1).Split('\0');
                    if (Array.Exists(hardware, s => string.Equals(s, HardwareId, StringComparison.OrdinalIgnoreCase))) {
                        var service = Property(set, ref info, 4);
                        if (service.Length != 0 && service != "ReyAudioAcx") throw new InvalidOperationException("Hardware ID belongs to another driver.");
                        roots.Add(Id(set, ref info));
                    }
                }
            } finally { SetupDiDestroyDeviceInfoList(set); }
            if (roots.Count > 1) throw new InvalidOperationException("Multiple Rey root devices found; inspect before installation.");
            return roots;
        }
        public static string Create() {
            var set = SetupDiCreateDeviceInfoList(ref Media, IntPtr.Zero);
            if (set == new IntPtr(-1)) throw new Win32Exception();
            try {
                var info = NewInfo(); Check(SetupDiCreateDeviceInfoW(set, "MEDIA", ref Media, "Rey Audio Driver", IntPtr.Zero, 1, ref info));
                var hardware = Encoding.Unicode.GetBytes(HardwareId + "\0\0");
                Check(SetupDiSetDeviceRegistryPropertyW(set, ref info, 1, hardware, (uint)hardware.Length));
                Check(SetupDiCallClassInstaller(0x19, set, ref info)); return Id(set, ref info);
            } finally { SetupDiDestroyDeviceInfoList(set); }
        }
        public static bool Bind(string inf) { bool reboot; Check(UpdateDriverForPlugAndPlayDevicesW(IntPtr.Zero, HardwareId, inf, 1, out reboot)); return reboot; }
        public static void RequireStarted(string expected) {
            var set = SetupDiGetClassDevsW(ref Media, "ROOT", IntPtr.Zero, 2);
            if (set == new IntPtr(-1)) throw new Win32Exception();
            try {
                for (uint i = 0; ; ++i) {
                    var info = NewInfo();
                    if (!SetupDiEnumDeviceInfo(set, i, ref info)) { if (Marshal.GetLastWin32Error() == 259) break; throw new Win32Exception(); }
                    if (!string.Equals(Id(set, ref info), expected, StringComparison.OrdinalIgnoreCase)) continue;
                    uint status, problem;
                    if (CM_Get_DevNode_Status(out status, out problem, info.Instance, 0) != 0 || problem != 0 || (status & 8) == 0)
                        throw new InvalidOperationException("Windows не запустила Rey Audio Driver. Код проблемы устройства: " + problem + ". Сохраните запись Code Integrity; настройки HVCI автоматически не изменяются.");
                    return;
                }
                throw new InvalidOperationException("Rey root device not found after driver binding.");
            } finally { SetupDiDestroyDeviceInfoList(set); }
        }
        public static void Remove(string expected) {
            var set = SetupDiGetClassDevsW(ref Media, "ROOT", IntPtr.Zero, 0);
            if (set == new IntPtr(-1)) throw new Win32Exception();
            try {
                for (uint i = 0; ; ++i) {
                    var info = NewInfo();
                    if (!SetupDiEnumDeviceInfo(set, i, ref info)) { if (Marshal.GetLastWin32Error() == 259) break; throw new Win32Exception(); }
                    if (!string.Equals(Id(set, ref info), expected, StringComparison.OrdinalIgnoreCase)) continue;
                    if (!Array.Exists(Property(set, ref info, 1).Split('\0'), s => string.Equals(s, HardwareId, StringComparison.OrdinalIgnoreCase))) throw new InvalidOperationException("Unexpected root device.");
                    var p = new RemoveParams { Size = 8, Function = 5, Scope = 1 };
                    Check(SetupDiSetClassInstallParamsW(set, ref info, ref p, (uint)Marshal.SizeOf(typeof(RemoveParams)))); Check(SetupDiCallClassInstaller(5, set, ref info)); return;
                }
            } finally { SetupDiDestroyDeviceInfoList(set); }
        }
    }
}
