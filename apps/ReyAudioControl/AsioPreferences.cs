using System;
using Microsoft.Win32;
namespace ReyAudio {
    // These preferences are read by the ASIO DLL when the host opens it.
    // One connected USB device is used automatically; there is no card binding.
    internal sealed class AsioPreferences {
        public int Block = 64, Lead = 3;
        public static AsioPreferences Load() {
            var result = new AsioPreferences();
            using (var key = Registry.CurrentUser.OpenSubKey(@"Software\ReyAudio\ASIO")) {
                if (key == null) return result;
                int block, lead;
                int.TryParse(Convert.ToString(key.GetValue("BufferSize", 64)), out block);
                int.TryParse(Convert.ToString(key.GetValue("RenderLeadBlocks", 3)), out lead);
                if (ValidBlock(block)) result.Block = block;
                if (lead >= 1 && lead <= 4) result.Lead = lead;
            }
            return result;
        }
        public static bool ValidBlock(int block) { return block >= 16 && block <= 256 && (block & (block - 1)) == 0; }
        public static void Save(int block, int lead) {
            if (!ValidBlock(block) || lead < 1 || lead > 4)
                throw new ArgumentException("Неверные настройки USB-ASIO.");
            using (var key = Registry.CurrentUser.CreateSubKey(@"Software\ReyAudio\ASIO")) {
                key.SetValue("BufferSize", block, RegistryValueKind.DWord);
                key.SetValue("RenderLeadBlocks", lead, RegistryValueKind.DWord);
                key.DeleteValue("DeviceId", false);
            }
        }
    }
}
