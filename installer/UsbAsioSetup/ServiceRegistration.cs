using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.ServiceProcess;
using Microsoft.Win32;
namespace ReyAudio.UsbAsioSetup {
    internal static class ServiceRegistration {
        public const string Name = "ReyAudioService";
        private const uint AllAccess = 0xF01FF, NoChange = 0xFFFFFFFF;
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr OpenSCManager(string machine, string database, uint access);
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr OpenService(IntPtr manager, string name, uint access);
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateService(IntPtr manager, string name, string display, uint access,
            uint type, uint start, uint error, string binary, string group, IntPtr tag, string dependencies, string account, string password);
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool ChangeServiceConfig(IntPtr service, uint type, uint start, uint error,
            string binary, string group, IntPtr tag, string dependencies, string account, string password, string display);
        [DllImport("advapi32.dll", SetLastError = true)] private static extern bool DeleteService(IntPtr service);
        [DllImport("advapi32.dll")] private static extern bool CloseServiceHandle(IntPtr handle);
        public static string ImagePath {
            get {
                using (var key = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\" + Name))
                    return key == null ? null : Convert.ToString(key.GetValue("ImagePath", ""));
            }
        }
        public static bool Exists { get { return ImagePath != null; } }
        public static void Stop() {
            if (!Exists) return;
            using (var service = new ServiceController(Name)) {
                service.Refresh();
                if (service.Status == ServiceControllerStatus.Stopped) return;
                if (service.Status != ServiceControllerStatus.StopPending) service.Stop();
                service.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(20));
            }
        }
        public static void Start() {
            using (var service = new ServiceController(Name)) {
                service.Refresh();
                if (service.Status != ServiceControllerStatus.Running) {
                    if (service.Status != ServiceControllerStatus.StartPending) service.Start();
                    service.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(20));
                }
            }
        }
        public static void Register(string binary) {
            var manager = OpenSCManager(null, null, 3);
            if (manager == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            IntPtr service = IntPtr.Zero;
            try {
                if (Exists) {
                    service = OpenService(manager, Name, AllAccess);
                    if (service == IntPtr.Zero || !ChangeServiceConfig(service, NoChange, 2, NoChange, binary,
                        null, IntPtr.Zero, null, null, null, "Rey Audio USB ASIO Service"))
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                } else {
                    service = CreateService(manager, Name, "Rey Audio USB ASIO Service", AllAccess, 0x10,
                        2, 1, binary, null, IntPtr.Zero, null, null, null);
                    if (service == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
                }
            } finally { if (service != IntPtr.Zero) CloseServiceHandle(service); CloseServiceHandle(manager); }
        }
        public static void Remove() {
            if (!Exists) return;
            Stop();
            var manager = OpenSCManager(null, null, 1);
            if (manager == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            IntPtr service = IntPtr.Zero;
            try {
                service = OpenService(manager, Name, AllAccess);
                if (service == IntPtr.Zero || !DeleteService(service)) throw new Win32Exception(Marshal.GetLastWin32Error());
            } finally { if (service != IntPtr.Zero) CloseServiceHandle(service); CloseServiceHandle(manager); }
        }
    }
}
