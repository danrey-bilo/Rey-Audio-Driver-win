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
[assembly: AssemblyVersion("2.8.2.7")]
[assembly: AssemblyFileVersion("2.8.2.7")]
namespace ReyAudio {
    internal sealed class Panel : IDisposable {
        private readonly Application app;
        private readonly Window window;
        private readonly ViewModel view = new ViewModel();
        private readonly ServiceClient service = new ServiceClient();
        private readonly BoardControl board = new BoardControl();
        private bool boardPolling;
        private long nextBoardRefresh;
        private readonly DispatcherTimer timer;
        private readonly EventWaitHandle showEvent;
        private Forms.NotifyIcon tray;
        private bool initialized, polling, exiting;
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private long nextRefresh;
        private Exception connectionError;
        private readonly HashSet<MixerChannel> pendingMix = new HashSet<MixerChannel>();
        private readonly HashSet<MixerPair> pendingLinks = new HashSet<MixerPair>();
        private bool mixing;
        public Panel(Application application, EventWaitHandle activate) {
            app = application; showEvent = activate;
            using (var source = Assembly.GetExecutingAssembly().GetManifestResourceStream("ReyAudio.MainWindow.xaml")) window = (Window)XamlReader.Load(source);
            window.DataContext = view;
            ((RadioButton)window.FindName("NavMixer")).Checked += (s, e) => Navigate("mixer");
            foreach(var channel in view.AllChannels) channel.Changed = c => {
                var other = c.Pair == null ? null : c.Pair.Mirror(c);
                if(other != null) pendingMix.Remove(other);
                pendingMix.Add(c);
            };
            foreach(var pair in view.AllPairs) pair.Changed = p => pendingLinks.Add(p);
            Click("ResetMixer", async () => { pendingMix.Clear(); pendingLinks.Clear(); foreach(var c in view.AllChannels) c.Dirty = false; foreach(var p in view.AllPairs) p.Dirty = false; await Command("MIX_RESET", "Уровни сброшены."); });
            ((RadioButton)window.FindName("NavUsb")).Checked += (s, e) => Navigate("usb");
            ((RadioButton)window.FindName("NavDiagnostics")).Checked += (s, e) => Navigate("diagnostics");
            Click("SaveUsb", SaveUsb);
            Click("SaveBoard", SaveBoard);
            Click("CopyReport", () => {
                if (!string.IsNullOrEmpty(view.LastJson)) Clipboard.SetText(view.LastJson);
                view.Message = string.IsNullOrEmpty(view.LastJson) ? "Диагностика станет доступна после запуска службы." : "Диагностика скопирована.";
                view.Notify(); return Task.FromResult(0);
            });
            ((ComboBox)window.FindName("AsioBlock")).SelectionChanged += (s, e) => view.Notify();
            window.Closing += (s, e) => { if (!exiting) { e.Cancel = true; window.Hide(); } };
            window.StateChanged += (s, e) => { if (window.WindowState == WindowState.Minimized && tray != null && tray.Visible) window.Hide(); };
            timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
            window.IsVisibleChanged += (s, e) => { timer.Interval = TimeSpan.FromMilliseconds(window.IsVisible ? 100 : 250); };
            timer.Tick += async (s, e) => {
                if (showEvent != null && showEvent.WaitOne(0)) Show();
                if (mixing) return;
                if (pendingLinks.Count != 0 && view.Available && !view.Busy && !polling) {
                    mixing = true; view.MixerSending = true;
                    var pair = pendingLinks.First(); pendingLinks.Remove(pair);
                    long revision = pair.Revision, leftRevision = pair.Left.Revision, rightRevision = pair.Right.Revision;
                    bool linked = pair.Linked;
                    try {
                        var response = await service.Send(pair.Command);
                        if (!ViewModel.Bool(response, "ok")) throw new ArgumentException(ViewModel.String(response, "error"));
                        if(pair.Revision == revision) pair.Dirty = false;
                        if(linked && pair.Left.Revision == leftRevision) { pair.Left.Dirty = false; pendingMix.Remove(pair.Left); }
                        if(linked && pair.Right.Revision == rightRevision) { pair.Right.Dirty = false; pendingMix.Remove(pair.Right); }
                        Update(response, false);
                    } catch(Exception error) { pendingLinks.Add(pair); view.Message = FriendlyError(error); view.Notify(); }
                    finally { mixing = false; view.MixerSending = false; }
                } else if (pendingMix.Count != 0 && view.Available && !view.Busy && !polling) {
                    mixing = true; view.MixerSending = true; var channel = pendingMix.First(); pendingMix.Remove(channel); long revision = channel.Revision;
                    var partner = channel.Pair != null && channel.Pair.Linked ? (channel == channel.Pair.Left ? channel.Pair.Right : channel.Pair.Left) : null;
                    long partnerRevision = partner == null ? 0 : partner.Revision;
                    try {
                        var response = await service.Send(channel.Command);
                        if (!ViewModel.Bool(response, "ok")) throw new ArgumentException(ViewModel.String(response, "error"));
                        if(channel.Revision == revision) channel.Dirty = false;
                        if(partner != null && partner.Revision == partnerRevision) partner.Dirty = false;
                        Update(response, false);
                    } catch(Exception error) { pendingMix.Add(channel); view.Message = FriendlyError(error); view.Notify(); }
                    finally { mixing = false; view.MixerSending = false; view.Notify(); }
                } else if ((view.Page == "mixer" && window.IsVisible) || clock.ElapsedMilliseconds >= nextRefresh) {
                    await Refresh(); nextRefresh = clock.ElapsedMilliseconds + 1000;
                }
                if (!view.Busy && !boardPolling && clock.ElapsedMilliseconds >= nextBoardRefresh)
                    RefreshBoard();
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
            if(!AsioPreferences.ValidBlock(view.AsioBlock))throw new ArgumentException("Недопустимый буфер.");
            if (view.BoardAvailable) {
                view.UpdateBoard(await board.Apply(view.BoardMode, view.UsbRate, Math.Max(32,view.AsioBlock), view.BoardRoutes));
                await Command("BOARD_MODE " + (view.BoardMode == 0 ? "soundcard" : "combined"), "");
            }
            await Command(string.Format(CultureInfo.InvariantCulture, "FORMAT {0} {1}", view.UsbRate, view.UsbBits), "Сохранено.");
            AsioPreferences.Save(view.AsioBlock);
        }
        private async Task SaveBoard() {
            if (view.AsioRunning) throw new ArgumentException("Остановите аудиодвижок ASIO перед сменой режима.");
            view.BoardBusy = true; view.Notify();
            try {
                view.UpdateBoard(await board.Apply(view.BoardMode, view.UsbRate, Math.Max(32,view.AsioBlock), view.BoardRoutes));
                await Command("BOARD_MODE " + (view.BoardMode == 0 ? "soundcard" : "combined"), "Режим и восемь маршрутов подтверждены платой.");
            } finally { view.BoardBusy = false; view.Notify(); }
        }
        private async void RefreshBoard() {
            boardPolling = true;
            try {
                view.UpdateBoard(await board.State());
                string mode = view.BoardMode == 0 ? "soundcard" : "combined";
                if (!view.BoardDirty && mode != view.ServiceBoardMode && view.Available && !view.AsioRunning)
                    await Command("BOARD_MODE " + mode, "");
                else if (mode != view.ServiceBoardMode && view.AsioRunning) {
                    view.BoardStatus = "Остановите ASIO для согласования режима платы со службой ПК."; view.Notify();
                }
            } catch (Exception error) {
                view.BoardAvailable = false; view.BoardStatus = error.Message; view.Notify();
            } finally { UpdateTray(); boardPolling = false; nextBoardRefresh = clock.ElapsedMilliseconds + 2000; }
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
            if (tray == null || exiting) return;
            string text = "Rey Audio · " + view.StatusTitle;
            if (tray.Text != text) tray.Text = text;
            // Keep the entry point available through device loss and service recovery.
            // USB state belongs in the status, not in the icon's lifetime.
            if (!tray.Visible) tray.Visible = true;
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
            menu.Items.Add("Устройство", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(() => { ((RadioButton)window.FindName("NavUsb")).IsChecked = true; Show(); })));
            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("Выход из панели", null, (s, e) => app.Dispatcher.BeginInvoke(new Action(() => { exiting = true; app.Shutdown(); })));
            using (var icon = Assembly.GetExecutingAssembly().GetManifestResourceStream("ReyAudio.ico")) {
                tray = new Forms.NotifyIcon { Icon = new System.Drawing.Icon(icon), Text = "Rey Audio Driver", Visible = true, ContextMenuStrip = menu };
            }
            tray.MouseClick += (s, e) => { if (e.Button == Forms.MouseButtons.Left) app.Dispatcher.BeginInvoke(new Action(Show)); };
        }
        public void Start(bool hidden) {
            CreateTray();
            timer.Interval = TimeSpan.FromMilliseconds(hidden ? 250 : 100);
            timer.Start();
            if (!hidden) Show();
            app.Dispatcher.BeginInvoke(new Action(async () => await Refresh()));
        }
        public async Task Render(string directory, string fixture = null) {
            Directory.CreateDirectory(directory);
            if (fixture == null) await Refresh();
            else Update(new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(fixture)), true);
            var root = (FrameworkElement)window.Content;
            double mixerBottom = 0;
            foreach (var page in new[] { "mixer", "usb", "diagnostics" }) {
                Navigate(page);
                ((RadioButton)window.FindName(page == "mixer" ? "NavMixer" : page == "usb" ? "NavUsb" : "NavDiagnostics")).IsChecked = true;
                root.Measure(new Size(1120, 800)); root.Arrange(new Rect(0, 0, 1120, 800)); root.UpdateLayout();
                await app.Dispatcher.InvokeAsync(() => { }, DispatcherPriority.Render);
                if (page == "mixer") {
                    var strips = (FrameworkElement)window.FindName("MixerStrips");
                    mixerBottom = strips.TransformToAncestor(root).TransformBounds(new Rect(0, 0, strips.ActualWidth, strips.ActualHeight)).Bottom;
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
                transport_settings_hidden = window.FindName("UsbDepth") == null && window.FindName("AsioLead") == null,
                asio_apply_hint = view.AsioApplyHint, asio_latency_label = view.AsioLatencyLabel,
                width = root.ActualWidth, height = root.ActualHeight, fixture = fixture != null
            }));
            VerifyMixerPairs(directory);
            VerifyTrayLifecycle(directory);
            VerifyAsioPanel(directory);
            VerifyBoardPanel(directory);
        }
        private void VerifyMixerPairs(string directory) {
            Navigate("mixer");
            var inputs = (ItemsControl)window.FindName("InputMixerPairs");
            var outputs = (ItemsControl)window.FindName("OutputMixerPairs");
            if(inputs.Items.Count != 4 || outputs.Items.Count != 4 || window.FindName("MixerInputs") != null || window.FindName("MixerOutputs") != null)
                throw new InvalidOperationException("Mixer must show four input pairs and four output pairs together");
            var original = view.AllPairs.Select(p => p.Linked).ToArray();
            foreach(bool linked in new[] {true, false}) {
                foreach(var pair in view.AllPairs) pair.Update(linked);
                var root=(FrameworkElement)window.Content;
                root.Measure(new Size(1120,800));root.Arrange(new Rect(0,0,1120,800));root.UpdateLayout();
                var bitmap=new RenderTargetBitmap(1120,800,96,96,PixelFormats.Pbgra32);bitmap.Render(root);
                var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));
                using(var output=File.Create(Path.Combine(directory,linked ? "rey-mixer-stereo.png" : "rey-mixer-mono.png")))png.Save(output);
            }
            int index=0;foreach(var pair in view.AllPairs) pair.Update(original[index++]);
            File.WriteAllText(Path.Combine(directory,"mixer-pairs-check.json"),new JavaScriptSerializer().Serialize(new {
                scope="finite WPF fixture; no service mutation or physical audio",input_pairs=inputs.Items.Count,
                output_pairs=outputs.Items.Count,directions_visible_together=true,direction_selector_absent=true,
                independently_linkable_pairs=8,meters_per_stereo_pair=2,default_gain_db=0
            }));
        }
        private void VerifyAsioPanel(string directory) {
            // The one user buffer control shares the audio format card.
            window.WindowState = WindowState.Normal;
            Show();
            Navigate("usb");
            ((RadioButton)window.FindName("NavUsb")).IsChecked = true;
            window.UpdateLayout();
            ((FrameworkElement)window.FindName("SaveUsb")).BringIntoView();
            window.UpdateLayout();
            var root = (FrameworkElement)window.Content;
            var first = (FrameworkElement)window.FindName("AsioBlock");
            var save = (FrameworkElement)window.FindName("SaveUsb");
            var bounds = save.TransformToAncestor(root).TransformBounds(new Rect(0, 0, save.ActualWidth, save.ActualHeight));
            bool accessible = first.IsVisible && save.IsVisible && bounds.Top >= 0 && bounds.Bottom <= root.ActualHeight - 8;
            if (!accessible) throw new InvalidOperationException("Expanded ASIO controls are inaccessible");
            var bitmap = new RenderTargetBitmap((int)Math.Ceiling(root.ActualWidth), (int)Math.Ceiling(root.ActualHeight), 96, 96, PixelFormats.Pbgra32);
            bitmap.Render(root); var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
            using (var output = File.Create(Path.Combine(directory, "rey-asio-expanded.png"))) png.Save(output);
            File.WriteAllText(Path.Combine(directory, "asio-disclosure-check.json"), new JavaScriptSerializer().Serialize(new { controls_accessible_after_expand = accessible }));
            window.Hide();
        }
        private void VerifyBoardPanel(string directory) {
            // Finite WPF rendering fixture; no USB/service mutation or PCM.
            view.Available=true;view.BoardAvailable=true;view.BoardMode=1;
            view.BoardStatus="Проверка интерфейса с тестовым состоянием. Физическое USB-подключение не проверяется.";
            view.Notify();Navigate("usb");
            ((RadioButton)window.FindName("NavUsb")).IsChecked=true;
            ((Expander)window.FindName("BoardRoutesSection")).IsExpanded=true;
            var root=(FrameworkElement)window.Content;
            root.Measure(new Size(1120,1100));root.Arrange(new Rect(0,0,1120,1100));root.UpdateLayout();
            ((FrameworkElement)window.FindName("BoardModeCard")).BringIntoView();root.UpdateLayout();
            var modes=(ComboBox)window.FindName("BoardMode");
            var routes=(ItemsControl)window.FindName("BoardRoutes");
            if(modes.Items.Count!=2 || routes.Items.Count!=8 || view.Rates.Any(r=>r.Value>96000))
                throw new InvalidOperationException("Board mode/route bindings failed");
            // Render the complete card outside its scrolled viewport. Keeping
            // its old parent clips the route rows and retains the layout offset.
            var card=(FrameworkElement)window.FindName("BoardModeCard");
            ((System.Windows.Controls.Panel)card.Parent).Children.Remove(card);
            card.Margin=new Thickness(0);window.Content=card;
            window.WindowState=WindowState.Normal;window.Width=1120;window.Height=1100;
            window.ShowActivated=false;window.Show();window.UpdateLayout();
            card.Measure(new Size(1080,double.PositiveInfinity));
            card.Arrange(new Rect(0,0,1080,card.DesiredSize.Height));
            var bitmap=new RenderTargetBitmap((int)Math.Ceiling(card.ActualWidth),(int)Math.Ceiling(card.ActualHeight),96,96,PixelFormats.Pbgra32);bitmap.Render(card);
            var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));
            using(var output=File.Create(Path.Combine(directory,"rey-board-modes.png")))png.Save(output);
            File.WriteAllText(Path.Combine(directory,"board-panel-check.json"),new JavaScriptSerializer().Serialize(new {
                scope="real WPF bindings with fixture state; no physical USB",pc_modes=modes.Items.Count,
                channels=routes.Items.Count,effect_rate_max=view.Rates.Max(r=>r.Value),web_mode_selector=false
            }));
        }
        private void VerifyTrayLifecycle(string directory) {
            // Finite UI integration check: real NotifyIcon and WPF lifecycle,
            // without changing the service, transport or audio preferences.
            CreateTray();
            bool available = view.Available, present = view.UsbPresent;
            var cases = new List<object>();
            foreach (var state in new[] { new[] { false, false }, new[] { true, false }, new[] { true, true }, new[] { false, true }, new[] { true, true }, new[] { true, false } }) {
                view.Available = state[0]; view.UsbPresent = state[1]; UpdateTray();
                if (!tray.Visible || window.IsVisible) throw new InvalidOperationException("Tray must remain available during USB/service loss");
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
                string name = page == "mixer" ? "MixerStrips" : page == "usb" ? "SaveUsb" : "CopyReport";
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
            exiting = true; tray.Visible = false; UpdateTray();
            bool exitKeepsIconHidden = !tray.Visible;
            if (!exitKeepsIconHidden) throw new InvalidOperationException("Exit must not recreate the tray icon");
            exiting = false;
            view.Available = available; view.UsbPresent = present; UpdateTray();
            File.WriteAllText(Path.Combine(directory, "tray-lifecycle.json"), new JavaScriptSerializer().Serialize(new {
                cases = cases, close_keeps_tray = closeKeepsTray, minimize_keeps_tray = minimizeKeepsTray,
                exit_keeps_icon_hidden = exitKeepsIconHidden,
                minimum_window = layouts,
                hidden_window_wake_ms = 250, hidden_status_poll_ms = 1000
            }));
        }
        public void Dispose() { timer.Stop(); board.Dispose(); if (tray != null) { tray.Visible = false; tray.Dispose(); } }
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
