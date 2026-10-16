using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Web.Script.Serialization;
namespace ReyAudio {
    internal static class BoardChecks {
        static int checks;
        static void Check(bool ok) { if(!ok) throw new Exception("Board control contract failed: "+checks);++checks; }
        static void Reject(Action operation) { try {operation();} catch(ArgumentException){++checks;return;}throw new Exception("Invalid PC board command accepted"); }
        [STAThread] public static int Main() {
            var view=new ViewModel();
            Check(view.BoardModes.Length==2 && view.BoardModes[0].Value==0 && view.BoardModes[1].Value==1);
            Check(object.ReferenceEquals(view.BoardModes,view.BoardModes));
            view.UsbRate=192000;view.BoardMode=1;
            Check(view.UsbRate==96000 && view.Rates.Length==4 && view.Rates.All(r=>r.Value<=96000));
            Check(object.ReferenceEquals(view.Rates,view.Rates));
            view.BoardMode=0;Check(view.Rates.Length==6);
            var state=new Dictionary<string,object>{{"studio_revision",3},{"operation",new Dictionary<string,object>{{"preferences",new Dictionary<string,object>{{"revision",9}}}}}};
            for(int i=0;i<8;++i){view.BoardRoutes[i].Capture=i%2;view.BoardRoutes[i].Playback=(i+1)%2;}
            var json=new JavaScriptSerializer();
            var body=json.Deserialize<Dictionary<string,object>>(json.Serialize(BoardControl.Request(state,1,96000,128,view.BoardRoutes)));
            Check((string)body["mode"]=="combined" && (int)body["expected_revision"]==3 && (int)body["expected_operation_revision"]==9);
            var channels=((IEnumerable)body["channels"]).Cast<Dictionary<string,object>>().ToArray();
            Check(channels.Length==8);
            for(int i=0;i<8;++i)Check((string)channels[i]["capture"]==(i%2==0?"pre":"post") && (string)channels[i]["playback"]==(i%2==0?"effects":"direct"));
            Reject(()=>BoardControl.Request(state,2,96000,128,view.BoardRoutes));
            Reject(()=>BoardControl.Request(state,1,192000,128,view.BoardRoutes));
            Reject(()=>BoardControl.Request(state,0,384000,128,view.BoardRoutes));
            Reject(()=>BoardControl.Request(state,0,96000,16,view.BoardRoutes));
            Reject(()=>BoardControl.Request(state,0,96000,128,view.BoardRoutes.Take(7)));
            view.BoardRoutes[0].Capture=2;
            Reject(()=>BoardControl.Request(state,0,96000,128,view.BoardRoutes));
            int reads=0, pauses=0;
            BoardControl.WaitForIdle(()=>new Dictionary<string,object>{{"pending",++reads<3}},()=>++pauses,()=>false);
            Check(reads==3 && pauses==2);
            bool timedOut=false;
            try { BoardControl.WaitForIdle(()=>new Dictionary<string,object>{{"pending",true}},()=>{throw new Exception("Expired wait slept");},()=>true); }
            catch(TimeoutException){timedOut=true;}
            Check(timedOut);
            Console.WriteLine("PC board control: "+checks+" contracts passed; no physical USB claim");return 0;
        }
    }
}
