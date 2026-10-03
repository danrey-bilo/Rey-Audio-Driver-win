using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace ReyAudio.Setup {
    // Windows' built-in Authenticode signer. No SDK, external signer or
    // distributed private key is required on the installation computer.
    internal static class Signing {
        [StructLayout(LayoutKind.Sequential)] struct FileInfo { public uint Size; public IntPtr Name, Handle; }
        [StructLayout(LayoutKind.Sequential)] struct Subject { public uint Size; public IntPtr Index; public uint Choice; public IntPtr Info; }
        [StructLayout(LayoutKind.Sequential)] struct StoreInfo { public uint Size; public IntPtr Certificate; public uint Policy; public IntPtr Store; }
        [StructLayout(LayoutKind.Sequential)] struct SignerCert { public uint Size, Choice; public IntPtr Info, Window; }
        [StructLayout(LayoutKind.Sequential)] struct SignatureInfo { public uint Size, Algorithm, Attributes; public IntPtr Info, Authenticated, Unauthenticated; }
        [DllImport("mssign32.dll")] static extern int SignerSignEx(uint flags, ref Subject subject, ref SignerCert cert, ref SignatureInfo signature, IntPtr provider, IntPtr timestamp, IntPtr requests, IntPtr sip, out IntPtr context);
        [DllImport("mssign32.dll")] static extern int SignerFreeSignerContext(IntPtr context);
        [StructLayout(LayoutKind.Sequential)] struct TrustData {
            public uint Size; public IntPtr Policy, Sip; public uint Ui, Revocation, Choice; public IntPtr Info;
            public uint StateAction; public IntPtr State, Url; public uint ProviderFlags, UiContext; public IntPtr SignatureSettings;
        }
        [StructLayout(LayoutKind.Sequential)] struct CatalogInfo {
            public uint Size, Version; public IntPtr Catalog, Tag, Member, Handle, Hash; public uint HashSize; public IntPtr Context, Admin;
        }
        [DllImport("wintrust.dll", ExactSpelling = true)] static extern int WinVerifyTrust(IntPtr window, ref Guid action, ref TrustData data);
        [DllImport("wintrust.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CryptCATAdminAcquireContext2(out IntPtr context, IntPtr subsystem, string algorithm, IntPtr policy, uint flags);
        [DllImport("wintrust.dll", SetLastError = true)] static extern bool CryptCATAdminCalcHashFromFileHandle2(IntPtr admin, IntPtr file, ref uint size, byte[] hash, uint flags);
        [DllImport("wintrust.dll")] static extern bool CryptCATAdminReleaseContext(IntPtr admin, uint flags);
        static IntPtr Structure(object value) { var p = Marshal.AllocHGlobal(Marshal.SizeOf(value)); Marshal.StructureToPtr(value, p, false); return p; }
        static uint Size<T>() { return (uint)Marshal.SizeOf(typeof(T)); }
        public static string Hash(string path) { using (var h = SHA256.Create()) using (var s = File.OpenRead(path)) return BitConverter.ToString(h.ComputeHash(s)).Replace("-", "").ToLowerInvariant(); }
        public static void Sign(string path, X509Certificate2 certificate) {
            IntPtr name = Marshal.StringToHGlobalUni(path), index = Marshal.AllocHGlobal(4), info = IntPtr.Zero, store = IntPtr.Zero, context = IntPtr.Zero;
            try {
                Marshal.WriteInt32(index, 0);
                info = Structure(new FileInfo { Size = Size<FileInfo>(), Name = name });
                store = Structure(new StoreInfo { Size = Size<StoreInfo>(), Certificate = certificate.Handle, Policy = 2 });
                var subject = new Subject { Size = Size<Subject>(), Index = index, Choice = 1, Info = info };
                var cert = new SignerCert { Size = Size<SignerCert>(), Choice = 2, Info = store };
                var signature = new SignatureInfo { Size = Size<SignatureInfo>(), Algorithm = 0x800c };
                int result = SignerSignEx(0, ref subject, ref cert, ref signature, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, out context);
                if (result != 0) Marshal.ThrowExceptionForHR(result);
            } finally {
                if (context != IntPtr.Zero) SignerFreeSignerContext(context);
                if (info != IntPtr.Zero) Marshal.FreeHGlobal(info);
                if (store != IntPtr.Zero) Marshal.FreeHGlobal(store);
                Marshal.FreeHGlobal(index); Marshal.FreeHGlobal(name);
                GC.KeepAlive(certificate);
            }
        }
        static int Verify(IntPtr info, uint choice) {
            var action = new Guid("00AAC56B-CD44-11D0-8CC2-00C04FC295EE");
            var data = new TrustData { Size = Size<TrustData>(), Ui = 2, Choice = choice, Info = info, StateAction = 1, ProviderFlags = 0x1000 };
            int status;
            try { status = WinVerifyTrust(new IntPtr(-1), ref action, ref data); }
            finally { data.StateAction = 2; WinVerifyTrust(new IntPtr(-1), ref action, ref data); }
            return status;
        }
        public static int VerifyFile(string path) {
            IntPtr name = Marshal.StringToHGlobalUni(path), info = IntPtr.Zero;
            try { info = Structure(new FileInfo { Size = Size<FileInfo>(), Name = name }); return Verify(info, 1); }
            finally { if (info != IntPtr.Zero) Marshal.FreeHGlobal(info); Marshal.FreeHGlobal(name); }
        }
        public static int VerifyMember(string catalog, string member) {
            IntPtr admin;
            if (!CryptCATAdminAcquireContext2(out admin, IntPtr.Zero, "SHA256", IntPtr.Zero, 0)) throw new Win32Exception();
            IntPtr cat = IntPtr.Zero, tag = IntPtr.Zero, name = IntPtr.Zero, hash = IntPtr.Zero, info = IntPtr.Zero;
            try {
                using (var file = File.OpenRead(member)) {
                    uint length = 0;
                    if (!CryptCATAdminCalcHashFromFileHandle2(admin, file.SafeFileHandle.DangerousGetHandle(), ref length, null, 0) || length != 32) throw new Win32Exception();
                    var digest = new byte[length];
                    if (!CryptCATAdminCalcHashFromFileHandle2(admin, file.SafeFileHandle.DangerousGetHandle(), ref length, digest, 0)) throw new Win32Exception();
                    cat = Marshal.StringToHGlobalUni(catalog); name = Marshal.StringToHGlobalUni(member);
                    tag = Marshal.StringToHGlobalUni(BitConverter.ToString(digest).Replace("-", ""));
                    hash = Marshal.AllocHGlobal((int)length); Marshal.Copy(digest, 0, hash, (int)length);
                    info = Structure(new CatalogInfo { Size = Size<CatalogInfo>(), Catalog = cat, Tag = tag, Member = name,
                        Handle = file.SafeFileHandle.DangerousGetHandle(), Hash = hash, HashSize = length, Admin = admin });
                    return Verify(info, 2);
                }
            } finally {
                foreach (var p in new[] { info, hash, cat, tag, name }) if (p != IntPtr.Zero) Marshal.FreeHGlobal(p);
                CryptCATAdminReleaseContext(admin, 0);
            }
        }
        public static void RequireSignature(string path, string thumbprint, bool allowUntrusted) {
            var actual = new X509Certificate2(X509Certificate.CreateFromSignedFile(path));
            using (actual) if (!string.Equals(actual.Thumbprint, thumbprint, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Unexpected local signer.");
            RequireStatus(VerifyFile(path), allowUntrusted);
        }
        public static void RequireStatus(int status, bool allowUntrusted) {
            if (status != 0 && !(allowUntrusted && status == unchecked((int)0x800b0109)))
                throw new InvalidDataException("Signature/catalog verification failed: 0x" + unchecked((uint)status).ToString("X8"));
        }
        public static X509Certificate2 CreateCertificate(RSACryptoServiceProvider key) {
            var request = new CertificateRequest("CN=Rey Audio local test " + Guid.NewGuid().ToString("N"), key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
            request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature, true));
            request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(new OidCollection { new Oid("1.3.6.1.5.5.7.3.3") }, true));
            request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, true));
            return request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-5), DateTimeOffset.UtcNow.AddYears(2));
        }
    }
}
