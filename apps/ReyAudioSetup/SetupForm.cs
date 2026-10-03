using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows.Forms;
namespace ReyAudio.Setup {
    internal sealed class SetupForm : Form {
        readonly Label status = new Label(); readonly Button install = new Button(), prepare = new Button();
        public SetupForm() {
            Text = "Rey Audio Driver · установка"; ClientSize = new Size(720, 470); MinimumSize = MaximumSize = Size;
            StartPosition = FormStartPosition.CenterScreen; BackColor = Color.FromArgb(245, 247, 251); Font = new Font("Segoe UI", 10);
            Controls.Add(new Label { Text = "Rey Audio Driver", Location = new Point(32, 28), AutoSize = true, Font = new Font("Segoe UI", 25, FontStyle.Bold), ForeColor = Color.FromArgb(23, 34, 53) });
            Controls.Add(new Label { Text = "USB + AoIP  ·  8 входов / 8 выходов  ·  Windows 11 x64", Location = new Point(35, 85), AutoSize = true, ForeColor = Color.FromArgb(78, 96, 122) });
            Controls.Add(new Label { Text = "Установщик добавляет аудиодрайвер, фоновую службу и микшер в трей.\nНастройки USB и LAN доступны в панели. Редактирование файлов не требуется.\n\nПлатные сертификаты не используются: драйвер получает локальную тестовую\nподпись на этом ПК. Для его загрузки Windows должен быть включён Test Mode.", Location = new Point(35, 127), Size = new Size(650, 126) });
            status.Location = new Point(35, 270); status.Size = new Size(650, 66); status.Font = new Font("Segoe UI", 11, FontStyle.Bold); Controls.Add(status);
            prepare.Text = "Включить Test Mode…"; prepare.Location = new Point(35, 355); prepare.Size = new Size(222, 44); Controls.Add(prepare);
            install.Text = "Установить"; install.Location = new Point(475, 355); install.Size = new Size(210, 44); install.BackColor = Color.FromArgb(36, 99, 235); install.ForeColor = Color.White; install.FlatStyle = FlatStyle.Flat; Controls.Add(install);
            Controls.Add(new Label { Text = "Предварительная версия. Физическая аудиозадержка и kernel streaming ещё проверяются.", Location = new Point(35, 425), Size = new Size(650, 25), ForeColor = Color.FromArgb(105, 119, 139), Font = new Font("Segoe UI", 9) });
            prepare.Click += (s, e) => {
                if (MessageBox.Show("Включить Test Mode для загрузки локально подписанного драйвера?\n\nЭто изменит политику следующей загрузки Windows. Потребуется перезагрузка. Secure Boot в BIOS и изоляция ядра этим действием не изменяются. При использовании BitLocker заранее убедитесь, что у вас есть ключ восстановления.", Text, MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK) return;
                try { Process.Start(new ProcessStartInfo(Assembly.GetExecutingAssembly().Location, "--enable-test-mode") { UseShellExecute = true, Verb = "runas" }); }
                catch (Exception error) { status.Text = error.Message; }
            };
            install.Click += async (s, e) => await Install(); RefreshState();
        }
        void RefreshState() { var readiness = Readiness.Read(); status.Text = readiness.Reason; install.Enabled = readiness.Ready; prepare.Visible = !readiness.TestMode; prepare.Enabled = readiness.SecureBoot != true; }
        async Task Install() {
            var resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("ReyAudio.Installer.msi");
            if (resource == null) { MessageBox.Show("Запустите готовый Rey Audio Setup.exe или MSI из раздела Releases.", Text); return; }
            install.Enabled = prepare.Enabled = false;
            string folder = Path.Combine(Path.GetTempPath(), "ReyAudioSetup-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
            string msi = Path.Combine(folder, "Rey-Audio-Driver.msi"); using (resource) using (var output = File.Create(msi)) resource.CopyTo(output);
            // Elevation belongs to Windows Installer. No hidden security changes.
            try {
                var info = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "msiexec.exe"), "/i \"" + msi + "\"") { UseShellExecute = true };
                using (var process = Process.Start(info)) { await Task.Run(() => process.WaitForExit()); status.Text = process.ExitCode == 0 ? "Установка завершена. Микшер доступен в меню Пуск и в трее." : "Установка не завершена. Код Windows Installer: " + process.ExitCode; }
            } catch (Exception error) { status.Text = error.Message; }
            finally { install.Enabled = Readiness.Read().Ready; prepare.Enabled = Readiness.Read().SecureBoot != true; }
        }
    }
}
