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
        public bool CanEditMixer { get { return CanApply && Identity.Length == 32; } }
        public bool CanUseWindowsSound { get { return !AsioOnly && DriverPresent; } }
        public string DeviceLabel { get { return UsbPresent ? "Rey Audio · " + ShortIdentity + " · USB" : "Подключите Rey Audio по USB"; } }
        public string DeviceHint { get { return "Одна USB-карта · 8 входов и 8 выходов · подключается автоматически"; } }
        public string DeviceWarning = "";
        public string EndpointHint { get { return AsioOnly ? "Ableton · ASIO → Rey Audio USB ASIO · входы 1–8 и выходы 1–8. Системные устройства Windows устанавливаются отдельным этапом." : "Windows · Rey Audio " + ShortIdentity + " · входы и выходы 1/2, 3/4, 5/6, 7/8 + 1–8. Для приложения выберите нужную пару в настройках звука."; } }
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
        public string MixerSourceLabel { get { return MixerOutputs ? "Воспроизведение · приложение → Pi" : "Захват · Pi → приложение"; } }
        public string MixerFormat { get { return ActiveRate > 0 ? (ActiveRate / 1000.0).ToString("0.#") + " кГц · PCM" + ActiveBits + " · " + RouteLabel : "8 входов / 8 выходов"; } }
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
            Blocks = new[] { new Choice(16, "16 кадров"), new Choice(32, "32 кадра"), new Choice(64, "64 кадра"), new Choice(128, "128 кадров"), new Choice(256, "256 кадров") };
            AsioLeads = new[] { new Choice(1, "1 блок"), new Choice(2, "2 блока"), new Choice(3, "3 блока"), new Choice(4, "4 блока") };
            var asio = AsioPreferences.Load(); AsioBlock = asio.Block; AsioLead = asio.Lead;
            AsioStateLabel = "Ожидание USB-сессии"; AsioCountersLabel = "Счётчики доступны при работающем ASIO-host";
            UsbRate = 192000; UsbBits = 32; UsbDepth = 3; UsbBlock = 64; UsbGuard = "0"; UsbAutomatic = true;
            Message = "";
        }
        public Visibility UsbVisibility { get { return Page == "usb" ? Visibility.Visible : Visibility.Collapsed; } }
        public Visibility DiagnosticsVisibility { get { return Page == "diagnostics" ? Visibility.Visible : Visibility.Collapsed; } }
        public string PageTitle { get { return Page == "mixer" ? "Микшер" : Page == "usb" ? "USB" : "Диагностика"; } }
        public string PageSubtitle { get { return Page == "mixer" ? "Уровни и управление восемью каналами в каждом направлении" : Page == "usb" ? "Прямое подключение Raspberry Pi 5 к компьютеру" : "Состояние службы, подключения и события"; } }
        public string ServiceLabel { get { return Available ? "Служба работает" : "Служба недоступна"; } }
        public string ServiceColor { get { return Available ? "#72DEBD" : "#AAB4C4"; } }
        public bool CanApply { get { return Available && !Busy; } }
        public bool CanUseUsb { get { return CanApply && UsbPresent; } }
        public string UsbDeviceLabel { get { return UsbPresent ? "Rey Audio · Pi5" : "Подключите Rey Audio по USB"; } }
        public string UsbConnectionLabel { get { return UsbPresent ? "USB High-Speed · 8 входов / 8 выходов" : "Устройство появится здесь автоматически"; } }
        public string ShortIdentity { get { return Identity.Length >= 8 ? Identity.Substring(Identity.Length - 8).ToUpperInvariant() : "—"; } }
        public string StatusTitle {
            get {
                if (!Available) return "Ожидание службы";
                if (DeviceWarning.Length != 0) return "Проверьте USB-подключение";
                if (!AsioOnly && !DriverPresent && !DigitalTest) return "Аудиодрайвер ещё не установлен";
                if (State == "digital_test") return "Цифровой тест · " + "USB";
                if (State == "streaming") return "Аудио подключено · " + "USB";
                if (State == "connecting") return "Подключение устройства…";
                if (State == "stopping") return "Применение настроек…";
                if (State == "idle") return UsbPresent ? "USB обнаружен" : "Ожидание устройства";
                return "Проверьте подключение";
            }
        }
        public string StatusBody {
            get {
                if (DeviceWarning.Length != 0) return DeviceWarning;
                if (!Available) return "Установите и запустите Rey Audio Service. После установки она запускается вместе с Windows.";
                if (!AsioOnly && !DriverPresent && !DigitalTest) return "Установите Rey Audio Driver через MSI/EXE. Настройки и микшер доступны здесь; уровни появятся при работающем аудиопотоке.";
                if (DigitalTest) return "Стенд проверяет цифровой транспорт. Системные аудиовходы и выходы в этом режиме не создаются.";
                if (State == "streaming") return AsioOnly ? "В Ableton выберите ASIO → Rey Audio USB ASIO. Закройте ASIO в приложении перед изменением формата USB." : "Устройство доступно аудиоприложениям Windows. Настройки USB применяются после перезапуска подключения.";
                if (State == "idle") return "USB подключается автоматически. Подключите одну карту Rey Audio к USB-порту компьютера.";
                return Detail;
            }
        }
        public string StatusColor { get { return !Available ? "#8391A5" : !AsioOnly && !DriverPresent && !DigitalTest ? "#B87512" : State == "streaming" || State == "digital_test" ? "#128466" : "#53708F"; } }
        public string RouteLabel { get { return Route == "usb" ? "USB" : "Не подключено"; } }
        public string DriverLabel { get { return AsioOnly ? "USB-ASIO" : DriverPresent ? "Доступен" : "Не установлен"; } }
        public string BackendLabel { get { return Backend.Length == 0 ? "—" : Backend; } }
        public string CallbackLabel { get { return Callbacks.ToString("N0"); } }
        public string FrameLabel { get { return Frames.ToString("N0"); } }
        public string MissingLabel { get { return Missing.ToString("N0"); } }
        public string GapLabel { get { return Gap.ToString("0.0", CultureInfo.CurrentCulture) + " мкс"; } }
        public string QueueHint { get { return "Запас очереди: " + (UsbDepth * 0.125).ToString("0.###") + " мс. Это не сквозная задержка."; } }
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
                AsioStateLabel = Bool(asio, "running") ? "ASIO работает · " + Int(asio, "block") + " кадров · запас " + Int(asio, "lead_blocks") + " блока" : Bool(asio, "ready") ? "USB-ASIO готов · откройте драйвер в Ableton" : "Ожидание активной USB-карты";
                AsioCountersLabel = Bool(asio, "connected") ? "Пропуски входа: " + String(asio, "capture_dropped") + " · опоздания выхода: " + String(asio, "render_late_frames") + " · пропуски выхода: " + String(asio, "render_missing_frames") + " · переполнение: " + String(asio, "render_overflow") : "Счётчики появятся после запуска ASIO в приложении";
            }
            Identity = String(data, "identity"); Backend = String(data, "backend");
            var usb = (Dictionary<string, object>)data["usb"];
            UsbPresent = Int(usb, "present") == 1;
            if (Identity.Length == 0) Identity = String(usb, "identity");
            if (loadFields) {
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
