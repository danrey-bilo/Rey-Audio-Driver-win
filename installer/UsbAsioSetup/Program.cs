using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;
[assembly: AssemblyTitle("Rey Audio USB ASIO Setup")]
[assembly: AssemblyProduct("Rey Audio Driver USB ASIO")]
[assembly: AssemblyCompany("Rey Audio")]
[assembly: AssemblyVersion("2.8.1.2")]
[assembly: AssemblyFileVersion("2.8.1.2")]
namespace ReyAudio.UsbAsioSetup {
    internal static class Program {
        private const string OwnerKey = @"Software\ReyAudio\USBASIOSetup";
        private const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\ReyAudioUSBASIO";
        private const string RunValue = "ReyAudioUSBASIO";
        private static readonly string Target = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), @"ReyAudio\USBASIO");
        private static readonly string BinaryPath = "\"" + Path.Combine(Target, "ReyAudioService.exe") + "\" --service --asio-only";
        private static readonly StringBuilder Log = new StringBuilder();
        private static void Record(string text) { Log.AppendLine(DateTime.UtcNow.ToString("o") + " " + text); }
        private static void CheckDirectory(string path) {
            for (string current = Path.GetFullPath(path); !string.IsNullOrEmpty(current); current = Path.GetDirectoryName(current))
                if (Directory.Exists(current) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("Отказ: путь установки содержит перенаправление: " + current);
        }
        private static void CheckFile(string path) {
            if (File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Отказ: файл установки содержит перенаправление: " + path);
        }
        private static bool Owned() {
            using (var key = Registry.LocalMachine.OpenSubKey(OwnerKey))
                return key != null && string.Equals(Convert.ToString(key.GetValue("InstallLocation", "")), Target, StringComparison.OrdinalIgnoreCase);
        }
        private static void CheckOwnership() {
            CheckDirectory(Target);
            if (ServiceRegistration.Exists && (!Owned() || !string.Equals(ServiceRegistration.ImagePath, BinaryPath, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidOperationException("ReyAudioService уже установлен другим пакетом. Этот USB-ASIO установщик его не заменяет.");
            if (Directory.Exists(Target) && !Owned() && Directory.GetFileSystemEntries(Target).Length != 0)
                throw new InvalidOperationException("Папка USBASIO содержит другую установку. Автоматическая замена отменена.");
            foreach (var file in Payload.Hashes.Keys) CheckFile(Path.Combine(Target, file));
            CheckFile(Path.Combine(Target, "USBASIOSetup.exe")); CheckFile(Path.Combine(Target, "setup.log"));
        }
        private static string Quote(string text) {
            var result = new StringBuilder("\""); int slashes = 0;
            foreach (char c in text) {
                if (c == '\\') { ++slashes; continue; }
                result.Append('\\', c == '"' ? slashes * 2 + 1 : slashes); slashes = 0; result.Append(c);
            }
            result.Append('\\', slashes * 2); result.Append('"'); return result.ToString();
        }
        private static void RegisterDll(bool remove) {
            string exe = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "regsvr32.exe");
            using (var process = Process.Start(new ProcessStartInfo(exe, "/s " + (remove ? "/u " : "") + Quote(Path.Combine(Target, "ReyAudioAsio.dll"))) {
                UseShellExecute = false, CreateNoWindow = true })) {
                if (!process.WaitForExit(15000)) { process.Kill(); throw new TimeoutException("Тайм-аут регистрации ASIO."); }
                if (process.ExitCode != 0) throw new InvalidOperationException("Не удалось зарегистрировать ASIO DLL: " + process.ExitCode);
            }
        }
        private static byte[] ReadPayload(string file) {
            using (var source = Assembly.GetExecutingAssembly().GetManifestResourceStream("ReyAudio.Payload." + file))
            using (var memory = new MemoryStream()) {
                if (source == null) throw new InvalidOperationException("В пакете отсутствует " + file);
                source.CopyTo(memory); var bytes = memory.ToArray();
                using (var sha = SHA256.Create()) {
                    string hash = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
                    if (hash != Payload.Hashes[file]) throw new InvalidOperationException("Повреждён пакет: " + file);
                }
                return bytes;
            }
        }
        private static void WriteMetadata() {
            using (var key = Registry.LocalMachine.CreateSubKey(OwnerKey)) key.SetValue("InstallLocation", Target);
            using (var key = Registry.LocalMachine.CreateSubKey(UninstallKey)) {
                key.SetValue("DisplayName", "Rey Audio USB ASIO"); key.SetValue("DisplayVersion", "2.8.1-usb-preview.2");
                key.SetValue("Publisher", "Rey Audio"); key.SetValue("InstallLocation", Target);
                key.SetValue("DisplayIcon", Path.Combine(Target, "ReyAudioControl.exe"));
                key.SetValue("UninstallString", Quote(Path.Combine(Target, "USBASIOSetup.exe")) + " --uninstall");
                key.SetValue("NoModify", 1, RegistryValueKind.DWord); key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
            }
            using (var key = Registry.LocalMachine.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run"))
                key.SetValue(RunValue, Quote(Path.Combine(Target, "ReyAudioControl.exe")) + " --tray");
        }
        private static void RemoveMetadata() {
            using (var key = Registry.LocalMachine.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run", true)) {
                if (key != null && Convert.ToString(key.GetValue(RunValue, "")) == Quote(Path.Combine(Target, "ReyAudioControl.exe")) + " --tray")
                    key.DeleteValue(RunValue, false);
            }
            Registry.LocalMachine.DeleteSubKeyTree(UninstallKey, false); Registry.LocalMachine.DeleteSubKeyTree(OwnerKey, false);
        }
        private static void Install() {
            CheckOwnership();
            // Verify every embedded file before stopping or replacing an installation.
            var files = new Dictionary<string, byte[]>(); var previous = new Dictionary<string, byte[]>();
            foreach (var name in Payload.Hashes.Keys) {
                files[name] = ReadPayload(name); string path = Path.Combine(Target, name);
                if (File.Exists(path)) previous[name] = File.ReadAllBytes(path);
            }
            var replacements = new Dictionary<string, byte[]>();
            foreach (var file in files) {
                byte[] oldBytes;
                if (!previous.TryGetValue(file.Key, out oldBytes) || !file.Value.SequenceEqual(oldBytes))
                    replacements[file.Key] = file.Value;
            }
            bool existed = ServiceRegistration.Exists, owned = Owned();
            var changed = new List<string>();
            string asioDll = Path.Combine(Target, "ReyAudioAsio.dll");
            if (replacements.ContainsKey("ReyAudioAsio.dll") && File.Exists(asioDll))
                using (var access = new FileStream(asioDll, FileMode.Open, FileAccess.ReadWrite, FileShare.Read)) { }
            CloseOwnPanel(); ServiceRegistration.Stop(); Directory.CreateDirectory(Target);
            try {
                // Check locks before replacing any payload. A DAW may retain the
                // COM DLL after choosing No Device; that requires closing the host.
                foreach (var name in previous.Keys.Where(replacements.ContainsKey))
                    using (var access = new FileStream(Path.Combine(Target, name), FileMode.Open, FileAccess.ReadWrite, FileShare.Read)) { }
                using (var key = Registry.LocalMachine.CreateSubKey(OwnerKey)) key.SetValue("InstallLocation", Target);
                foreach (var file in replacements) { changed.Add(file.Key); File.WriteAllBytes(Path.Combine(Target, file.Key), file.Value); }
                string self = Assembly.GetExecutingAssembly().Location, installedSetup = Path.Combine(Target, "USBASIOSetup.exe");
                if (!string.Equals(Path.GetFullPath(self), installedSetup, StringComparison.OrdinalIgnoreCase)) File.Copy(self, installedSetup, true);
                RegisterDll(false); ServiceRegistration.Register(BinaryPath); ServiceRegistration.Start(); WriteMetadata();
                Record("INSTALL_OK: USB ASIO registered; auto-start service Running; replaced " +
                    string.Join(", ", changed) + "; no kernel/boot changes.");
            } catch {
                try {
                    ServiceRegistration.Stop();
                    if (!existed) { ServiceRegistration.Remove(); if (File.Exists(Path.Combine(Target, "ReyAudioAsio.dll"))) RegisterDll(true); }
                    foreach (var name in changed) {
                        if (previous.ContainsKey(name)) File.WriteAllBytes(Path.Combine(Target, name), previous[name]);
                        else File.Delete(Path.Combine(Target, name));
                    }
                    if (existed) { RegisterDll(false); ServiceRegistration.Start(); }
                    if (!owned) { RemoveMetadata(); File.Delete(Path.Combine(Target, "USBASIOSetup.exe")); }
                    Record("ROLLBACK_OK");
                } catch (Exception rollback) { Record("ROLLBACK_ERROR: " + rollback); }
                throw;
            }
        }
        private static void Uninstall() {
            CheckOwnership(); if (!Owned()) throw new InvalidOperationException("Установка USB-ASIO не найдена.");
            ServiceRegistration.Stop();
            RegisterDll(true); ServiceRegistration.Remove();
            CloseOwnPanel();
            foreach (var name in Payload.Hashes.Keys) File.Delete(Path.Combine(Target, name));
            RemoveMetadata();
            using (var key = Registry.LocalMachine.CreateSubKey(OwnerKey)) {
                key.SetValue("InstallLocation", Target); key.SetValue("State", "Removed");
            }
            // The running setup and readable log are retained; no recursive deletion.
            Record("UNINSTALL_OK: own service, ASIO registration and payload removed; card profiles retained.");
        }
        private static void CloseOwnPanel() {
            foreach (var process in Process.GetProcessesByName("ReyAudioControl")) {
                try { if (string.Equals(process.MainModule.FileName, Path.Combine(Target, "ReyAudioControl.exe"), StringComparison.OrdinalIgnoreCase)) { process.Kill(); process.WaitForExit(5000); } }
                finally { process.Dispose(); }
            }
        }
        private static void UpdatePanel() {
            CheckOwnership();
            if (!Owned() || !ServiceRegistration.Exists) throw new InvalidOperationException("Сначала установите USB-ASIO.");
            var bytes = ReadPayload("ReyAudioControl.exe");
            CloseOwnPanel(); File.WriteAllBytes(Path.Combine(Target, "ReyAudioControl.exe"), bytes);
            string self = Assembly.GetExecutingAssembly().Location, installedSetup = Path.Combine(Target, "USBASIOSetup.exe");
            if (!string.Equals(Path.GetFullPath(self), installedSetup, StringComparison.OrdinalIgnoreCase)) File.Copy(self, installedSetup, true);
            WriteMetadata();
            Record("PANEL_UPDATE_OK: own panel replaced; service and ASIO DLL unchanged.");
        }
        [STAThread]
        public static int Main(string[] args) {
            bool quiet = Array.IndexOf(args, "--quiet") >= 0, remove = Array.IndexOf(args, "--uninstall") >= 0;
            bool panel = Array.IndexOf(args, "--panel-only") >= 0;
            bool elevated = Array.IndexOf(args, "--elevated") >= 0;
            if (remove && panel) return 2;
            foreach (var arg in args) if (arg != "--quiet" && arg != "--uninstall" && arg != "--panel-only" && arg != "--elevated") return 2;
            if (!InteractiveStartup.IsAdministrator) {
                if (elevated) return 1;
                return InteractiveStartup.Elevate(args, quiet, remove, Target);
            }
            bool created;
            using (var mutex = new Mutex(true, @"Global\ReyAudio.UsbAsioSetup", out created)) {
                if (!created) return 3;
                int result = 0;
                try {
                    if (!Environment.Is64BitOperatingSystem || !Environment.Is64BitProcess) throw new InvalidOperationException("Требуется Windows x64.");
                    Record(remove ? "UNINSTALL_BEGIN" : panel ? "PANEL_UPDATE_BEGIN" : "INSTALL_BEGIN");
                    if (remove) Uninstall(); else if (panel) UpdatePanel(); else Install();
                    if (!quiet) MessageBox.Show(remove ? "Rey Audio Driver удалён." : "Rey Audio Driver установлен.", "Rey Audio Driver", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    if (!remove && !elevated) InteractiveStartup.StartPanel(Target);
                } catch (Exception error) {
                    result = 1; Record("ERROR: " + error);
                    if (!quiet) MessageBox.Show(error.Message, "Rey Audio USB ASIO", MessageBoxButtons.OK, MessageBoxIcon.Error);
                } finally {
                    if (Directory.Exists(Target)) {
                        try { CheckDirectory(Target); CheckFile(Path.Combine(Target, "setup.log")); File.AppendAllText(Path.Combine(Target, "setup.log"), Log.ToString(), Encoding.UTF8); }
                        catch { }
                    }
                }
                return result;
            }
        }
    }
}
