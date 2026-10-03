using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
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
[assembly: AssemblyVersion("2.8.1.2")]
[assembly: AssemblyFileVersion("2.8.1.2")]
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
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private long nextRefresh;
        private Exception connectionError;
        private readonly HashSet<MixerChannel> pendingMix = new HashSet<MixerChannel>();
        private bool mixing;
        public Panel(Application application, EventWaitHandle activate) {
            app = application; showEvent = activate;
            using (var source = Assembly.GetExecutingAssembly().GetManifestResourceStream("ReyAudio.MainWindow.xaml")) window = (Window)XamlReader.Load(source);
            window.DataContext = view;
            ((RadioButton)window.FindName("NavMixer")).Checked += (s, e) => Navigate("mixer");
            ((RadioButton)window.FindName("MixerInputs")).Checked += (s, e) => { view.MixerOutputs = false; view.Notify(); };
            ((RadioButton)window.FindName("MixerOutputs")).Checked += (s, e) => { view.MixerOutputs = true; view.Notify(); };
            foreach(var channel in view.AllChannels) channel.Changed = c => pendingMix.Add(c);
            Click("ResetMixer", async () => { pendingMix.Clear(); foreach(var c in view.AllChannels) c.Dirty = false; await Command("MIX_RESET", "Микшер сброшен."); });
            ((RadioButton)window.FindName("NavUsb")).Checked += (s, e) => Navigate("usb");
            ((RadioButton)window.FindName("NavDiagnostics")).Checked += (s, e) => Navigate("diagnostics");
            Click("SaveUsb", SaveUsb);
            Click("SaveAsio", () => {
                AsioPreferences.Save(view.AsioBlock, view.AsioLead);
                view.Message = "Сохранено.";
                view.Notify(); return Task.FromResult(0);
            });
            Click("CopyReport", () => {
                if (!string.IsNullOrEmpty(view.LastJson)) Clipboard.SetText(view.LastJson);
                view.Message = string.IsNullOrEmpty(view.LastJson) ? "Диагностика станет доступна после запуска службы." : "Диагностика скопирована.";
                view.Notify(); return Task.FromResult(0);
            });
            ((ComboBox)window.FindName("UsbDepth")).SelectionChanged += (s, e) => view.Notify();
            ((ComboBox)window.FindName("AsioBlock")).SelectionChanged += (s, e) => view.Notify();
            ((ComboBox)window.FindName("AsioLead")).SelectionChanged += (s, e) => view.Notify();
            window.Closing += (s, e) => { if (!exiting) { e.Cancel = true; window.Hide(); } };
            window.StateChanged += (s, e) => { if (window.WindowState == WindowState.Minimized && tray != null && tray.Visible) window.Hide(); };
            timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
            window.IsVisibleChanged += (s, e) => { timer.Interval = TimeSpan.FromMilliseconds(window.IsVisible ? 50 : 250); };
            timer.Tick += async (s, e) => {
                if (showEvent != null && showEvent.WaitOne(0)) Show();
                if (mixing) return;
                if (pendingMix.Count != 0 && view.Available && !view.Busy && !polling) {
                    mixing = true; view.MixerSending = true; view.Notify(); var channel = pendingMix.First(); pendingMix.Remove(channel); long revision = channel.Revision;
                    try {
                        var response = await service.Send(channel.Command);
                        if (!ViewModel.Bool(response, "ok")) throw new ArgumentException(ViewModel.String(response, "error"));
                        if(channel.Revision == revision) channel.Dirty = false;
                        Update(response, false);
                    } catch(Exception error) { pendingMix.Add(channel); view.Message = FriendlyError(error); view.Notify(); }
                    finally { mixing = false; view.MixerSending = false; view.Notify(); }
                } else if ((view.Page == "mixer" && window.IsVisible) || clock.ElapsedMilliseconds >= nextRefresh) {
                    await Refresh(); nextRefresh = clock.ElapsedMilliseconds + 1000;
                }
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
                return "Служба недоступна.";
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
            await Command(string.Format(CultureInfo.InvariantCulture, "USB {0} {1} {2} {3} {4} 1", view.UsbRate, view.UsbBits, view.UsbDepth, view.UsbBlock, guard), "Сохранено.");
        }
        private async Task Command(string command, string message) {
            var response = await service.Send(command);
            if (!ViewModel.Bool(response, "ok")) throw new ArgumentException("Настройки отклонены службой: " + ViewModel.String(response, "error"));
            Update(response, false); view.Message = message; view.Notify();
        }
        private void Update(Dictionary<string, object> response, bool fields) {
            view.LastJson = new JavaScriptSerializer().Serialize(response);
            view.Update(response, fields);
            UpdateTray();
        }
        private void UpdateTray() {
            if (tray == null) return;
            bool detected = view.Available && view.UsbPresent;
            string text = "Rey Audio · " + view.StatusTitle;
            if (tray.Text != text) tray.Text = text;
            if (tray.Visible != detected) tray.Visible = detected;
        }
        public async Task Refresh() {
            if (polling || view.Busy || mixing) return;
            polling = true;
            try { Update(await service.Send("STATUS"), !initialized); initialized = true; connectionError = null; }
            catch (Exception error) { connectionError = error; view.Available = false; view.UsbPresent = false; UpdateTray(); view.Notify(); }
            finally { polling = false; }
        }
        public void Navigate(string page) { view.Page = page; view.Notify(); }
        public void Show() { window.Show(); window.WindowState = WindowState.Normal; window.Activate(); }
        private void CreateTray() {
            var menu = new Forms.ContextMenuStrip();
            menu.Items.Add("Открыть Rey Audio Driver", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(Show)));
            menu.Items.Add("Микшер", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(() => { ((RadioButton)window.FindName("NavMixer")).IsChecked = true; Show(); })));
            menu.Items.Add("Настройки", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(() => { ((RadioButton)window.FindName("NavUsb")).IsChecked = true; Show(); })));
            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("Выход из панели", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(() => { exiting = true; app.Shutdown(); })));
            using (var icon = Assembly.GetExecutingAssembly().GetManifestResourceStream("ReyAudio.ico")) {
                tray = new Forms.NotifyIcon { Icon = new System.Drawing.Icon(icon), Text = "Rey Audio Driver", Visible = false, ContextMenuStrip = menu };
            }
            tray.DoubleClick += (s, e) => app.Dispatcher.BeginInvoke(new Action(Show));
        }
        public void Start(bool hidden) {
            CreateTray();
            timer.Interval = TimeSpan.FromMilliseconds(hidden ? 250 : 50);
            timer.Start();
            if (!hidden) Show();
            app.Dispatcher.BeginInvoke(new Action(async () => await Refresh()));
        }
        public async Task Render(string directory, string fixture = null) {
            Directory.CreateDirectory(directory);
            if (fixture == null) await Refresh();
            else Update(new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(fixture)), true);
            var root = (FrameworkElement)window.Content;
            double mixerBottom = 0, asioBottom = 0;
            foreach (var page in new[] { "mixer", "usb", "diagnostics" }) {
                Navigate(page);
                ((RadioButton)window.FindName(page == "mixer" ? "NavMixer" : page == "usb" ? "NavUsb" : "NavDiagnostics")).IsChecked = true;
                root.Measure(new Size(1120, 800)); root.Arrange(new Rect(0, 0, 1120, 800)); root.UpdateLayout();
                await app.Dispatcher.InvokeAsync(() => { }, DispatcherPriority.Render);
                if (page == "mixer") {
                    var strips = (FrameworkElement)window.FindName("MixerStrips");
                    mixerBottom = strips.TransformToAncestor(root).TransformBounds(new Rect(0, 0, strips.ActualWidth, strips.ActualHeight)).Bottom;
                }
                if (page == "usb") {
                    var save = (FrameworkElement)window.FindName("SaveAsio");
                    asioBottom = save.TransformToAncestor(root).TransformBounds(new Rect(0, 0, save.ActualWidth, save.ActualHeight)).Bottom;
                }
                var bitmap = new RenderTargetBitmap(1120, 800, 96, 96, PixelFormats.Pbgra32); bitmap.Render(root);
                var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
                using (var output = File.Create(Path.Combine(directory, "rey-" + page + ".png"))) png.Save(output);
            }
            File.WriteAllText(Path.Combine(directory, "service-snapshot.json"), view.LastJson.Length == 0 ? "{\"service_available\":false}" : view.LastJson);
            if (connectionError != null) File.WriteAllText(Path.Combine(directory, "connection-error.txt"), connectionError.ToString());
            File.WriteAllText(Path.Combine(directory, "layout-check.json"), new JavaScriptSerializer().Serialize(new {
                usb_apply_enabled = ((Button)window.FindName("SaveUsb")).IsEnabled,
                service_available = view.Available, viewmodel_can_apply = view.CanApplyUsb,
                device_label = view.DeviceLabel, device_limit = 1, page_count = 3,
                lan_navigation_absent = window.FindName("NavLan") == null,
                card_selector_absent = window.FindName("AudioCardSelector") == null,
                tray_button_absent = window.FindName("ToTray") == null,
                windows_sound_button_absent = window.FindName("WindowsSound") == null,
                all_channel_controls_visible = mixerBottom <= root.ActualHeight - 22,
                asio_controls_visible = asioBottom <= root.ActualHeight - 22,
                asio_apply_hint = view.AsioApplyHint, asio_latency_label = view.AsioLatencyLabel,
                width = root.ActualWidth, height = root.ActualHeight, fixture = fixture != null
            }));
            VerifyTrayLifecycle(directory);
        }
        private void VerifyTrayLifecycle(string directory) {
            // Finite UI integration check: real NotifyIcon and WPF lifecycle,
            // without changing the service, transport or audio preferences.
            CreateTray();
            bool available = view.Available, present = view.UsbPresent;
            var cases = new List<object>();
            foreach (var state in new[] { new[] { false, false }, new[] { true, false }, new[] { true, true }, new[] { false, true }, new[] { true, true }, new[] { true, false } }) {
                view.Available = state[0]; view.UsbPresent = state[1]; UpdateTray();
                bool expected = state[0] && state[1];
                if (tray.Visible != expected || window.IsVisible) throw new InvalidOperationException("Tray detection lifecycle failed");
                cases.Add(new { service = state[0], usb = state[1], icon = tray.Visible, window = window.IsVisible });
            }
            view.Available = true; view.UsbPresent = true; UpdateTray();
            window.WindowStartupLocation = WindowStartupLocation.Manual;
            window.Left = -32000; window.Top = -32000; window.ShowInTaskbar = false;
            window.Width = window.MinWidth; window.Height = window.MinHeight;
            Show(); window.UpdateLayout();
            var root = (FrameworkElement)window.Content;
            var layouts = new List<object>();
            foreach (string page in new[] { "mixer", "usb", "diagnostics" }) {
                Navigate(page);
                ((RadioButton)window.FindName(page == "mixer" ? "NavMixer" : page == "usb" ? "NavUsb" : "NavDiagnostics")).IsChecked = true;
                window.UpdateLayout();
                string name = page == "mixer" ? "ResetMixer" : page == "usb" ? "SaveUsb" : "CopyReport";
                var control = (FrameworkElement)window.FindName(name);
                double bottom = control.TransformToAncestor(root).TransformBounds(new Rect(0, 0, control.ActualWidth, control.ActualHeight)).Bottom;
                bool visible = bottom <= root.ActualHeight - 8;
                if (page != "diagnostics" && !visible) throw new InvalidOperationException("Core controls clipped at minimum window size: " + page);
                layouts.Add(new { page = page, width = root.ActualWidth, height = root.ActualHeight, core_controls_visible = visible });
                var bitmap = new RenderTargetBitmap((int)Math.Ceiling(root.ActualWidth), (int)Math.Ceiling(root.ActualHeight), 96, 96, PixelFormats.Pbgra32);
                bitmap.Render(root); var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
                using (var output = File.Create(Path.Combine(directory, "rey-" + page + "-minimum.png"))) png.Save(output);
            }
            window.Close();
            bool closeKeepsTray = !window.IsVisible && tray.Visible && !exiting;
            Show(); window.WindowState = WindowState.Minimized;
            bool minimizeKeepsTray = !window.IsVisible && tray.Visible && !exiting;
            if (!closeKeepsTray || !minimizeKeepsTray) throw new InvalidOperationException("Window/tray lifecycle failed");
            view.Available = available; view.UsbPresent = present; UpdateTray();
            File.WriteAllText(Path.Combine(directory, "tray-lifecycle.json"), new JavaScriptSerializer().Serialize(new {
                cases = cases, close_keeps_tray = closeKeepsTray, minimize_keeps_tray = minimizeKeepsTray,
                minimum_window = layouts,
                hidden_window_wake_ms = 250, hidden_status_poll_ms = 1000
            }));
        }
        public void Dispose() { timer.Stop(); if (tray != null) { tray.Visible = false; tray.Dispose(); } }
    }
    internal static class Program {
        [STAThread]
        public static int Main(string[] args) {
            bool render = (args.Length == 2 || (args.Length == 4 && args[2] == "--fixture")) && args[0] == "--render-test";
            bool background = args.Contains("--tray");
            bool created;
            using (var singleton = new Mutex(true, "Local\\ReyAudio.Control", out created))
            using (var show = new EventWaitHandle(false, EventResetMode.AutoReset, "Local\\ReyAudio.Show")) {
                if (!created && !render) { if (!background) show.Set(); return 0; }
                try {
                    var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
                    using (var panel = new Panel(app, show)) {
                        if (render) app.Dispatcher.BeginInvoke(new Action(async () => { try { await panel.Render(Path.GetFullPath(args[1]), args.Length == 4 ? Path.GetFullPath(args[3]) : null); app.Shutdown(0); } catch (Exception e) { File.WriteAllText(Path.Combine(Path.GetFullPath(args[1]), "render-error.txt"), e.ToString()); app.Shutdown(1); } }));
                        else panel.Start(background);
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
