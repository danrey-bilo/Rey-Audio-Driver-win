using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.Windows;
namespace ReyAudio {
    internal sealed class Choice {
        public int Value { get; private set; }
        public string Label { get; private set; }
        public Choice(int value, string label) { Value = value; Label = label; }
        public override string ToString() { return Label; }
    }
    internal sealed class LogRow {
        public string Time { get; set; }
        public string Title { get; set; }
        public string Detail { get; set; }
    }
    internal sealed class ViewModel : INotifyPropertyChanged {
        public event PropertyChangedEventHandler PropertyChanged;
        public void Notify() { if (PropertyChanged != null) PropertyChanged(this, new PropertyChangedEventArgs("")); }
        public readonly ObservableCollection<LogRow> Log = new ObservableCollection<LogRow>();
        public bool MixerSending;
        public bool CanEditMixer { get { return CanApply && UsbPresent && Identity.Length == 32; } }
        public string DeviceLabel { get { return "Rey Audio · Pi5"; } }
        public string DeviceWarning = "";
        public ObservableCollection<LogRow> Events { get { return Log; } }
        public Choice[] Rates { get; private set; }
        public Choice[] Bits { get; private set; }
        public Choice[] Depths { get; private set; }
        public Choice[] Blocks { get; private set; }
        public Choice[] AsioLeads { get; private set; }
        public int AsioBlock { get; set; }
        public int AsioLead { get; set; }
        public string AsioStateLabel { get; private set; }
        public string AsioCountersLabel { get; private set; }
        private int liveAsioBlock, liveAsioLead, activeUsbDepth;
        private bool asioRunning;
        public bool AsioRunning { get { return asioRunning && Available; } }
        private bool asioDeadlineErrors;
        public string AsioHealthColor { get { return asioDeadlineErrors ? "#B87512" : "#137F6C"; } }
        public string AsioLatencyLabel {
            get {
                if (!AsioRunning || !UsbPresent || ActiveRate <= 0 || liveAsioBlock <= 0 || activeUsbDepth <= 0) return "—";
                return (AsioInputLatency + AsioOutputLatency).ToString("0.00") + " мс";
            }
        }
        private double AsioInputLatency { get { return (liveAsioBlock + activeUsbDepth * ((ActiveRate + 7999) / 8000)) * 1000.0 / ActiveRate; } }
        private double AsioOutputLatency { get { return ((liveAsioLead - 1) * liveAsioBlock + 1) * 1000.0 / ActiveRate; } }
        public string AsioLatencyDetail { get { return AsioLatencyLabel == "—" ? "Нет активного ASIO-потока" : "Вход " + AsioInputLatency.ToString("0.00") + " мс · выход " + AsioOutputLatency.ToString("0.00") + " мс. Расчёт буферов; физическая задержка не измеряется."; } }
        public string AsioApplyHint {
            get {
                return AsioRunning && (liveAsioBlock != AsioBlock || liveAsioLead != AsioLead)
                    ? "Применяется после перезапуска аудиодвижка." : "";
            }
        }
        public Visibility AsioApplyHintVisibility { get { return AsioApplyHint.Length == 0 ? Visibility.Collapsed : Visibility.Visible; } }
        public bool CanApplyUsb { get { return CanApply && !AsioRunning; } }
        public Visibility UsbBusyHintVisibility { get { return AsioRunning ? Visibility.Visible : Visibility.Collapsed; } }
        public bool AsioOnly;
        public bool CanSaveAsio { get { return !Busy; } }
        public string Page = "mixer";
        public bool MixerOutputs;
        public MixerChannel[] Inputs { get; private set; }
        public MixerChannel[] Outputs { get; private set; }
        public MixerChannel Master { get; private set; }
        public MixerChannel[] VisibleChannels { get { return MixerOutputs ? Outputs : Inputs; } }
        public IEnumerable<MixerChannel> AllChannels { get { foreach(var c in Inputs) yield return c; foreach(var c in Outputs) yield return c; yield return Master; } }
        public Visibility MixerVisibility { get { return Page == "mixer" ? Visibility.Visible : Visibility.Collapsed; } }
        public string MixerFormat { get { return Available && UsbPresent && ActiveRate > 0 ? (ActiveRate / 1000.0).ToString("0.#") + " кГц · " + ActiveBits + " бит" : "8 × 8"; } }
        public bool Available, Busy, UsbPresent, DriverPresent, DigitalTest;
        public string State = "offline", Route = "none", Identity = "", Backend = "";
        public string LastJson = "", Detail = "";
        public int UsbRate { get; set; }
        public int UsbBits { get; set; }
        public int UsbDepth { get; set; }
        public int UsbBlock { get; set; }
        public string UsbGuard { get; set; }
        public bool UsbAutomatic { get; set; }
        public string Message { get; set; }
        public long Callbacks, Frames, Missing;
        public double Gap;
        public int ActiveRate, ActiveBits;
        public ViewModel() {
            Inputs = new MixerChannel[8]; Outputs = new MixerChannel[8]; Master = new MixerChannel(2, 0);
            for(int i = 0; i < 8; ++i) { Inputs[i] = new MixerChannel(0, i); Outputs[i] = new MixerChannel(1, i); }
            Rates = new[] { new Choice(44100, "44,1 кГц"), new Choice(48000, "48 кГц"), new Choice(88200, "88,2 кГц"), new Choice(96000, "96 кГц"), new Choice(176400, "176,4 кГц"), new Choice(192000, "192 кГц") };
            Bits = new[] { new Choice(16, "16 бит"), new Choice(24, "24 бит"), new Choice(32, "32 бит") };
            Depths = new[] { new Choice(1, "1 пакет"), new Choice(2, "2 пакета"), new Choice(3, "3 пакета"), new Choice(4, "4 пакета"), new Choice(6, "6 пакетов"), new Choice(8, "8 пакетов"), new Choice(12, "12 пакетов"), new Choice(16, "16 пакетов") };
            Blocks = new[] { new Choice(16, "16 Samples"), new Choice(32, "32 Samples"), new Choice(64, "64 Samples"), new Choice(128, "128 Samples"), new Choice(256, "256 Samples") };
            AsioLeads = new[] { new Choice(1, "1 блок"), new Choice(2, "2 блока"), new Choice(3, "3 блока"), new Choice(4, "4 блока") };
            var asio = AsioPreferences.Load(); AsioBlock = asio.Block; AsioLead = asio.Lead;
            AsioStateLabel = "ASIO не активен"; AsioCountersLabel = "—";
            UsbRate = 192000; UsbBits = 32; UsbDepth = 4; UsbBlock = 64; UsbGuard = "0"; UsbAutomatic = true;
            Message = "";
        }
        public Visibility UsbVisibility { get { return Page == "usb" ? Visibility.Visible : Visibility.Collapsed; } }
        public Visibility DiagnosticsVisibility { get { return Page == "diagnostics" ? Visibility.Visible : Visibility.Collapsed; } }
        public string PageTitle { get { return Page == "mixer" ? "Микшер" : Page == "usb" ? "Настройки" : "Диагностика"; } }
        public string ServiceLabel { get { return Available ? "Служба работает" : "Служба недоступна"; } }
        public bool CanApply { get { return Available && !Busy; } }
        public string ShortIdentity { get { return Identity.Length >= 8 ? Identity.Substring(Identity.Length - 8).ToUpperInvariant() : "—"; } }
        public string StatusTitle {
            get {
                if (!Available) return "Нет связи";
                if (DeviceWarning.Length != 0) return "Проверьте подключение";
                if (State == "connecting") return "Подключение…";
                if (State == "stopping") return "Применение…";
                return UsbPresent ? "Подключено" : "Не подключено";
            }
        }
        public string StatusBody {
            get {
                if (DeviceWarning.Length != 0) return DeviceWarning;
                if (!Available) return "Служба Rey Audio недоступна.";
                return UsbPresent ? "" : "Подключите аудиокарту по USB.";
            }
        }
        public Visibility StatusBodyVisibility { get { return StatusBody.Length == 0 ? Visibility.Collapsed : Visibility.Visible; } }
        public string StatusColor { get { return !Available || !UsbPresent ? "#8391A5" : DeviceWarning.Length != 0 ? "#B87512" : "#128466"; } }
        public string RouteLabel { get { return Route == "usb" ? "USB" : "Не подключено"; } }
        public string DriverLabel { get { return AsioOnly ? "USB-ASIO" : DriverPresent ? "Доступен" : "Не установлен"; } }
        public string BackendLabel { get { return Backend.Length == 0 ? "—" : Backend; } }
        public string CallbackLabel { get { return Callbacks.ToString("N0"); } }
        public string FrameLabel { get { return Frames.ToString("N0"); } }
        public string MissingLabel { get { return Missing.ToString("N0"); } }
        public string GapLabel { get { return Gap.ToString("0.0", CultureInfo.CurrentCulture) + " мкс"; } }
        public string QueueHint { get { return "Запас очереди " + (UsbDepth * 0.125).ToString("0.###") + " мс; не сквозная задержка."; } }
        public Visibility MessageVisibility { get { return string.IsNullOrEmpty(Message) ? Visibility.Collapsed : Visibility.Visible; } }
        public static string String(Dictionary<string, object> d, string key) { return d.ContainsKey(key) && d[key] != null ? Convert.ToString(d[key], CultureInfo.InvariantCulture) : ""; }
        public static int Int(Dictionary<string, object> d, string key) { int v; return int.TryParse(String(d, key), out v) ? v : 0; }
        public static bool Bool(Dictionary<string, object> d, string key) { return d.ContainsKey(key) && d[key] is bool && (bool)d[key]; }
        public void Update(Dictionary<string, object> data, bool loadFields) {
            string identity = String(data, "identity");
            if (identity != Identity) {
                loadFields = true;
                foreach (var channel in AllChannels) channel.Dirty = false;
            }
            DeviceWarning = String(data, "device_warning");
            if (DeviceWarning == "Connect only one Rey Audio USB device") DeviceWarning = "Обнаружено несколько устройств. Оставьте подключённой одну USB-карту Rey Audio.";
            else if (DeviceWarning == "Only one USB device is supported; the additional device is ignored") DeviceWarning = "Работает первая USB-карта. Дополнительное устройство не подключается к аудиопотоку.";
            Available = true; State = String(data, "state"); Detail = String(data, "detail"); Route = String(data, "route");
            DriverPresent = Bool(data, "driver_present"); DigitalTest = Bool(data, "digital_test");
            AsioOnly = Bool(data, "asio_only");
            if (data.ContainsKey("asio")) {
                var asio = (Dictionary<string, object>)data["asio"];
                asioRunning = Bool(asio, "running"); liveAsioBlock = Int(asio, "block"); liveAsioLead = Int(asio, "lead_blocks");
                asioDeadlineErrors = false;
                foreach (var counter in new[] { "capture_dropped", "render_late_frames", "render_missing_frames", "render_overflow" })
                    if (asio.ContainsKey(counter) && Convert.ToUInt64(asio[counter]) != 0) asioDeadlineErrors = true;
                AsioStateLabel = Bool(asio, "running") ? "Активно · " + Int(asio, "block") + " Samples" : Bool(asio, "ready") ? "ASIO готов" : "ASIO не активен";
                AsioCountersLabel = Bool(asio, "connected") ? "Пропуски входа: " + String(asio, "capture_dropped") + " · опоздания выхода: " + String(asio, "render_late_frames") + " · пропуски выхода: " + String(asio, "render_missing_frames") + " · переполнение: " + String(asio, "render_overflow") : "—";
            }
            Identity = String(data, "identity"); Backend = String(data, "backend");
            var usb = (Dictionary<string, object>)data["usb"];
            activeUsbDepth = Int(usb, "depth");
            UsbPresent = Int(usb, "present") == 1;
            if (Identity.Length == 0) Identity = String(usb, "identity");
            if (loadFields || asioRunning) {
                UsbRate = Int(usb, "rate"); UsbBits = Int(usb, "bits"); UsbDepth = Int(usb, "depth"); UsbBlock = Int(usb, "block");
                UsbGuard = String(usb, "guard"); UsbAutomatic = Bool(usb, "automatic");
            }
            var active = (Dictionary<string, object>)data["active"]; ActiveRate = Int(active, "rate"); ActiveBits = Int(active, "bits");
            var stats = (Dictionary<string, object>)data["stats"];
            Callbacks = Convert.ToInt64(stats["callbacks"]); Frames = Convert.ToInt64(stats["frames"]); Missing = Convert.ToInt64(stats["missing_frames"]); Gap = Convert.ToDouble(stats["callback_gap_max_us"], CultureInfo.InvariantCulture);
            if (data.ContainsKey("mixer")) {
                var mixer = (Dictionary<string, object>)data["mixer"]; int i = 0;
                bool meters = State == "streaming" || State == "digital_test";
                foreach(Dictionary<string, object> row in (IEnumerable)mixer["inputs"]) { if (i < 8) Inputs[i++].Update(row, meters); }
                i = 0; foreach(Dictionary<string, object> row in (IEnumerable)mixer["outputs"]) { if (i < 8) Outputs[i++].Update(row, meters); }
                Master.Update(new Dictionary<string, object> { {"gain_cdb", mixer["master_cdb"]}, {"mute", mixer["master_mute"]} }, false);
            }
            Log.Clear();
            foreach (Dictionary<string, object> row in (IEnumerable)data["events"]) {
                var stamp = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc).AddMilliseconds(Convert.ToDouble(row["utc_ms"])).ToLocalTime();
                Log.Add(new LogRow { Time = stamp.ToString("HH:mm:ss"), Title = String(row, "code"), Detail = String(row, "detail") });
            }
            Notify();
        }
    }
}
