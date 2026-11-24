// SPDX-License-Identifier: GPL-3.0-or-later
// Boot command stream follows Pad8Lab Ops/Plan.cs at 14b55c42565c85a90287799e399adaa7cc3204a9.
using Pad8Lab.Core.Hid;
using Pad8Lab.Core.Protocol;
internal static class FlashWriter
{
    private static void Send(IChannel channel,byte[] request,double timeout,int tries)
    {
        bool Accept(byte[] a)=>a.Length>=18 && a[0]==0 && a[1]==5 && a[2]==0 &&
            a[4]==request[1] && a.AsSpan(14,4).SequenceEqual(request.AsSpan(13,4));
        for(int i=0;i<tries;i++)
        {
            var reply=channel.Xfer(request,timeout,Accept);
            if(reply is not null && Accept(reply)) return;
        }
        throw new IOException($"Bootloader command {request[1]} was not acknowledged. Keep recovery mode connected; use restore.");
    }
    public static void Write(IChannel channel,byte[] original,byte[] target,bool restoring,
        Action<string> log,Action<byte[]> savePreflash)
    {
        FlashImage.CheckBackup(original);
        if(target.Length!=FlashImage.End-FlashImage.Base ||
           !(restoring?target.AsSpan().SequenceEqual(original.AsSpan(FlashImage.Base,target.Length)):
             target.AsSpan().SequenceEqual(FlashImage.Full(original))))
            throw new IOException("Image is neither the compiled full integration nor exact stock restore.");
        if(BootProtocol.Identify(channel)!=BootProtocol.ControllerId)
            throw new IOException("Recovery device is not the Wireless 301B controller.");
        log("Reading all flash before any erase.");
        var live=ReadAll(channel,log);
        if(!FlashImage.SameProtected(live,original))
            throw new IOException("Bootloader, second bank, update records or factory bytes differ. Nothing erased.");
        if(FlashImage.PendingUpdate(live)) throw new IOException("Pending vendor update. Nothing erased.");
        bool stock=live.AsSpan(FlashImage.Base,target.Length).SequenceEqual(original.AsSpan(FlashImage.Base,target.Length));
        bool installed=live.AsSpan(FlashImage.Base,target.Length).SequenceEqual(FlashImage.Full(original));
        if(!restoring && !stock && !installed && !FlashImage.KnownPrevious(live,original))
            throw new IOException("Unknown current application. Restore the verified original first.");
        if(!restoring && stock && !live.AsSpan(FlashImage.BondBase,FlashImage.BondEnd-FlashImage.BondBase)
            .SequenceEqual(original.AsSpan(FlashImage.BondBase,FlashImage.BondEnd-FlashImage.BondBase)))
            throw new IOException("Reserved bond sectors are not erased. Nothing changed.");
        savePreflash(live); // Must be durably stored before the first erase.
        var expected=(byte[])live.Clone();target.CopyTo(expected,FlashImage.Base);
        if(restoring) original.AsSpan(FlashImage.BondBase,FlashImage.BondEnd-FlashImage.BondBase)
            .CopyTo(expected.AsSpan(FlashImage.BondBase));
        if(live.AsSpan().SequenceEqual(expected)) { log("Target already present; no writes needed.");return; }
        // Prepare extension before changing the application. The first bank uses
        // upstream's stock-length parameter; the extension uses its run length.
        WriteRange(channel,live,expected,FlashImage.LegacyEnd,FlashImage.End,
                   FlashImage.End-FlashImage.LegacyEnd,log);
        WriteRange(channel,live,expected,FlashImage.Base,FlashImage.LegacyEnd,
                   FlashImage.StockEnd-FlashImage.Base,log);
        if(restoring) WriteRange(channel,live,expected,FlashImage.BondBase,FlashImage.BondEnd,
                                 FlashImage.BondEnd-FlashImage.BondBase,log);
        log("Reading back the full device, including every protected byte.");
        var after=ReadAll(channel,log);
        if(!after.AsSpan().SequenceEqual(expected))
            throw new IOException("Full readback mismatch. No restart sent; keep recovery connected for stock restoration.");
        log($"VERIFIED application SHA256 {FlashImage.Hash(target)}; bootloader, bank B, factory and calibration unchanged.");
    }
    private static void WriteRange(IChannel c,byte[] before,byte[] after,int lo,int hi,int parameter,Action<string> log)
    {
        bool application=lo==FlashImage.Base && hi==FlashImage.LegacyEnd;
        bool extension=lo==FlashImage.LegacyEnd && hi==FlashImage.End;
        bool bonds=lo==FlashImage.BondBase && hi==FlashImage.BondEnd;
        if(!(application||extension||bonds) || (lo&4095)!=0 || (hi&4095)!=0)
            throw new IOException("Internal write range is outside reviewed application/bond sectors.");
        if(before.AsSpan(lo,hi-lo).SequenceEqual(after.AsSpan(lo,hi-lo))) return;
        log($"Erasing 0x{lo:x5}..0x{hi-1:x5}.");
        Send(c,BootProtocol.Request(4,lo,hi-lo),15,1);
        // Preserve original stock chunk boundaries, including its final short chunk.
        int firstEnd=application?FlashImage.StockEnd:hi;
        Program(lo,firstEnd);if(firstEnd<hi) Program(firstEnd,hi);
        var check=BootProtocol.Read(c,lo,hi-lo);
        if(!check.AsSpan().SequenceEqual(after.AsSpan(lo,hi-lo)))
            throw new IOException($"Range readback mismatch at 0x{lo:x5}. No restart sent.");
        void Program(int start,int end)
        {
            for(int a=start;a<end;a+=BootProtocol.Chunk)
            {
                int count=Math.Min(BootProtocol.Chunk,end-a);var bytes=after[a..(a+count)];
                if(bytes.Any(b=>b!=255)) Send(c,BootProtocol.Request(3,a,parameter,bytes),1,3);
                if((a-start)%(BootProtocol.Chunk*512)==0) log($"Programming 0x{a:x5}.");
            }
        }
    }
    private static byte[] ReadAll(IChannel c,Action<string> log)=>BootProtocol.Read(c,0,FlashImage.Size,
        (done,total)=>{if(done%(BootProtocol.Chunk*4096)==0) log($"Reading {done*100/total}%.");});
}
