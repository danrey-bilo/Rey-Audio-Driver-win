using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace ReyAudio.UsbAsioSetup {
    // Only the original signed kernel is installed. The SAR ASIO wrapper,
    // application routing hooks and network filter are not used by Rey.
    internal static class NativeEndpoints {
        internal const string InstanceId = @"ROOT\REYAUDIO_SAR\0000";
        private const string HardwareId = @"SW\{0BCFFA5C-E754-48CF-A783-A64C0DC0BB2C}";
        private static readonly Guid Media = new Guid("4d36e96c-e325-11ce-bfc1-08002be10318");
        private static readonly Guid Audio = new Guid("6994ad04-93ef-11d0-a3cc-00a0c9223196");
        private static readonly Guid Capture = new Guid("65e8773d-8f56-11d0-a3b9-00a0c9223196");
        private static readonly Guid Render = new Guid("65e8773e-8f56-11d0-a3b9-00a0c9223196");
        private static readonly Guid Realtime = new Guid("eb115ffc-10c8-4964-831d-6dcb02e6f23f");
        [StructLayout(LayoutKind.Sequential)] private struct DeviceInfo { internal uint Size; internal Guid Class; internal uint Instance; internal UIntPtr Reserved; }
        [StructLayout(LayoutKind.Sequential)] private struct InterfaceInfo { internal uint Size; internal Guid Class; internal uint Flags; internal UIntPtr Reserved; }
        [DllImport("setupapi.dll", SetLastError=true)] private static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid g, IntPtr parent);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool SetupDiCreateDeviceInfoW(IntPtr list,string name,ref Guid g,string description,IntPtr parent,uint flags,ref DeviceInfo info);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool SetupDiOpenDeviceInfoW(IntPtr list,string id,IntPtr parent,uint flags,ref DeviceInfo info);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr list,ref DeviceInfo info,uint property,out uint type,byte[] data,uint size,out uint required);
        [DllImport("setupapi.dll", SetLastError=true)] private static extern bool SetupDiSetDeviceRegistryPropertyW(IntPtr list,ref DeviceInfo info,uint property,byte[] data,uint size);
        [DllImport("setupapi.dll", SetLastError=true)] private static extern bool SetupDiCallClassInstaller(uint action,IntPtr list,ref DeviceInfo info);
        [DllImport("setupapi.dll", SetLastError=true)] private static extern bool SetupDiDestroyDeviceInfoList(IntPtr list);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr SetupDiGetClassDevsW(ref Guid g,string enumerator,IntPtr parent,uint flags);
        [DllImport("setupapi.dll", SetLastError=true)] private static extern bool SetupDiEnumDeviceInfo(IntPtr list,uint index,ref DeviceInfo info);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool SetupDiGetDeviceInstanceIdW(IntPtr list,ref DeviceInfo info,StringBuilder value,uint size,out uint required);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool SetupDiCreateDeviceInterfaceW(IntPtr list,ref DeviceInfo info,ref Guid g,string reference,uint flags,ref InterfaceInfo item);
        [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr SetupDiCreateDeviceInterfaceRegKeyW(IntPtr list,ref InterfaceInfo item,uint reserved,uint access,IntPtr inf,string section);
        [DllImport("newdev.dll", SetLastError=true)] private static extern bool DiInstallDevice(IntPtr parent,IntPtr list,ref DeviceInfo info,IntPtr driver,uint flags,out bool reboot);
        [DllImport("cfgmgr32.dll")] private static extern uint CM_Get_DevNode_Status(out uint status,out uint problem,uint instance,uint flags);
        private static void Check(bool success,string operation) { if(!success) throw new Win32Exception(Marshal.GetLastWin32Error(), operation); }
        private static DeviceInfo NewInfo() { return new DeviceInfo { Size=(uint)Marshal.SizeOf(typeof(DeviceInfo)) }; }
        private static IntPtr NewList() {
            var media=Media; var list=SetupDiCreateDeviceInfoList(ref media,IntPtr.Zero);
            if(list==new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error());
            return list;
        }
        private static string Property(IntPtr list, ref DeviceInfo info,uint property) {
            uint type,required; var value=new byte[4096];
            Check(SetupDiGetDeviceRegistryPropertyW(list,ref info,property,out type,value,(uint)value.Length,out required),"Read Rey endpoint device identity");
            return Encoding.Unicode.GetString(value,0,(int)required).TrimEnd('\0');
        }
        private static void SetProperty(IntPtr list,ref DeviceInfo info,uint property,string text) {
            var value=Encoding.Unicode.GetBytes(text);
            Check(SetupDiSetDeviceRegistryPropertyW(list,ref info,property,value,(uint)value.Length),"Set Rey endpoint device identity");
        }
        private static void ValidateOwned(IntPtr list,ref DeviceInfo info) {
            if(!string.Equals(Property(list,ref info,1),HardwareId,StringComparison.OrdinalIgnoreCase) ||
                !string.Equals(Property(list,ref info,4),"SynchronousAudioRouter",StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Идентификатор Rey занят другим драйвером; замена отменена.");
        }
        private static void CheckOtherInstallations() {
            var media=Media;var list=SetupDiGetClassDevsW(ref media,null,IntPtr.Zero,2);
            if(list==new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error());
            try {
                for(uint index=0;;index++) {
                    var info=NewInfo();
                    if(!SetupDiEnumDeviceInfo(list,index,ref info)) {
                        int error=Marshal.GetLastWin32Error();if(error==259)break;
                        throw new Win32Exception(error,"Enumerate endpoint devices");
                    }
                    uint type,required;var data=new byte[4096];
                    if(!SetupDiGetDeviceRegistryPropertyW(list,ref info,1,out type,data,(uint)data.Length,out required))continue;
                    var hardware=Encoding.Unicode.GetString(data,0,(int)required).TrimEnd('\0');
                    if(!string.Equals(hardware,HardwareId,StringComparison.OrdinalIgnoreCase))continue;
                    var id=new StringBuilder(1024);
                    Check(SetupDiGetDeviceInstanceIdW(list,ref info,id,(uint)id.Capacity,out required),"Read endpoint instance");
                    if(!string.Equals(id.ToString(),InstanceId,StringComparison.OrdinalIgnoreCase))
                        throw new InvalidOperationException("Уже установлен другой Synchronous Audio Router. Rey не будет заменять его или перехватывать его каналы.");
                }
            } finally {SetupDiDestroyDeviceInfoList(list);}
        }
        private static void Prepare(IntPtr list, ref DeviceInfo info) {
            // SAR 0.13.1 writes the inbox-driver MSEP store. Windows 11 requires
            // the third-party EP store. Register the dormant references before
            // the service enables them, keeping the signed INF/SYS untouched.
            foreach(int rate in new[] {44100,48000,88200,96000,176400,192000})
            for(int pair=0;pair<4;pair++)
            for(int direction=0;direction<2;direction++) {
                string id=string.Format("rey-{0}-{1}-{2}",direction==0?"input":"output",pair*2+1,pair*2+2);
                string reference=string.Format("{0}_2_{1}_4",id,rate);
                foreach(var category in new[] {Audio,direction==0?Capture:Render,Realtime}) {
                    var guid=category;
                    var item=new InterfaceInfo {Size=(uint)Marshal.SizeOf(typeof(InterfaceInfo))};
                    Check(SetupDiCreateDeviceInterfaceW(list,ref info,ref guid,reference,0,ref item),"Prepare Rey stereo interface");
                    var raw=SetupDiCreateDeviceInterfaceRegKeyW(list,ref item,0,0x2001f,IntPtr.Zero,null);
                    if(raw==new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error(),"Prepare Rey stereo properties");
                    using(var handle=new SafeRegistryHandle(raw,true))
                    using(var properties=RegistryKey.FromHandle(handle))
                    using(var endpoint=properties.CreateSubKey(@"EP\0")) {
                        endpoint.SetValue("{1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E},2","{00000000-0000-0000-0000-000000000000}",RegistryValueKind.String);
                        endpoint.SetValue("{1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E},7",1,RegistryValueKind.DWord);
                        endpoint.SetValue("{F4B15B6F-8C3F-48B6-A115-42FDE19EF05B},0",id,RegistryValueKind.String);
                    }
                }
            }
        }
        internal static bool Ensure(string directory,Action<string> record) {
            CheckOtherInstallations();
            string inf=Path.Combine(directory,"SynchronousAudioRouter.inf");
            var start=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"pnputil.exe"),"/add-driver \""+inf+"\"") {
                UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,
                StandardOutputEncoding=Encoding.Default,StandardErrorEncoding=Encoding.Default
            };
            using(var process=Process.Start(start)) {
                var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();
                if(!process.WaitForExit(15000)) {process.Kill();throw new TimeoutException("Тайм-аут установки Windows-аудиодрайвера.");}
                record("ENDPOINT_PACKAGE: "+output.Result.Trim()+" "+error.Result.Trim());
                if(process.ExitCode!=0 && process.ExitCode!=3010) throw new InvalidOperationException("Windows отклонила установку готового аудиодрайвера: "+process.ExitCode+". "+output.Result);
            }
            var list=NewList();var info=NewInfo();bool created=false,okay=false;
            try {
                if(SetupDiOpenDeviceInfoW(list,InstanceId,IntPtr.Zero,0,ref info)) ValidateOwned(list,ref info);
                else {
                    int error=Marshal.GetLastWin32Error();
                    if(error!=433 && error!= unchecked((int)0xe000020b)) throw new Win32Exception(error,"Open Rey endpoint device");
                    var media=Media;info=NewInfo();
                    Check(SetupDiCreateDeviceInfoW(list,InstanceId,ref media,"Rey Audio",IntPtr.Zero,0,ref info),"Create Rey endpoint device");
                    SetProperty(list,ref info,1,HardwareId+"\0\0");
                    Check(SetupDiCallClassInstaller(0x19,list,ref info),"Register Rey endpoint device");created=true;
                }
                bool reboot;
                Check(DiInstallDevice(IntPtr.Zero,list,ref info,IntPtr.Zero,0,out reboot),"Start signed endpoint driver");
                uint state=0,problem=0,cm=0;
                for(int n=0;n<30;n++) { cm=CM_Get_DevNode_Status(out state,out problem,info.Instance,0);if(cm==0 && (state&8)!=0)break;System.Threading.Thread.Sleep(100); }
                if(cm!=0 || problem!=0 || (state&8)==0) throw new InvalidOperationException("Готовый аудиодрайвер не запустился: код "+problem+". Изменения защиты Windows не выполнялись.");
                SetProperty(list,ref info,12,"Rey Audio\0");
                Prepare(list,ref info);okay=true;
                record("ENDPOINT_DRIVER_OK: original signed SAR 0.13.1; 4 stereo pairs in both directions; six rates prepared; reboot_required="+reboot);
                return created;
            } finally {
                if(created && !okay) Check(SetupDiCallClassInstaller(5,list,ref info),"Roll back owned Rey endpoint device");
                SetupDiDestroyDeviceInfoList(list);
            }
        }
        internal static void RemoveOwned(Action<string> record) {
            var list=NewList();var info=NewInfo();
            try {
                if(!SetupDiOpenDeviceInfoW(list,InstanceId,IntPtr.Zero,0,ref info)) {
                    int error=Marshal.GetLastWin32Error();
                    if(error==433 || error==unchecked((int)0xe000020b))return;
                    throw new Win32Exception(error,"Open owned Rey endpoint device");
                }
                ValidateOwned(list,ref info);
                Check(SetupDiCallClassInstaller(5,list,ref info),"Remove owned Rey endpoint device");
                record("ENDPOINT_DEVICE_REMOVED: "+InstanceId+"; shared signed driver-store package retained");
            } finally {SetupDiDestroyDeviceInfoList(list);}
        }
    }
}
