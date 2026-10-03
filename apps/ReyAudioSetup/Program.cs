using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Security.Principal;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;
[assembly: AssemblyTitle("Rey Audio Driver Setup")]
[assembly: AssemblyProduct("Rey Audio Driver")]
[assembly: AssemblyCompany("Rey Audio")]
[assembly: AssemblyVersion("2.6.1.0")]
namespace ReyAudio.Setup {
    internal static class DriverInstall {
        static string StatePath = @"Software\ReyAudio\InstallerState";
        public static string Root { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "ReyAudio"); } }
        static void ProtectedDirectory(string path) {
            for (string p = Path.GetFullPath(path); p != null; p = Path.GetDirectoryName(p))
                if (Directory.Exists(p) && (File.GetAttributes(p) & FileAttributes.ReparsePoint) != 0) throw new IOException("Reparse installation directory.");
            Directory.CreateDirectory(path);
            var acl = new DirectorySecurity(); acl.SetAccessRuleProtection(true, false);
            foreach (string id in new[] { "S-1-5-18", "S-1-5-32-544" }) acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(id), FileSystemRights.FullControl, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier("S-1-5-32-545"), FileSystemRights.ReadAndExecute, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            Directory.SetAccessControl(path, acl);
        }
        public static void ValidatePayload(string input) {
            foreach (var pair in Payload.Hashes) {
                string path = Path.Combine(input, pair.Key);
                if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0 || Signing.Hash(path) != pair.Value) throw new InvalidDataException("Driver payload hash mismatch: " + pair.Key);
            }
        }
        public static Dictionary<string, object> Prepare(string input, string output, bool machine) {
            ValidatePayload(input);
            if (Directory.Exists(output)) throw new IOException("Signing output must be a new directory.");
            if (machine) ProtectedDirectory(output); else Directory.CreateDirectory(output);
            foreach (var pair in Payload.Hashes) File.Copy(Path.Combine(input, pair.Key), Path.Combine(output, pair.Key), false);
            ValidatePayload(output);
            var parameters = new CspParameters(24, "Microsoft Enhanced RSA and AES Cryptographic Provider", "ReyAudioTemporary-" + Guid.NewGuid().ToString("N")) {
                KeyNumber = 2, Flags = CspProviderFlags.UseNonExportableKey | CspProviderFlags.NoPrompt | (machine ? CspProviderFlags.UseMachineKeyStore : 0)
            };
            string thumbprint;
            using (var key = new RSACryptoServiceProvider(3072, parameters)) {
                try {
                    using (var certificate = Signing.CreateCertificate(key)) {
                        thumbprint = certificate.Thumbprint;
                        Signing.Sign(Path.Combine(output, "ReyAudioAcx.sys"), certificate);
                        // PE SIP hashes exclude the embedded signature. Verify the
                        // unchanged catalog's SYS membership rather than assuming it.
                        Signing.Sign(Path.Combine(output, "reyaudioacx.cat"), certificate);
                        File.WriteAllBytes(Path.Combine(output, "local-test.cer"), certificate.Export(X509ContentType.Cert));
                    }
                } finally { key.PersistKeyInCsp = false; key.Clear(); }
            }
            string sys = Path.Combine(output, "ReyAudioAcx.sys"), cat = Path.Combine(output, "reyaudioacx.cat"), inf = Path.Combine(output, "ReyAudioAcx.inf");
            Signing.RequireSignature(sys, thumbprint, true); Signing.RequireSignature(cat, thumbprint, true);
            Signing.RequireStatus(Signing.VerifyMember(cat, sys), true); Signing.RequireStatus(Signing.VerifyMember(cat, inf), true);
            return new Dictionary<string, object> { { "local_test_signing", true }, { "thumbprint", thumbprint },
                { "source_sys_sha256", Payload.Hashes["ReyAudioAcx.sys"] }, { "signed_sys_sha256", Signing.Hash(sys) },
                { "signed_cat_sha256", Signing.Hash(cat) }, { "catalog_members_verified", true },
                { "private_key_exported", false }, { "private_key_deleted", true }, { "paid_certificate_required", false },
                { "driver_loaded", false }, { "created_utc", DateTime.UtcNow.ToString("o") } };
        }
        static RegistryKey State(bool write) {
            using (var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                return write ? machine.CreateSubKey(StatePath) : machine.OpenSubKey(StatePath);
        }
        static void Trust(X509Certificate2 cert, StoreName name, bool add) {
            if (!cert.Subject.StartsWith("CN=Rey Audio local test ", StringComparison.Ordinal) || cert.Subject != cert.Issuer) throw new InvalidDataException("Unexpected local test certificate.");
            using (var store = new X509Store(name, StoreLocation.LocalMachine)) {
                store.Open(OpenFlags.ReadWrite);
                if (add) store.Add(cert);
                else foreach (var found in store.Certificates.Find(X509FindType.FindByThumbprint, cert.Thumbprint, false)) store.Remove(found);
            }
        }
        static void RemoveTrust(string folder, string thumbprint) {
            if (string.IsNullOrEmpty(folder) || string.IsNullOrEmpty(thumbprint)) return;
            var absolute = Path.GetFullPath(folder);
            if (!absolute.StartsWith(Path.GetFullPath(Path.Combine(Root, "Driver")) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new IOException("Unexpected driver state path.");
            using (var cert = new X509Certificate2(Path.Combine(absolute, "local-test.cer"))) {
                if (!string.Equals(cert.Thumbprint, thumbprint, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Unexpected certificate state.");
                Trust(cert, StoreName.TrustedPublisher, false); Trust(cert, StoreName.Root, false);
            }
        }
        public static void Install(string folder) {
            var readiness = Readiness.Read();
            if (!readiness.Administrator || !readiness.Ready) throw new InvalidOperationException(readiness.Reason + " Установка требует прав администратора.");
            var input = Path.Combine(Path.GetFullPath(folder), "driver"); ValidatePayload(input);
            var roots = Device.Roots();
            using (var old = State(false)) {
                if (old != null && roots.Count == 1 && string.Equals(Convert.ToString(old.GetValue("RootInstance")), roots[0], StringComparison.OrdinalIgnoreCase)) {
                    if (Convert.ToString(old.GetValue("SourceSysSha256")) != Payload.Hashes["ReyAudioAcx.sys"])
                        throw new InvalidOperationException("Удалите предыдущий экспериментальный kernel-драйвер через его MSI перед сменой версии SYS. Настройки сохранятся.");
                    string active = Convert.ToString(old.GetValue("SignedFolder")), thumb = Convert.ToString(old.GetValue("Thumbprint"));
                    Signing.RequireSignature(Path.Combine(active, "ReyAudioAcx.sys"), thumb, false);
                    Signing.RequireSignature(Path.Combine(active, "reyaudioacx.cat"), thumb, false);
                    Signing.RequireStatus(Signing.VerifyMember(Path.Combine(active, "reyaudioacx.cat"), Path.Combine(active, "ReyAudioAcx.sys")), false);
                    Signing.RequireStatus(Signing.VerifyMember(Path.Combine(active, "reyaudioacx.cat"), Path.Combine(active, "ReyAudioAcx.inf")), false);
                    bool reboot = Device.Bind(Path.Combine(active, "ReyAudioAcx.inf"));
                    if (!reboot) Device.RequireStarted(roots[0]);
                    old.Close(); using (var state = State(true)) {
                        state.SetValue("RollbackCreatedRoot", 0); state.SetValue("RebootRequired", reboot ? 1 : 0);
                    }
                    return;
                }
            }
            if (roots.Count != 0) throw new InvalidOperationException("Найден Rey root без состояния этого установщика. Удалите прежнюю тестовую установку перед продолжением.");
            ProtectedDirectory(Root); ProtectedDirectory(Path.Combine(Root, "Driver"));
            string output = Path.Combine(Root, "Driver", Guid.NewGuid().ToString("N"));
            var proof = Prepare(input, output, true); string thumbprint = Convert.ToString(proof["thumbprint"]), created = "";
            try {
                using (var cert = new X509Certificate2(Path.Combine(output, "local-test.cer"))) { Trust(cert, StoreName.Root, true); Trust(cert, StoreName.TrustedPublisher, true); }
                Signing.RequireSignature(Path.Combine(output, "ReyAudioAcx.sys"), thumbprint, false);
                Signing.RequireSignature(Path.Combine(output, "reyaudioacx.cat"), thumbprint, false);
                Signing.RequireStatus(Signing.VerifyMember(Path.Combine(output, "reyaudioacx.cat"), Path.Combine(output, "ReyAudioAcx.sys")), false);
                Signing.RequireStatus(Signing.VerifyMember(Path.Combine(output, "reyaudioacx.cat"), Path.Combine(output, "ReyAudioAcx.inf")), false);
                created = Device.Create(); bool reboot = Device.Bind(Path.Combine(output, "ReyAudioAcx.inf"));
                if (!reboot) Device.RequireStarted(created);
                using (var state = State(true)) {
                    state.SetValue("SourceSysSha256", Payload.Hashes["ReyAudioAcx.sys"]); state.SetValue("SignedFolder", output);
                    state.SetValue("Thumbprint", thumbprint); state.SetValue("RootInstance", created); state.SetValue("RollbackCreatedRoot", 1);
                    state.SetValue("RebootRequired", reboot ? 1 : 0);
                }
            } catch {
                try { if (created.Length != 0) Device.Remove(created); }
                finally { RemoveTrust(output, thumbprint); }
                throw;
            }
        }
        public static void Remove(bool rollback) {
            if (!Readiness.Read().Administrator) throw new UnauthorizedAccessException("Administrator required.");
            using (var state = State(true)) {
                if (rollback && Convert.ToInt32(state.GetValue("RollbackCreatedRoot", 0)) != 1) return;
                string root = Convert.ToString(state.GetValue("RootInstance")), output = Convert.ToString(state.GetValue("SignedFolder")), thumb = Convert.ToString(state.GetValue("Thumbprint"));
                if (root.Length != 0) Device.Remove(root);
                RemoveTrust(output, thumb);
                foreach (var name in new[] { "RootInstance", "SignedFolder", "Thumbprint", "SourceSysSha256", "RollbackCreatedRoot", "RebootRequired" }) state.DeleteValue(name, false);
            }
            // Retain signed public files and profiles for diagnosis. No shared
            // devices, certificate stores or driver packages are deleted broadly.
        }
        public static void Commit() { using (var state = State(true)) state.DeleteValue("RollbackCreatedRoot", false); }
    }
    internal static class Program {
        static void Json(string path, object data) { File.WriteAllText(path, new JavaScriptSerializer().Serialize(data)); }
        [STAThread] public static int Main(string[] args) {
            try {
                if (args.Length == 2 && args[0] == "--check") { Json(args[1], Readiness.Read()); return 0; }
                if (args.Length == 1 && args[0] == "--msi-check") {
                    var ready = Readiness.Read(); if (ready.Ready) return 0;
                    MessageBox.Show(ready.Reason + "\n\nНа обычной Windows самоподпись MSI не заменяет разрешённую подпись kernel-драйвера. Откройте Rey Audio Setup.exe для подготовки Test Mode.", "Rey Audio Driver", MessageBoxButtons.OK, MessageBoxIcon.Information); return 1603;
                }
                if (args.Length == 4 && args[0] == "--sign-test") { Json(args[3], DriverInstall.Prepare(Path.GetFullPath(args[1]), Path.GetFullPath(args[2]), false)); return 0; }
                if (args.Length == 3 && args[0] == "--verify-test") {
                    string folder = Path.GetFullPath(args[1]), cat = Path.Combine(folder, "reyaudioacx.cat"), sys = Path.Combine(folder, "ReyAudioAcx.sys");
                    using (var cert = new X509Certificate2(Path.Combine(folder, "local-test.cer"))) {
                        Signing.RequireSignature(sys, cert.Thumbprint, true); Signing.RequireSignature(cat, cert.Thumbprint, true);
                        Signing.RequireStatus(Signing.VerifyMember(cat, sys), true);
                        var bytes = File.ReadAllBytes(sys); int pe = BitConverter.ToInt32(bytes, 0x3c);
                        int section = pe + 24 + BitConverter.ToUInt16(bytes, pe + 20), raw = BitConverter.ToInt32(bytes, section + 20);
                        bytes[raw] ^= 1; string corrupt = Path.Combine(folder, "tampered.sys"); File.WriteAllBytes(corrupt, bytes);
                        int signatureStatus = Signing.VerifyFile(corrupt), memberStatus = Signing.VerifyMember(cat, corrupt);
                        File.Delete(corrupt);
                        if (signatureStatus != unchecked((int)0x80096010) || memberStatus == 0 || memberStatus == unchecked((int)0x800b0109)) throw new InvalidDataException("Corrupt driver was not rejected.");
                        Json(args[2], new { passed = true, embedded_signature_tamper_rejected = true, catalog_member_tamper_rejected = true, signature_status = unchecked((uint)signatureStatus).ToString("X8"), catalog_status = unchecked((uint)memberStatus).ToString("X8"), machine_trust_changed = false });
                    } return 0;
                }
                if (args.Length == 2 && args[0] == "--install-driver") { DriverInstall.Install(args[1]); return 0; }
                if (args.Length == 1 && args[0] == "--remove-driver") { DriverInstall.Remove(false); return 0; }
                if (args.Length == 1 && args[0] == "--rollback-driver") { DriverInstall.Remove(true); return 0; }
                if (args.Length == 1 && args[0] == "--commit-driver") { DriverInstall.Commit(); return 0; }
                if (args.Length == 1 && args[0] == "--enable-test-mode") {
                    var ready = Readiness.Read();
                    if (!ready.Administrator || ready.SecureBoot == true) throw new InvalidOperationException(ready.Reason);
                    var info = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "bcdedit.exe"), "/set {current} testsigning on") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
                    using (var process = Process.Start(info)) { string text = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd(); process.WaitForExit(); if (process.ExitCode != 0) throw new InvalidOperationException(text); }
                    MessageBox.Show("Test Mode включён для следующей загрузки. Перезагрузите компьютер и снова запустите этот установщик.", "Rey Audio Driver"); return 0;
                }
                if (args.Length == 0) { Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false); Application.Run(new SetupForm()); return 0; }
                return 1;
            } catch (Exception error) {
                if (args.Length >= 2 && (args[0] == "--sign-test" || args[0] == "--verify-test")) { Json(args[args.Length - 1], new { passed = false, error = error.ToString() }); return 1; }
                if (args.Length == 2 && args[0] == "--check") { Json(args[1], new { Ready = false, error = error.ToString() }); return 1; }
                MessageBox.Show(error.Message, "Rey Audio Driver — установка", MessageBoxButtons.OK, MessageBoxIcon.Error); return 1603;
            }
        }
    }
}
