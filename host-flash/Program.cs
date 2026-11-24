// SPDX-License-Identifier: GPL-3.0-or-later
using System.Buffers.Binary;
using Pad8Lab.Core.Hid;
using Pad8Lab.Core.Protocol;

try
{
    if(args is ["self-test",var testBackup]) return FlashWriterTests.Run(testBackup);
    if(args is ["build-image",var backupPath,var outputPath])
    {
        var image=FlashImage.Full(File.ReadAllBytes(backupPath));
        using var output=new FileStream(outputPath,FileMode.CreateNew,FileAccess.Write,FileShare.None);
        output.Write(image);output.Flush(true);
        Console.WriteLine($"Built {image.Length} bytes. SHA256 {FlashImage.Hash(image)}");
        return 0;
    }
    if (args is not (["plan", _, _] or ["install", _, _, "--approved-sha256", _] or
        ["restore", _, "--approved-backup-sha256", _]))
    {
        Console.Error.WriteLine("ControllerFlash self-test <backup> | build-image <backup> <new.bin> | plan <backup> <full.bin> | install <backup> <full.bin> --approved-sha256 <hash> | restore <backup> --approved-backup-sha256 <hash>");
        return 2;
    }
    var original = File.ReadAllBytes(args[1]);
    FlashImage.CheckBackup(original);
    bool restoring = args[0] == "restore";
    var target = restoring ? original[FlashImage.Base..FlashImage.End] : File.ReadAllBytes(args[2]);
    if (!restoring && !target.AsSpan().SequenceEqual(FlashImage.Full(original)))
        throw new IOException("Candidate differs from the compiled full firmware recipe.");
    string targetHash = FlashImage.Hash(target);
    Console.WriteLine($"Operation: {(restoring ? "exact stock restore" : "Wireless 301B v1.09 Switch 2 firmware")}");
    Console.WriteLine($"Application SHA256: {targetHash}");
    Console.WriteLine($"Application 0x6000..0x{FlashImage.End-1:x5}; restore also clears custom bonds 0x3d000..0x3efff. No bootloader/factory writes.");
    if (args[0] == "plan") return 0;
    if (!string.Equals(args[^1], restoring ? FlashImage.Hash(original) : targetHash, StringComparison.OrdinalIgnoreCase))
        throw new IOException("The explicit approval hash does not match this operation.");
    var directory = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[1]))!, "flash-records");
    Directory.CreateDirectory(directory);
    using var exclusive = new FileStream(Path.Combine(directory, "writer.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    string stamp = DateTimeOffset.UtcNow.ToString("yyyyMMddTHHmmssfffZ");
    using var journal = new StreamWriter(new FileStream(Path.Combine(directory, stamp + ".log"), FileMode.CreateNew, FileAccess.Write)) { AutoFlush = true };
    void Log(string line) { Console.WriteLine(line); journal.WriteLine($"{DateTimeOffset.UtcNow:O} {line}"); }
    Log($"BEGIN {(restoring ? "restore" : "full-301b-v1")} authorized hash {args[^1]}");
    var boot = HidTransport.ListDevices().Where(IsBoot).ToArray();
    if (boot.Length == 0)
    {
        var app = HidTransport.ListDevices().Where(d => d.Vendor == 0x2dc8 && d.Product == 0x310a &&
            d.UsagePage == 0xff7a && d.Path.Contains("vid_2dc8&pid_310a&mi_02", StringComparison.OrdinalIgnoreCase)).ToArray();
        if (app.Length != 1) throw new IOException("Connect this controller by USB; no unique direct-cable interface is available.");
        using (var channel = HidTransport.OpenChannel(app[0], 0x81))
        {
            var id = channel.Xfer([0x81, 0x05, 0x00, 0x21, 0x01], 2,
                r => r.Length >= 8 && r[0] == 2 && r[1] == 0x22);
            if (id is null || BinaryPrimitives.ReadUInt16LittleEndian(id.AsSpan(6, 2)) != 0x301b || id[2] != 109)
                throw new IOException("Direct-cable model/version mismatch. Nothing changed.");
            var signature = AppProtocol.Read(channel, 0x10000, 16);
            if (signature is null || !signature.AsSpan().SequenceEqual(original.AsSpan(0x10000, 16)))
                throw new IOException("Application signature mismatch; refusing an ambiguous target.");
            Log("Entering the resident bootloader through the upstream application restart command.");
            AppProtocol.EnterUpdateMode(channel);
        }
        for (int attempt = 0; attempt < 40; attempt++)
        {
            Thread.Sleep(250);
            boot = HidTransport.ListDevices().Where(IsBoot).ToArray();
            if (boot.Length != 0) break;
        }
    }
    if (boot.Length != 1) throw new IOException($"Expected one recovery interface, found {boot.Length}. Nothing erased.");
    using (var channel = HidTransport.OpenChannel(boot[0], 0))
    {
        FlashWriter.Write(channel, original, target, restoring, Log, bytes => {
            using var file = new FileStream(Path.Combine(directory, stamp + "-preflash.bin"), FileMode.CreateNew, FileAccess.Write);
            file.Write(bytes); file.Flush(flushToDisk: true);
        });
        Log("Verified operation complete; requesting normal restart.");
        channel.Xfer(BootProtocol.Request(7), 0.5);
    }
    Log("DONE. Firmware readback verified. Check operation on the owner's console.");
    return 0;
}
catch (Exception e)
{
    Console.Error.WriteLine(e.Message);
    return 1;
}

static bool IsBoot(DeviceInfo d) => d.Vendor == 0x2dc8 && d.Product == 0x3208 &&
    d.Path.Contains("vid_2dc8&pid_3208", StringComparison.OrdinalIgnoreCase);
