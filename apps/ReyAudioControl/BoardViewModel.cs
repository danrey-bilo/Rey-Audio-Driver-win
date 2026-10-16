using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
namespace ReyAudio {
    internal sealed class BoardRoute : INotifyPropertyChanged {
        public event PropertyChangedEventHandler PropertyChanged;
        public int Channel { get; private set; }
        public string Name { get { return "CH " + Channel; } }
        int capture, playback;
        public int Capture { get { return capture; } set { capture = value; Notify(); } }
        public int Playback { get { return playback; } set { playback = value; Notify(); } }
        public BoardRoute(int channel) { Channel = channel; }
        void Notify() { if (PropertyChanged != null) PropertyChanged(this, new PropertyChangedEventArgs("")); }
    }
    internal sealed partial class ViewModel {
        readonly Choice[] boardModes = {new Choice(0,"Аудиокарта"),new Choice(1,"Комбинированный")};
        readonly Choice[] captureRoutes = {new Choice(0,"До эффектов"),new Choice(1,"После эффектов")};
        readonly Choice[] playbackRoutes = {new Choice(0,"Прямо на ЦАП"),new Choice(1,"Через эффекты")};
        public Choice[] BoardModes { get { return boardModes; } }
        public Choice[] CaptureRoutes { get { return captureRoutes; } }
        public Choice[] PlaybackRoutes { get { return playbackRoutes; } }
        readonly BoardRoute[] boardRoutes = Enumerable.Range(1,8).Select(i=>new BoardRoute(i)).ToArray();
        public BoardRoute[] BoardRoutes { get { return boardRoutes; } }
        public string ServiceBoardMode = "soundcard";
        int boardMode, appliedBoardMode = -1;
        int[] appliedCapture, appliedPlayback;
        public int BoardMode { get { return boardMode; } set { boardMode=value; if(value==1 && UsbRate>96000) UsbRate=96000; Notify(); } }
        public bool BoardAvailable, BoardBusy;
        public string BoardStatus { get; set; }
        public bool BoardDirty { get { return appliedBoardMode != BoardMode || appliedCapture == null || BoardRoutes.Where((r,i)=>r.Capture!=appliedCapture[i] || r.Playback!=appliedPlayback[i]).Any(); } }
        public bool CanApplyBoard { get { return BoardAvailable && CanApplyUsb && !BoardBusy; } }
        public bool CanEditBoardRoutes { get { return CanApplyBoard && BoardMode==1; } }
        public void UpdateBoard(Dictionary<string, object> state) {
            bool load = !BoardAvailable || !BoardDirty;
            var operation=(Dictionary<string, object>)state["operation"];
            var prefs=(Dictionary<string, object>)operation["preferences"];
            int mode=String(prefs,"usb_mode")=="combined" ? 1 : 0;
            var rows=((IEnumerable)prefs["channels"]).Cast<Dictionary<string,object>>().ToArray();
            if(rows.Length!=8) throw new ArgumentException("Плата вернула неверное число каналов.");
            appliedBoardMode=mode;appliedCapture=rows.Select(r=>String(r,"capture")=="post"?1:0).ToArray();
            appliedPlayback=rows.Select(r=>String(r,"playback")=="effects"?1:0).ToArray();
            if(load) {
                BoardMode=mode;
                for(int i=0;i<8;++i){BoardRoutes[i].Capture=appliedCapture[i];BoardRoutes[i].Playback=appliedPlayback[i];}
            }
            BoardAvailable=true;
            var usb=(Dictionary<string,object>)operation["usb"];
            BoardStatus=Bool(usb,"connected") ? "Плата подключена. Управление режимом — с этого ПК."
                : "USB-аудиотранспорт платы недоступен; сейчас работает автономный процессор эффектов.";
            if(usb.ContainsKey("error") && usb["error"]!=null) BoardStatus += " " + Convert.ToString(usb["error"]);
            Notify();
        }
    }
}
