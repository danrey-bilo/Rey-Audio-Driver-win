using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
namespace ReyAudio {
    internal sealed class ServiceClient {
        private readonly SemaphoreSlim gate = new SemaphoreSlim(1, 1);
        public async Task<Dictionary<string, object>> Send(string command) {
            await gate.WaitAsync();
            try {
                using (var pipe = new NamedPipeClientStream(".", "ReyAudio.Control.v1", PipeDirection.InOut, PipeOptions.Asynchronous))
                using (var timeout = new CancellationTokenSource(4000))
                using (timeout.Token.Register(() => pipe.Dispose())) {
                    await pipe.ConnectAsync(750, timeout.Token);
                    pipe.ReadMode = PipeTransmissionMode.Message;
                    var request = Encoding.ASCII.GetBytes(command);
                    if (request.Length < 1 || request.Length >= 1024) throw new IOException("Недопустимая команда управления.");
                    await pipe.WriteAsync(request, 0, request.Length, timeout.Token);
                    var buffer = new byte[32768];
                    int used = 0;
                    do {
                        int count = await pipe.ReadAsync(buffer, used, buffer.Length - used, timeout.Token);
                        if (count == 0 || used + count >= buffer.Length) throw new IOException("Неполный ответ службы.");
                        used += count;
                    } while (!pipe.IsMessageComplete);
                    var acknowledgement = Encoding.ASCII.GetBytes("ACK");
                    await pipe.WriteAsync(acknowledgement, 0, acknowledgement.Length, timeout.Token);
                    return new JavaScriptSerializer { MaxJsonLength = 32768 }.Deserialize<Dictionary<string, object>>(Encoding.UTF8.GetString(buffer, 0, used));
                }
            } finally { gate.Release(); }
        }
    }
}
