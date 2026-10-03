using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Forms = System.Windows.Forms;
[assembly: AssemblyTitle("Rey Audio Driver")]
[assembly: AssemblyProduct("Rey Audio Driver")]
[assembly: AssemblyCompany("Rey Audio")]
[assembly: AssemblyVersion("2.6.0.0")]
[assembly: AssemblyFileVersion("2.6.0.0")]
namespace ReyAudio {
    internal sealed class Panel : IDisposable {
        private readonly Application app;
        private readonly Window window;
        private readonly ViewModel view = new ViewModel();
        private readonly ServiceClient service = new ServiceClient();
        private readonly DispatcherTimer timer;
        private readonly EventWaitHandle showEvent;
        private Forms.NotifyIcon tray;
        private bool initialized, polling, exiting;
        private string lastState = "";
        private Exception connectionError;
        public Panel(Application application, EventWaitHandle activate) {
            app = application; showEvent = activate;
            using (var source = Assembly.GetExecutingAssembly().GetManifestResourceStream("ReyAudio.MainWindow.xaml")) window = (Window)XamlReader.Load(source);
            window.DataContext = view;
            ((RadioButton)window.FindName("NavUsb")).Checked += (s, e) => Navigate("usb");
            ((RadioButton)window.FindName("NavLan")).Checked += (s, e) => Navigate("lan");
            ((RadioButton)window.FindName("NavDiagnostics")).Checked += (s, e) => Navigate("diagnostics");
            Click("ToTray", () => { window.Hide(); return Task.FromResult(0); });
            Click("UseUsb", async () => { await SaveUsb(); await Command("USE usb", "Выбран USB. Подключение перезапускается."); });
            Click("SaveUsb", SaveUsb);
            Click("SaveLan", SaveLan);
            Click("ConnectLan", async () => {
                if (view.LanEnabled) await Command("LAN_DISCONNECT", "LAN отключён. Автоматическое подключение USB доступно.");
                else { await SaveLan(); await Command("LAN_CONNECT", "LAN подключается. По умолчанию используется Ethernet."); }
            });
            Click("ScanLan", Scan);
            Click("AutoRoute", () => Command("USE auto", "Выбор подключения: LAN по умолчанию."));
            Click("CopyReport", () => {
                if (!string.IsNullOrEmpty(view.LastJson)) Clipboard.SetText(view.LastJson);
                view.Message = string.IsNullOrEmpty(view.LastJson) ? "Диагностика станет доступна после запуска службы." : "Диагностика скопирована.";
                view.Notify(); return Task.FromResult(0);
            });
            ((ComboBox)window.FindName("UsbDepth")).SelectionChanged += (s, e) => view.Notify();
            window.Closing += (s, e) => { if (!exiting) { e.Cancel = true; window.Hide(); } };
            timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            timer.Tick += async (s, e) => {
                if (showEvent != null && showEvent.WaitOne(0)) Show();
                await Refresh();
            };
        }
        private void Click(string name, Func<Task> action) {
            ((Button)window.FindName(name)).Click += async (s, e) => {
                if (view.Busy) return;
                view.Busy = true; view.Message = ""; view.Notify();
                try { await action(); }
                catch (Exception error) { view.Message = FriendlyError(error); }
                finally { view.Busy = false; view.Notify(); }
            };
        }
        private static string FriendlyError(Exception error) {
            if (error is TimeoutException || error is IOException || error is ObjectDisposedException || error is OperationCanceledException)
                return "Служба не ответила. Проверьте её запуск и повторите действие.";
            return error.Message;
        }
        private static int Number(string text, int minimum, int maximum, string label) {
            int value;
            if (!int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out value) || value < minimum || value > maximum)
                throw new ArgumentException(label + ": укажите целое число от " + minimum + " до " + maximum + ".");
            return value;
        }
        private async Task SaveUsb() {
            int guard = Number(view.UsbGuard, 0, 8192, "Запас USB");
            await Command(string.Format(CultureInfo.InvariantCulture, "USB {0} {1} {2} {3} {4} {5}", view.UsbRate, view.UsbBits, view.UsbDepth, view.UsbBlock, guard, view.UsbAutomatic ? 1 : 0), "Настройки USB сохранены.");
        }
        private async Task SaveLan() {
            IPAddress address;
            if (!IPAddress.TryParse(view.LanPeer, out address) || address.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork)
                throw new ArgumentException("Укажите IPv4-адрес Pi, например 192.168.1.2.");
            int port = Number(view.LanPort, 1, 65535, "UDP-порт"), guard = Number(view.LanGuard, 0, 8192, "Запас LAN"), frames = Number(view.LanFrames, 0, 256, "Размер пакета");
            await Command(string.Format(CultureInfo.InvariantCulture, "LAN {0} {1} {2} {3} {4} {5}", address, port, view.LanBlock, guard, frames, view.LanEnergy ? 1 : 0), "Настройки LAN сохранены.");
        }
        private async Task Command(string command, string message) {
            var response = await service.Send(command);
            if (!ViewModel.Bool(response, "ok")) throw new ArgumentException("Настройки отклонены службой: " + ViewModel.String(response, "error"));
            Update(response, false); view.Message = message; view.Notify();
        }
        private async Task Scan() {
            var response = await service.Send("SCAN");
            var devices = ((IEnumerable)response["devices"]).Cast<Dictionary<string, object>>().ToList();
            if (devices.Count == 0) view.Message = "Устройства не найдены на Ethernet. Можно ввести IP-адрес вручную.";
            else if (devices.Count == 1) {
                view.LanPeer = ViewModel.String(devices[0], "ip");
                view.Message = "Найден Rey Audio по адресу " + view.LanPeer + ". Нажмите «Подключить LAN».";
            } else view.Message = "Найдено несколько устройств: " + string.Join(", ", devices.Select(d => ViewModel.String(d, "ip"))) + ". Укажите адрес нужного Pi.";
            view.Notify();
        }
        private void Update(Dictionary<string, object> response, bool fields) {
            view.LastJson = new JavaScriptSerializer().Serialize(response);
            view.Update(response, fields);
            if (tray != null) tray.Text = "Rey Audio Driver · " + view.RouteLabel;
            if (lastState != view.State) lastState = view.State;
        }
        public async Task Refresh() {
            if (polling || view.Busy) return;
            polling = true;
            try { Update(await service.Send("STATUS"), !initialized); initialized = true; connectionError = null; }
            catch (Exception error) { connectionError = error; view.Available = false; view.Notify(); }
            finally { polling = false; }
        }
        public void Navigate(string page) { view.Page = page; view.Notify(); }
        public void Show() { window.Show(); window.WindowState = WindowState.Normal; window.Activate(); }
        public void Start(bool hidden) {
            var menu = new Forms.ContextMenuStrip();
            menu.Items.Add("Открыть Rey Audio Driver", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(Show)));
            menu.Items.Add("Настроить USB", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(() => { ((RadioButton)window.FindName("NavUsb")).IsChecked = true; Show(); })));
            menu.Items.Add("Настроить AoIP / LAN", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(() => { ((RadioButton)window.FindName("NavLan")).IsChecked = true; Show(); })));
            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("Выход из панели", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(() => { exiting = true; app.Shutdown(); })));
            using (var icon = Assembly.GetExecutingAssembly().GetManifestResourceStream("ReyAudio.ico")) {
                tray = new Forms.NotifyIcon { Icon = new System.Drawing.Icon(icon), Text = "Rey Audio Driver", Visible = true, ContextMenuStrip = menu };
            }
            tray.DoubleClick += (s, e) => app.Dispatcher.BeginInvoke(new Action(Show));
            timer.Start();
            if (!hidden) Show();
            app.Dispatcher.BeginInvoke(new Action(async () => await Refresh()));
        }
        public async Task Render(string directory) {
            Directory.CreateDirectory(directory);
            await Refresh();
            var root = (FrameworkElement)window.Content;
            foreach (var page in new[] { "usb", "lan", "diagnostics" }) {
                Navigate(page);
                ((RadioButton)window.FindName(page == "usb" ? "NavUsb" : page == "lan" ? "NavLan" : "NavDiagnostics")).IsChecked = true;
                root.Measure(new Size(1120, 800)); root.Arrange(new Rect(0, 0, 1120, 800)); root.UpdateLayout();
                await app.Dispatcher.InvokeAsync(() => { }, DispatcherPriority.Render);
                var bitmap = new RenderTargetBitmap(1120, 800, 96, 96, PixelFormats.Pbgra32); bitmap.Render(root);
                var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
                using (var output = File.Create(Path.Combine(directory, "rey-" + page + ".png"))) png.Save(output);
            }
            File.WriteAllText(Path.Combine(directory, "service-snapshot.json"), view.LastJson.Length == 0 ? "{\"service_available\":false}" : view.LastJson);
            if (connectionError != null) File.WriteAllText(Path.Combine(directory, "connection-error.txt"), connectionError.ToString());
            File.WriteAllText(Path.Combine(directory, "layout-check.json"), new JavaScriptSerializer().Serialize(new {
                usb_apply_enabled = ((Button)window.FindName("SaveUsb")).IsEnabled,
                lan_apply_enabled = ((Button)window.FindName("SaveLan")).IsEnabled,
                usb_select_enabled = ((Button)window.FindName("UseUsb")).IsEnabled,
                service_available = view.Available, viewmodel_can_apply = view.CanApply,
                width = root.ActualWidth, height = root.ActualHeight
            }));
        }
        public void Dispose() { timer.Stop(); if (tray != null) { tray.Visible = false; tray.Dispose(); } }
    }
    internal static class Program {
        [STAThread]
        public static int Main(string[] args) {
            bool render = args.Length == 2 && args[0] == "--render-test";
            bool created;
            using (var singleton = new Mutex(true, "Local\\ReyAudio.Control", out created))
            using (var show = new EventWaitHandle(false, EventResetMode.AutoReset, "Local\\ReyAudio.Show")) {
                if (!created && !render) { show.Set(); return 0; }
                try {
                    var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
                    using (var panel = new Panel(app, show)) {
                        if (render) app.Dispatcher.BeginInvoke(new Action(async () => { try { await panel.Render(Path.GetFullPath(args[1])); app.Shutdown(0); } catch (Exception e) { File.WriteAllText(Path.Combine(Path.GetFullPath(args[1]), "render-error.txt"), e.ToString()); app.Shutdown(1); } }));
                        else panel.Start(args.Contains("--tray"));
                        return app.Run();
                    }
                } catch (Exception e) {
                    if (render) { Directory.CreateDirectory(args[1]); File.WriteAllText(Path.Combine(args[1], "render-error.txt"), e.ToString()); }
                    else MessageBox.Show(e.Message, "Rey Audio Driver", MessageBoxButton.OK, MessageBoxImage.Error);
                    return 1;
                }
            }
        }
    }
}
