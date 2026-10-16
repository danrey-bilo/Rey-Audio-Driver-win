using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Microsoft.Win32;
namespace ReyAudio {
    // USB CDC management only. PCM remains in WinUSB/ASIO, never in this class.
    internal sealed class BoardControl : IDisposable {
        readonly SemaphoreSlim gate = new SemaphoreSlim(1, 1);
        readonly JavaScriptSerializer json = new JavaScriptSerializer { MaxJsonLength = 32768 };
        SerialPort port;
        readonly Stopwatch clock = Stopwatch.StartNew();
        long nextCommand;
        volatile bool disposed;
        public static string[] OwnedPorts() {
            var result = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            using (var usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB")) {
                if (usb == null) return new string[0];
                foreach (var name in usb.GetSubKeyNames()) {
                    if (!name.StartsWith("VID_1D6B&PID_0107", StringComparison.OrdinalIgnoreCase) &&
                        !name.StartsWith("VID_1D6B&PID_0104", StringComparison.OrdinalIgnoreCase)) continue;
                    using (var device = usb.OpenSubKey(name)) {
                        if (device == null) continue;
                        foreach (var instanceName in device.GetSubKeyNames()) using (var instance = device.OpenSubKey(instanceName)) {
                            if (instance == null || !string.Equals(instance.GetValue("Service") as string, "usbser", StringComparison.OrdinalIgnoreCase)) continue;
                            using (var parameters = instance.OpenSubKey("Device Parameters")) {
                                var value = parameters == null ? null : parameters.GetValue("PortName") as string;
                                if (value != null && SerialPort.GetPortNames().Contains(value, StringComparer.OrdinalIgnoreCase)) result.Add(value);
                            }
                        }
                    }
                }
            }
            return result.OrderBy(x => x).ToArray();
        }
        void Open() {
            if (disposed) throw new ObjectDisposedException("BoardControl");
            if (port != null && port.IsOpen) return;
            var ports = OwnedPorts();
            if (ports.Length != 1) throw new IOException(ports.Length == 0
                ? "USB-управление платой недоступно. Требуется Rey Effects CDC на Raspberry Pi."
                : "Найдено несколько портов управления Rey. Подключите одну плату.");
            port = new SerialPort(ports[0], 115200) { ReadTimeout = 200, WriteTimeout = 1000, Encoding = Encoding.UTF8, DtrEnable = true };
            port.Open(); port.DiscardInBuffer();
            var hello = Exchange("hello", new Dictionary<string, object>());
            if (ViewModel.String(hello, "product") != "Rey Effects" || ViewModel.Int(hello, "protocol") != 1)
                throw new IOException("Порт не принадлежит поддерживаемой плате Rey Effects.");
        }
        Dictionary<string, object> Exchange(string op, object payload) {
            string id = Guid.NewGuid().ToString("N");
            string request = json.Serialize(new { version = 1, request_id = id, op = op, payload = payload }) + "\n";
            if (Encoding.UTF8.GetByteCount(request) > 32768) throw new ArgumentException("Слишком большая команда платы.");
            int pause = (int)Math.Max(0, nextCommand - clock.ElapsedMilliseconds);
            if (pause != 0) Thread.Sleep(pause);
            nextCommand = clock.ElapsedMilliseconds + 20;
            port.Write(request);
            var line = new StringBuilder(); var timer = Stopwatch.StartNew();
            while (timer.ElapsedMilliseconds < 3500 && !disposed) {
                int c;
                try { c = port.ReadChar(); } catch (TimeoutException) { continue; }
                if (c < 0) throw new IOException("USB-управление отключено.");
                if (c != '\n') {
                    if (line.Length >= 32768) throw new IOException("Слишком большой ответ платы.");
                    line.Append((char)c); continue;
                }
                var reply = json.Deserialize<Dictionary<string, object>>(line.ToString()); line.Clear();
                if (ViewModel.String(reply, "request_id") != id) continue;
                if (reply.ContainsKey("error")) throw new ArgumentException(Convert.ToString(reply["error"]));
                return (Dictionary<string, object>)reply["result"];
            }
            throw new TimeoutException("Плата не ответила на команду USB.");
        }
        Task<T> Run<T>(Func<T> action) {
            return Task.Run(() => {
                gate.Wait();
                try { return action(); }
                catch { if (port != null) { port.Dispose(); port = null; } throw; }
                finally { gate.Release(); }
            });
        }
        Dictionary<string, object> ReadOnly(string operation) {
            if (operation != "operation_state" && operation != "operation_status")
                throw new ArgumentException("Only state reads may be retried.");
            for (int attempt = 0; ; ++attempt) {
                try { Open(); return Exchange(operation, new Dictionary<string, object>()); }
                catch (Exception error) {
                    if (!(error is IOException) && !(error is TimeoutException)) throw;
                    if (port != null) { port.Dispose(); port = null; }
                    if (attempt == 2 || disposed) throw;
                    Thread.Sleep(150);
                }
            }
        }
        internal static void WaitForIdle(Func<Dictionary<string, object>> status, Action pause, Func<bool> expired) {
            while (ViewModel.Bool(status(), "pending")) {
                if (expired()) throw new TimeoutException("Предыдущее переключение платы ещё не завершилось.");
                pause();
            }
        }
        public Task<Dictionary<string, object>> State() {
            return Run(() => ReadOnly("operation_state"));
        }
        internal static object Request(Dictionary<string, object> state, int mode, int rate, int block, IEnumerable<BoardRoute> routes) {
            if (mode < 0 || mode > 1) throw new ArgumentException("На ПК доступны только аудиокарта и комбинированный режим.");
            if (!(new[] {44100,48000,88200,96000,176400,192000}).Contains(rate) || (mode == 1 && rate > 96000))
                throw new ArgumentException("Частота недоступна для выбранного режима USB.");
            if (!(new[] {32,64,128,256}).Contains(block)) throw new ArgumentException("Недопустимый буфер платы.");
            var rows = routes.ToArray();
            if (rows.Length != 8 || rows.Where((r,i)=>r.Channel!=i+1 || r.Capture<0 || r.Capture>1 || r.Playback<0 || r.Playback>1).Any())
                throw new ArgumentException("Требуется восемь допустимых маршрутов по порядку каналов.");
            var channels = rows.Select(r => new { capture = r.Capture == 0 ? "pre" : "post", playback = r.Playback == 0 ? "direct" : "effects" }).ToArray();
            var operation = (Dictionary<string, object>)state["operation"];
            var preferences = (Dictionary<string, object>)operation["preferences"];
            return new { mode = mode == 0 ? "soundcard" : "combined", sample_rate = rate, block_size = block,
                channels = channels, expected_revision = state["studio_revision"], expected_operation_revision = preferences["revision"] };
        }
        public Task<Dictionary<string, object>> Apply(int mode, int rate, int block, IEnumerable<BoardRoute> routes) {
            // Freeze UI intent before handing it to the worker thread.
            var frozen = routes.Select(r => new BoardRoute(r.Channel) { Capture = r.Capture, Playback = r.Playback }).ToArray();
            return Run(() => {
                var timer = Stopwatch.StartNew();
                // USB/ASIO close can already have triggered an automatic profile
                // transaction. Wait for it before reading revision/admitting ours.
                WaitForIdle(() => ReadOnly("operation_status"), () => Thread.Sleep(100),
                    () => timer.ElapsedMilliseconds >= 90000 || disposed);
                var state = ReadOnly("operation_state");
                var accepted = Exchange("set_operating_mode", Request(state, mode, rate, block, frozen));
                // Never replay a mutation after an ambiguous timeout. Once its
                // ID is acknowledged, reconnect only the readback/status queries.
                string id = ViewModel.String(accepted, "id");
                while (timer.ElapsedMilliseconds < 90000 && !disposed) {
                    var status = ReadOnly("operation_status");
                    if (ViewModel.String(status, "id") == id && !ViewModel.Bool(status, "pending")) {
                        if (status.ContainsKey("error") && status["error"] != null) throw new ArgumentException(Convert.ToString(status["error"]));
                        return ReadOnly("operation_state");
                    }
                    Thread.Sleep(100);
                }
                throw new TimeoutException("Не удалось подтвердить режим платы. Обновите состояние перед повтором.");
            });
        }
        public void Dispose() { disposed = true; if (port != null) port.Dispose(); }
    }
}
