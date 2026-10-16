using System;
using System.ComponentModel;
using System.Windows;
namespace ReyAudio {
    internal sealed class MixerPair : INotifyPropertyChanged {
        public event PropertyChangedEventHandler PropertyChanged;
        public Action<MixerPair> Changed;
        public readonly int Direction, Index;
        public MixerChannel Left { get; private set; }
        public MixerChannel Right { get; private set; }
        public bool Dirty;
        public long Revision;
        bool linked;
        public MixerPair(MixerChannel left, MixerChannel right) {
            Left = left; Right = right; Direction = left.Direction; Index = left.Index / 2;
            left.Pair = right.Pair = this;
            left.PropertyChanged += (s, e) => Notify();
            right.PropertyChanged += (s, e) => Notify();
        }
        public string Name { get { return Left.Number + "–" + Right.Number; } }
        public string LinkLabel { get { return linked ? "STEREO" : "MONO"; } }
        public bool Linked {
            get { return linked; }
            set {
                if (linked == value) return;
                linked = value; Dirty = true; ++Revision;
                if (linked) Right.CopyControlsFrom(Left);
                Notify(); if (Changed != null) Changed(this);
            }
        }
        public Visibility StereoVisibility { get { return linked ? Visibility.Visible : Visibility.Collapsed; } }
        public Visibility MonoVisibility { get { return linked ? Visibility.Collapsed : Visibility.Visible; } }
        public string Command { get {
            return "LINK " + Direction + ":" + Index + " " + (linked ? 1 : 0) + " " +
                ((int)Math.Round(Left.Gain * 100) + 6000) + " " + (Left.Mute ? 1 : 0) + " " +
                (Left.Solo ? 1 : 0) + " " + (Left.Invert ? 1 : 0) + " apply";
        } }
        public MixerChannel Mirror(MixerChannel edited) {
            if (!linked) return null;
            var other = edited == Left ? Right : Left;
            other.CopyControlsFrom(edited); return other;
        }
        public void Update(bool value) { if (!Dirty) linked = value; Notify(); }
        void Notify() { if (PropertyChanged != null) PropertyChanged(this, new PropertyChangedEventArgs("")); }
    }
}
