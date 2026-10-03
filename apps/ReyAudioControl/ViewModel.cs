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
    internal sealed class AudioCard : INotifyPropertyChanged {
        public string Id { get; private set; }
        public string Label { get; private set; }
        public event PropertyChangedEventHandler PropertyChanged;
        public AudioCard(string id) { Id = id; }
        public override string ToString() { return Label; }
        public void Update(Dictionary<string, object> row) {
            string route = ViewModel.String(row, "route"), state = ViewModel.String(row, "state");
            string next = ViewModel.String(row, "name") + " · " + (route == "usb" ? "USB" : route == "lan" ? "LAN" : "ожидание") +
                (state == "streaming" || state == "digital_test" ? " · работает" : "");
            if (Label == next) return;
            Label = next; if (PropertyChanged != null) PropertyChanged(this, new PropertyChangedEventArgs("Label"));
        }
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
        public ObservableCollection<AudioCard> Cards { get; private set; }
        public string SelectedDevice { get; private set; }
        public int RunningCards;
        public bool MixerSending;
        public bool CanSelectCard { get { return CanApply && !MixerSending && Cards.Count > 0; } }
        public bool CanEditCard { get { return CanApply && SelectedDevice.Length == 32; } }
        public bool CanDisconnectLan { get { return CanEditCard && LanEnabled; } }
        public bool CanForgetCard { get { return CanEditCard && !UsbPresent && !LanEnabled; } }
        public string CardsHint { get { return Cards.Count == 0 ? "Подключите USB или добавьте карту на странице AoIP / LAN" :
            "Карт: " + Cards.Count + " · работают: " + RunningCards + ". Выбор меняет только показ микшера."; } }
        public string EndpointHint { get { return "Windows · Rey Audio " + ShortIdentity + " · входы и выходы 1/2, 3/4, 5/6, 7/8 + 1–8. Для приложения выберите нужную пару в настройках звука."; } }
        public ObservableCollection<LogRow> Events { get { return Log; } }
        public Choice[] Rates { get; private set; }
        public Choice[] Bits { get; private set; }
        public Choice[] Depths { get; private set; }
        public Choice[] Blocks { get; private set; }
        public string Page = "mixer";
        public bool MixerOutputs;
        public MixerChannel[] Inputs { get; private set; }
        public MixerChannel[] Outputs { get; private set; }
        public MixerChannel Master { get; private set; }
        public MixerChannel[] VisibleChannels { get { return MixerOutputs ? Outputs : Inputs; } }
        public IEnumerable<MixerChannel> AllChannels { get { foreach(var c in Inputs) yield return c; foreach(var c in Outputs) yield return c; yield return Master; } }
        public Visibility MixerVisibility { get { return Page == "mixer" ? Visibility.Visible : Visibility.Collapsed; } }
        public string MixerSourceLabel { get { return MixerOutputs ? "Воспроизведение · Windows → Pi" : "Захват · Pi → Windows"; } }
        public string MixerFormat { get { return ActiveRate > 0 ? (ActiveRate / 1000.0).ToString("0.#") + " кГц · PCM" + ActiveBits + " · " + RouteLabel : "8 входов / 8 выходов"; } }
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
            Cards = new ObservableCollection<AudioCard>(); SelectedDevice = "";
            Inputs = new MixerChannel[8]; Outputs = new MixerChannel[8]; Master = new MixerChannel(2, 0);
            for(int i = 0; i < 8; ++i) { Inputs[i] = new MixerChannel(0, i); Outputs[i] = new MixerChannel(1, i); }
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
        public string PageTitle { get { return Page == "mixer" ? "Микшер" : Page == "usb" ? "USB" : Page == "lan" ? "AoIP / LAN" : "Диагностика"; } }
        public string PageSubtitle { get { return Page == "mixer" ? "Уровни и управление восемью каналами в каждом направлении" : Page == "usb" ? "Прямое подключение Raspberry Pi 5 к компьютеру" : Page == "lan" ? "Аудио через встроенный Ethernet-порт" : "Состояние службы, подключения и события"; } }
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
                if (!DriverPresent && !DigitalTest) return "Установите Rey Audio Driver через MSI/EXE. Настройки и микшер доступны здесь; уровни появятся при работающем аудиопотоке.";
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
            string selected = String(data, "selected_device");
            if (selected != SelectedDevice) {
                loadFields = true;
                foreach (var channel in AllChannels) channel.Dirty = false;
            }
            if (data.ContainsKey("devices")) {
                var ids = new HashSet<string>(); RunningCards = 0;
                foreach(Dictionary<string, object> row in (IEnumerable)data["devices"]) {
                    string id = String(row, "id"); ids.Add(id); AudioCard card = null;
                    foreach(var existing in Cards) if (existing.Id == id) card = existing;
                    if (card == null) { card = new AudioCard(id); Cards.Add(card); }
                    card.Update(row);
                    string state = String(row, "state"); if (state == "streaming" || state == "digital_test") ++RunningCards;
                }
                for (int i = Cards.Count - 1; i >= 0; --i) if (!ids.Contains(Cards[i].Id)) Cards.RemoveAt(i);
            }
            SelectedDevice = selected;
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
