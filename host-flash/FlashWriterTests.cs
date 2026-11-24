// SPDX-License-Identifier: GPL-3.0-or-later
using System.Buffers.Binary;
using Pad8Lab.Core.Hid;
using Pad8Lab.Core.Protocol;

internal static class FlashWriterTests
{
    private static void Require(bool condition,string message)
    { if(!condition) throw new IOException("SELF-TEST: "+message); }
    private static void Fails(Action action)
    { try { action(); } catch(IOException) { return; } throw new Exception("Expected writer refusal/failure"); }
    private sealed class Nor(byte[] original):IChannel
    {
        public byte[] Flash=(byte[])original.Clone();
        public bool Saved,WrongModel,SilentFailure;
        public int Writes,StopAfter=int.MaxValue;
        public void Dispose() {}
        public byte[]? Xfer(byte[] q,double timeoutSeconds=0.5,Func<byte[],bool>? accept=null)
        {
            int command=q[1],a=BinaryPrimitives.ReadInt32LittleEndian(q.AsSpan(13,4));
            int n=BinaryPrimitives.ReadUInt16LittleEndian(q.AsSpan(5,2));
            int parameter=BinaryPrimitives.ReadInt32LittleEndian(q.AsSpan(9,4));
            var response=new byte[64];response[1]=5;response[4]=q[1];q.AsSpan(13,4).CopyTo(response.AsSpan(14));
            switch(command)
            {
                case 8:response[6]=2;response[18]=(byte)(WrongModel?0x1c:0x1b);response[19]=0x30;break;
                case 5:
                    Require(a>=0 && n<=46 && a<=Flash.Length-n,"read bounds");
                    Flash.AsSpan(a,n).CopyTo(response.AsSpan(18));break;
                case 3:case 4:
                    Require(Saved,"write before durable snapshot");
                    int length=command==4?parameter:n;
                    bool app=a>=FlashImage.Base && a+length<=FlashImage.LegacyEnd;
                    bool ext=a>=FlashImage.LegacyEnd && a+length<=FlashImage.End;
                    bool bond=a>=FlashImage.BondBase && a+length<=FlashImage.BondEnd;
                    Require(length>0 && (app||ext||bond),"protected boundary crossed");
                    if(++Writes>=StopAfter) throw new IOException("Simulated loss of USB/power");
                    if(command==4)
                    {
                        Require((a&4095)==0 && (length&4095)==0,"unaligned erase");
                        Array.Fill(Flash,(byte)255,a,length);
                    }
                    else
                    {
                        Require(n<=46 && BootProtocol.Crc16(q.AsSpan(17,n))==
                            BinaryPrimitives.ReadUInt16LittleEndian(q.AsSpan(7,2)),"program CRC");
                        Require(parameter==(app?FlashImage.StockEnd-FlashImage.Base:
                            ext?FlashImage.End-FlashImage.LegacyEnd:FlashImage.BondEnd-FlashImage.BondBase),"program parameter");
                        if(!SilentFailure) for(int i=0;i<n;i++)
                        { Require((Flash[a+i]&q[17+i])==q[17+i],"NOR bit set without erase");Flash[a+i]&=q[17+i]; }
                    }
                    break;
                default:throw new IOException("Unexpected command, including unprotect or restart");
            }
            Require(accept is null || accept(response),"response correlation");return response;
        }
    }
    public static int Run(string backupPath)
    {
        var original=File.ReadAllBytes(backupPath);
        var target=FlashImage.Full(original);var stock=original[FlashImage.Base..FlashImage.End];
        void Run(Nor c,bool restore=false)=>FlashWriter.Write(c,original,restore?stock:target,restore,_=>{},
            b=>{Require(b.AsSpan().SequenceEqual(c.Flash),"preflash snapshot mismatch");c.Saved=true;});
        var device=new Nor(original);Run(device);
        Require(device.Flash.AsSpan(FlashImage.Base,target.Length).SequenceEqual(target),"full install mismatch");
        int count=device.Writes;Run(device);Require(device.Writes==count,"idempotent install rewrote flash");
        device.Flash[FlashImage.BondBase]=0;Run(device,true);
        Require(device.Flash.AsSpan().SequenceEqual(original),"stock restore not byte-exact");
        foreach(int cut in new[]{1,2,17,350,count-2})
        {
            var c=new Nor(original){StopAfter=cut};Fails(()=>Run(c));
            c.StopAfter=int.MaxValue;Run(c,true);
            Require(c.Flash.AsSpan().SequenceEqual(original),"interrupted image could not restore");
        }
        var silent=new Nor(original){SilentFailure=true};Fails(()=>Run(silent));
        silent.SilentFailure=false;Run(silent,true);
        Require(silent.Flash.AsSpan().SequenceEqual(original),"silent write failure recovery");
        foreach(int address in new[]{0,FlashImage.End,0x3f018,0x40000,0x76000})
        {
            var c=new Nor(original);c.Flash[address]^=1;Fails(()=>Run(c));Require(c.Writes==0,"protected mismatch wrote");
        }
        var wrong=new Nor(original){WrongModel=true};Fails(()=>Run(wrong));Require(wrong.Writes==0,"wrong model wrote");
        // A different owner's factory bytes are valid with that owner's backup.
        var otherBackup=(byte[])original.Clone();otherBackup[0x76000]^=1;
        var other=new Nor(otherBackup);
        var otherImage=FlashImage.Full(otherBackup);
        Require(otherImage.AsSpan().SequenceEqual(target),"patch depends on factory data");
        FlashWriter.Write(other,otherBackup,otherImage,false,_=>{},_=>other.Saved=true);
        Require(other.Flash[0x76000]==otherBackup[0x76000],"other owner's factory byte changed");
        FlashWriter.Write(other,otherBackup,otherBackup[FlashImage.Base..FlashImage.End],true,_=>{},_=>other.Saved=true);
        Require(other.Flash.AsSpan().SequenceEqual(otherBackup),"other owner's restore mismatch");
        var foreign=new Nor(original);
        foreign.Flash[0x76000]^=1;Fails(()=>Run(foreign,true));
        Require(foreign.Writes==0,"restore accepted another unit's factory data");
        var unknown=new Nor(original);unknown.Flash[0x6100]^=1;
        Fails(()=>Run(unknown));Require(unknown.Writes==0,"unknown firmware accepted");
        var nosave=new Nor(original);
        Fails(()=>FlashWriter.Write(nosave,original,target,false,_=>{},_=>throw new IOException("disk full")));
        Require(nosave.Writes==0,"snapshot failure wrote");
        var bad=(byte[])target.Clone();bad[0]^=1;
        Fails(()=>FlashWriter.Write(nosave,original,bad,false,_=>{},_=>nosave.Saved=true));
        Require(nosave.Writes==0,"tampered image wrote");
        Console.WriteLine("PASS: full image install/byte-exact restore, five interruption points, silent programming failure, boundary/model/hash guards, idempotence and durable-snapshot ordering. No real device accessed.");
        return 0;
    }
}
