using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
namespace ReyAudio {
    internal sealed class MixerChannel : INotifyPropertyChanged {
        public event PropertyChangedEventHandler PropertyChanged;
        public Action<MixerChannel> Changed;
        public readonly int Direction, Index;
        public bool Dirty;
        public long Revision;
        double gain, peak;
        bool mute, solo, invert;
        long clips;
        public MixerChannel(int direction, int index) { Direction = direction; Index = index; }
        public string Name { get { return Direction == 2 ? "Все выходы" : (Direction == 0 ? "Вход " : "Выход ") + (Index + 1).ToString("00"); } }
        public string Source { get { return Direction == 0 ? "Pi → Windows" : "Windows → Pi"; } }
        void Notify() { if (PropertyChanged != null) PropertyChanged(this, new PropertyChangedEventArgs("")); }
        void Edit() { Dirty = true; ++Revision; Notify(); if (Changed != null) Changed(this); }
        public double Gain { get { return gain; } set { double v = Math.Round(value, 2); if (gain != v) { gain = v; Edit(); } } }
        public bool Mute { get { return mute; } set { if (mute != value) { mute = value; Edit(); } } }
        public bool Solo { get { return solo; } set { if (solo != value) { solo = value; Edit(); } } }
        public bool Invert { get { return invert; } set { if (invert != value) { invert = value; Edit(); } } }
        public string GainLabel { get { return gain.ToString("+0.0;-0.0;0.0", CultureInfo.CurrentCulture) + " dB"; } }
        public string PeakLabel { get { return peak <= 0 ? "−∞" : (20 * Math.Log10(peak)).ToString("0.0", CultureInfo.CurrentCulture); } }
        public double MeterHeight { get { return peak <= 0 ? 0 : Math.Max(0, Math.Min(190, (20 * Math.Log10(peak) + 60) / 60 * 190)); } }
        public string ClipColor { get { return clips > 0 ? "#EF5C62" : "#DCE6EE"; } }
        public string ClipHint { get { return "Обрезанных отсчётов: " + clips.ToString("N0"); } }
        public string Command {
            get { return Direction == 2 ? "MASTER " + ((int)Math.Round(gain * 100) + 6000) + " " + (mute ? 1 : 0) :
                "MIX " + Direction + ":" + Index + " " + ((int)Math.Round(gain * 100) + 6000) + " " + (mute ? 1 : 0) + " " + (solo ? 1 : 0) + " " + (invert ? 1 : 0) + " apply"; }
        }
        public void Update(Dictionary<string, object> data, bool meters) {
            if (!Dirty) { gain = ViewModel.Int(data, "gain_cdb") / 100.0; mute = ViewModel.Bool(data, "mute"); solo = ViewModel.Bool(data, "solo"); invert = ViewModel.Bool(data, "invert"); }
            peak = meters && data.ContainsKey("peak") ? Convert.ToDouble(data["peak"], CultureInfo.InvariantCulture) : 0;
            clips = data.ContainsKey("clips") ? Convert.ToInt64(data["clips"]) : 0;
            Notify();
        }
    }
}
