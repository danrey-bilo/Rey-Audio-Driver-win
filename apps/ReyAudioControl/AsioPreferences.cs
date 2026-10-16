using System;
using Microsoft.Win32;
namespace ReyAudio {
    // These preferences are read by the ASIO DLL when the host opens it.
    // One connected USB device is used automatically; there is no card binding.
    internal sealed class AsioPreferences {
        public int Block = 64;
        public static AsioPreferences Load() {
            var result = new AsioPreferences();
            using (var key = Registry.CurrentUser.OpenSubKey(@"Software\ReyAudio\ASIO")) {
                if (key == null) return result;
                int block;
                int.TryParse(Convert.ToString(key.GetValue("BufferSize", 64)), out block);
                if (ValidBlock(block)) result.Block = block;
            }
            return result;
        }
        public static bool ValidBlock(int block) { return block >= 16 && block <= 256 && (block & (block - 1)) == 0; }
        public static void Save(int block) {
            if (!ValidBlock(block))
                throw new ArgumentException("Неверные настройки USB-ASIO.");
            using (var key = Registry.CurrentUser.CreateSubKey(@"Software\ReyAudio\ASIO")) {
                key.SetValue("BufferSize", block, RegistryValueKind.DWord);
                key.DeleteValue("RenderLeadBlocks", false);
                key.DeleteValue("DeviceId", false);
            }
        }
    }
}
