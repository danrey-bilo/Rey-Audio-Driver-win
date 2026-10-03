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
        public ObservableCollection<LogRow> Events { get { return Log; } }
        public Choice[] Rates { get; private set; }
        public Choice[] Bits { get; private set; }
        public Choice[] Depths { get; private set; }
        public Choice[] Blocks { get; private set; }
        public string Page = "usb";
        public bool Available, Busy, UsbPresent, DriverPresent, DigitalTest, LanEnabled;
        public string State = "offline", Route = "none", Preferred = "auto", Identity = "", Backend = "";
        public string LastJson = "", Detail = "";
        public int UsbRate { get; set; }
        public int UsbBits { get; set; }
        public int UsbDepth { get; set; }
        public int UsbBlock { get; set; }
        public string UsbGuard { get; set; }
        public bool UsbAutomatic { get; set; }
        public string LanPeer { get; set; }
        public string LanPort { get; set; }
        public int LanBlock { get; set; }
        public string LanGuard { get; set; }
        public string LanFrames { get; set; }
        public bool LanEnergy { get; set; }
        public string Message { get; set; }
        public long Callbacks, Frames, Missing;
        public double Gap;
        public int ActiveRate, ActiveBits;
        public ViewModel() {
            Rates = new[] { new Choice(44100, "44,1 кГц"), new Choice(48000, "48 кГц"), new Choice(88200, "88,2 кГц"), new Choice(96000, "96 кГц"), new Choice(176400, "176,4 кГц"), new Choice(192000, "192 кГц") };
            Bits = new[] { new Choice(16, "16 бит"), new Choice(24, "24 бит"), new Choice(32, "32 бит") };
            Depths = new[] { new Choice(1, "1 пакет"), new Choice(2, "2 пакета"), new Choice(3, "3 пакета"), new Choice(4, "4 пакета"), new Choice(6, "6 пакетов"), new Choice(8, "8 пакетов"), new Choice(12, "12 пакетов"), new Choice(16, "16 пакетов") };
            Blocks = new[] { new Choice(16, "16 кадров"), new Choice(32, "32 кадра"), new Choice(64, "64 кадра"), new Choice(128, "128 кадров"), new Choice(256, "256 кадров") };
            UsbRate = 192000; UsbBits = 32; UsbDepth = 3; UsbBlock = 64; UsbGuard = "0"; UsbAutomatic = true;
            LanPeer = ""; LanPort = "50021"; LanBlock = 256; LanGuard = "1536"; LanFrames = "32"; Message = "";
        }
        public Visibility UsbVisibility { get { return Page == "usb" ? Visibility.Visible : Visibility.Collapsed; } }
        public Visibility LanVisibility { get { return Page == "lan" ? Visibility.Visible : Visibility.Collapsed; } }
        public Visibility DiagnosticsVisibility { get { return Page == "diagnostics" ? Visibility.Visible : Visibility.Collapsed; } }
        public string PageTitle { get { return Page == "usb" ? "USB" : Page == "lan" ? "AoIP / LAN" : "Диагностика"; } }
        public string PageSubtitle { get { return Page == "usb" ? "Прямое подключение Raspberry Pi 5 к компьютеру" : Page == "lan" ? "Аудио через встроенный Ethernet-порт" : "Состояние службы, подключения и события"; } }
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
                if (!DriverPresent && !DigitalTest) return "Аудиодрайвер ещё не установлен";
                if (State == "digital_test") return "Цифровой тест · " + (Route == "usb" ? "USB" : "LAN");
                if (State == "streaming") return "Аудио подключено · " + (Route == "usb" ? "USB" : "LAN");
                if (State == "connecting") return "Подключение устройства…";
                if (State == "stopping") return "Применение настроек…";
                if (State == "idle") return UsbPresent ? "USB обнаружен" : "Ожидание устройства";
                return "Проверьте подключение";
            }
        }
        public string StatusBody {
            get {
                if (!Available) return "Установите и запустите Rey Audio Service. После установки она запускается вместе с Windows.";
                if (!DriverPresent && !DigitalTest) return "USB обнаруживается и настройки доступны. Для входов и выходов в Windows нужен подписанный драйвер Rey Audio.";
                if (DigitalTest) return "Стенд проверяет цифровой транспорт. Системные аудиовходы и выходы в этом режиме не создаются.";
                if (State == "streaming") return "Устройство доступно аудиоприложениям Windows. Смена транспорта перезапускает подключение.";
                if (State == "idle") return "USB подключается автоматически. Для LAN укажите адрес устройства и нажмите «Подключить».";
                return Detail;
            }
        }
        public string StatusColor { get { return !Available ? "#8391A5" : !DriverPresent && !DigitalTest ? "#B87512" : State == "streaming" || State == "digital_test" ? "#128466" : "#53708F"; } }
        public string RouteLabel { get { return Route == "none" ? "Не подключено" : Route == "usb" ? "USB" : "AoIP / LAN"; } }
        public string PreferredLabel { get { return Preferred == "usb" ? "Выбран USB" : Preferred == "lan" ? "Выбран LAN" : "LAN по умолчанию"; } }
        public string DriverLabel { get { return DriverPresent ? "Доступен" : "Не установлен"; } }
        public string BackendLabel { get { return Backend.Length == 0 ? "—" : Backend; } }
        public string CallbackLabel { get { return Callbacks.ToString("N0"); } }
        public string FrameLabel { get { return Frames.ToString("N0"); } }
        public string MissingLabel { get { return Missing.ToString("N0"); } }
        public string GapLabel { get { return Gap.ToString("0.0", CultureInfo.CurrentCulture) + " мкс"; } }
        public string LanFormatLabel { get { return Route == "lan" && ActiveRate > 0 ? (ActiveRate / 1000.0).ToString("0.#") + " кГц · PCM" + ActiveBits : "Формат определяется устройством Pi"; } }
        public string LanActionLabel { get { return LanEnabled ? "Отключить LAN" : "Подключить LAN"; } }
        public string QueueHint { get { return "Запас очереди: " + (UsbDepth * 0.125).ToString("0.###") + " мс. Это не сквозная задержка."; } }
        public Visibility MessageVisibility { get { return string.IsNullOrEmpty(Message) ? Visibility.Collapsed : Visibility.Visible; } }
        public static string String(Dictionary<string, object> d, string key) { return d.ContainsKey(key) && d[key] != null ? Convert.ToString(d[key], CultureInfo.InvariantCulture) : ""; }
        public static int Int(Dictionary<string, object> d, string key) { int v; return int.TryParse(String(d, key), out v) ? v : 0; }
        public static bool Bool(Dictionary<string, object> d, string key) { return d.ContainsKey(key) && d[key] is bool && (bool)d[key]; }
        public void Update(Dictionary<string, object> data, bool loadFields) {
            Available = true; State = String(data, "state"); Detail = String(data, "detail"); Route = String(data, "route");
            Preferred = String(data, "preferred"); DriverPresent = Bool(data, "driver_present"); DigitalTest = Bool(data, "digital_test");
            Identity = String(data, "identity"); Backend = String(data, "backend");
            var usb = (Dictionary<string, object>)data["usb"]; var lan = (Dictionary<string, object>)data["lan"];
            UsbPresent = Int(usb, "present") == 1; LanEnabled = Bool(lan, "enabled");
            if (Identity.Length == 0) Identity = String(usb, "identity");
            if (loadFields) {
                UsbRate = Int(usb, "rate"); UsbBits = Int(usb, "bits"); UsbDepth = Int(usb, "depth"); UsbBlock = Int(usb, "block");
                UsbGuard = String(usb, "guard"); UsbAutomatic = Bool(usb, "automatic");
                LanPeer = String(lan, "peer"); LanPort = String(lan, "port"); LanBlock = Int(lan, "block");
                LanGuard = String(lan, "guard"); LanFrames = String(lan, "frames"); LanEnergy = Bool(lan, "energy");
            }
            var active = (Dictionary<string, object>)data["active"]; ActiveRate = Int(active, "rate"); ActiveBits = Int(active, "bits");
            var stats = (Dictionary<string, object>)data["stats"];
            Callbacks = Convert.ToInt64(stats["callbacks"]); Frames = Convert.ToInt64(stats["frames"]); Missing = Convert.ToInt64(stats["missing_frames"]); Gap = Convert.ToDouble(stats["callback_gap_max_us"], CultureInfo.InvariantCulture);
            Log.Clear();
            foreach (Dictionary<string, object> row in (IEnumerable)data["events"]) {
                var stamp = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc).AddMilliseconds(Convert.ToDouble(row["utc_ms"])).ToLocalTime();
                Log.Add(new LogRow { Time = stamp.ToString("HH:mm:ss"), Title = String(row, "code"), Detail = String(row, "detail") });
            }
            Notify();
        }
    }
}
