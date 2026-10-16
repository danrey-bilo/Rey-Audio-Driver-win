using System;
namespace ReyAudio {
    internal static class MixerPairChecks {
        static void Check(bool okay, string message) { if(!okay) throw new InvalidOperationException(message); }
        public static int Main() {
            var view = new ViewModel();
            foreach(var c in view.AllChannels) c.Changed = edited => { if(edited.Pair != null) edited.Pair.Mirror(edited); };
            Check(view.InputPairs.Length == 4 && view.OutputPairs.Length == 4, "Both directions must expose four pairs");
            foreach(var pair in view.AllPairs) {
                pair.Left.Gain = -3; pair.Right.Gain = -9;
                pair.Left.Solo = true;
                pair.Linked = true;
                Check(pair.Left.Gain == pair.Right.Gain && pair.Right.Gain == -3 && pair.Right.Solo, "Link must copy the first channel's controls");
                Check(pair.Command == "LINK " + pair.Direction + ":" + pair.Index + " 1 5700 0 1 0 apply", "Link command must match the service protocol");
                pair.Right.Gain = -6; pair.Right.Invert = true;
                Check(pair.Left.Gain == -6 && pair.Left.Invert, "Editing either side must update both linked controls");
                Check(pair.Left.Side == "L" && pair.Right.Side == "R", "Audio sides must retain their identity");
                pair.Linked = false; pair.Left.Gain = -1;
                Check(pair.Right.Gain == -6, "Unlinked faders must remain independent");
                long revision = pair.Revision;
                pair.Dirty = false; pair.Update(true);
                Check(pair.Linked && pair.Revision == revision && !pair.Dirty, "Server polling must not send edits or change revisions");
                pair.Linked = false;
            }
            var untouched = view.OutputPairs[3];
            var target = view.InputPairs[0];
            target.Linked = true; target.Left.Gain = -12;
            Check(untouched.Left.Gain == -1 && untouched.Right.Gain == -6, "Links must not leak across directions or pairs");
            Console.WriteLine("MIXER_PAIR PASS: eight independent pairs, linked and mono edits, service payload and polling");
            return 0;
        }
    }
}
