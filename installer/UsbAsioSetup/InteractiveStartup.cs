using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Principal;
using System.Windows.Forms;

namespace ReyAudio.UsbAsioSetup {
    internal static class InteractiveStartup {
        public static bool IsAdministrator {
            get {
                using (var identity = WindowsIdentity.GetCurrent())
                    return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
            }
        }

        public static void StartPanel(string directory) {
            if (Process.GetCurrentProcess().SessionId == 0) return;
            Process.Start(new ProcessStartInfo(Path.Combine(directory, "ReyAudioControl.exe"), "--tray") {
                UseShellExecute = true, WindowStyle = ProcessWindowStyle.Hidden
            });
        }

        public static int Elevate(string[] arguments, bool quiet, bool remove, string directory) {
            try {
                // The normal desktop process stays alive while the installer
                // child handles UAC. It then launches the tray as the user,
                // without passing an elevated token to the control panel.
                var start = new ProcessStartInfo(Assembly.GetExecutingAssembly().Location,
                    string.Join(" ", arguments.Concat(new[] { "--elevated" }))) {
                    UseShellExecute = true, Verb = "runas", WindowStyle = ProcessWindowStyle.Hidden
                };
                using (var process = Process.Start(start)) {
                    process.WaitForExit();
                    if (process.ExitCode == 0 && !remove) StartPanel(directory);
                    return process.ExitCode;
                }
            } catch (Win32Exception error) {
                if (error.NativeErrorCode != 1223 && !quiet)
                    MessageBox.Show(error.Message, "Rey Audio Driver", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return error.NativeErrorCode == 1223 ? 2 : 1;
            }
        }
    }
}
