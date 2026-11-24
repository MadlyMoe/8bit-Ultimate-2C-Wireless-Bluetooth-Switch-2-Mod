// SPDX-License-Identifier: GPL-3.0-or-later
using System.Buffers.Binary;
using System.Security.Cryptography;
using Pad8Lab.Core.Hid;
using Pad8Lab.Core.Protocol;

if (args is not (["self-test"] or ["identify"] or ["inventory"] or ["inputs"] or ["backup", _] or ["recovery-check", _] or ["diagnose", _] or ["verify-image", _]))
{
    Console.Error.WriteLine("Usage: ControllerRead self-test | identify | inventory | inputs | backup <new-file.bin> | recovery-check <original.bin> | diagnose <new-private-folder> | verify-image <application.bin>");
    return 2;
}

try
{
    if (args[0] == "self-test") return PairingSnapshot.SelfTest();
    if (args[0] == "inventory")
    {
        var all = HidTransport.ListDevices();
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new {
            capturedUtc = DateTimeOffset.UtcNow, accessibleHidInterfaces = all.Count,
            controllerInterfaces = all.Where(d => d.Vendor == 0x2dc8 || d.Vendor == 0x057e ||
                d.Name.Contains("Controller", StringComparison.OrdinalIgnoreCase) ||
                d.Name.Contains("8BitDo", StringComparison.OrdinalIgnoreCase)).ToArray()
        }, new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }
    if (args[0] == "inputs")
    {
        return InputSnapshot.Print();
    }
    if (args[0] == "recovery-check")
    {
        var original = File.ReadAllBytes(args[1]);
        if (original.Length != 0x80000 ||
            Convert.ToHexString(SHA256.HashData(original.AsSpan(0x6000, 0x12400))) !=
            "DFD443C77F831008AA453EE2C1C9CB6871C36F8947C548E61D4387FBA0A9DA70")
            throw new IOException("Expected a complete backup with the stock Wireless v1.09 application.");
        var candidates = HidTransport.ListDevices().Where(d => d.Vendor == 0x2dc8 &&
            d.Product == 0x3208 && d.Path.Contains("vid_2dc8&pid_3208", StringComparison.OrdinalIgnoreCase)).ToArray();
        if (candidates.Length != 1)
            throw new IOException($"Expected one 3208 bootloader interface, found {candidates.Length}.");
        using var boot = HidTransport.OpenChannel(candidates[0], 0);
        if (BootProtocol.Identify(boot) != BootProtocol.ControllerId)
            throw new IOException("Bootloader did not identify as the 301b controller.");
        Console.WriteLine("Bootloader 2dc8:3208, internal model 301b. Comparing full flash to backup.");
        var live = BootProtocol.Read(boot, 0, original.Length, (done, total) =>
        {
            if (done % (BootProtocol.Chunk * 2048) == 0) Console.WriteLine($"{done * 100 / total}%");
        });
        if (!live.AsSpan().SequenceEqual(original))
        {
            var sector = Enumerable.Range(0, live.Length / 0x1000).Where(n =>
                !live.AsSpan(n * 0x1000, 0x1000).SequenceEqual(original.AsSpan(n * 0x1000, 0x1000)));
            throw new IOException("Backup differs in flash sectors starting at " +
                string.Join(", ", sector.Select(n => $"0x{n * 0x1000:x5}")));
        }
        Console.WriteLine($"Recovery read matched all {live.Length} bytes. SHA256 {Convert.ToHexString(SHA256.HashData(live))}");
        return 0;
    }
    var devices = HidTransport.ListDevices().Where(d => d.Vendor == 0x2dc8 &&
        d.Product == 0x310a && d.UsagePage == 0xff7a &&
        d.Path.Contains("vid_2dc8&pid_310a&mi_02", StringComparison.OrdinalIgnoreCase)).ToArray();
    if (devices.Length != 1)
        throw new IOException($"Expected one accessible USB controller interface, found {devices.Length}.");

    var device = devices[0];
    using var channel = HidTransport.OpenChannel(device, 0x81);
    byte[] identify = [0x81, 0x05, 0x00, 0x21, 0x01];
    var reply = channel.Xfer(identify, 2, r => r.Length >= 8 && r[0] == 2 && r[1] == 0x22)
        ?? throw new IOException("The controller did not return an identify response.");
    var model = BinaryPrimitives.ReadUInt16LittleEndian(reply.AsSpan(6, 2));
    Console.WriteLine($"{device.Name} | USB 2dc8:310a bcdDevice {device.Version:x4} | " +
        $"vendor model 0x{model:x4} | vendor firmware {reply[2] / 100}.{reply[2] % 100:D2}");
    if (model != 0x301b || reply[2] != 109)
        throw new IOException("The flash layout is pinned to the direct-cable Wireless model 301b v1.09.");

    if (args[0] == "identify") return 0;
    if (args[0] == "diagnose") return PairingSnapshot.Save(channel, args[1]);
    if (args[0] == "verify-image")
    {
        var expected = File.ReadAllBytes(args[1]);
        if (expected.Length < 0x12400 || expected.Length > 0x1e000)
            throw new IOException("Image is outside the reviewed application range.");
        for (int offset = 0; offset < expected.Length; offset += 46)
        {
            int count = Math.Min(46, expected.Length - offset);
            var live = AppProtocol.Read(channel, 0x6000 + offset, count);
            if (live is null || !live.AsSpan().SequenceEqual(expected.AsSpan(offset, count)))
                throw new IOException($"Installed application differs or read failed at 0x{0x6000 + offset:x5}.");
        }
        Console.WriteLine($"Verified installed application: {expected.Length} bytes, SHA256 {Convert.ToHexString(SHA256.HashData(expected))}. Read only.");
        return 0;
    }

    const int size = 0x80000;
    const int chunk = 46;
    var output = Path.GetFullPath(args[1]);
    if (File.Exists(output)) throw new IOException("Choose a new backup filename.");
    var first = new byte[size];
    for (int pass = 1; pass <= 2; pass++)
    {
        for (int address = 0; address < size; address += chunk)
        {
            var count = Math.Min(size - address, chunk);
            var bytes = AppProtocol.Read(channel, address, count)
                ?? throw new IOException($"Read failed at 0x{address:x5}, pass {pass}.");
            if (pass == 1) bytes.CopyTo(first, address);
            else if (!first.AsSpan(address, count).SequenceEqual(bytes))
                throw new IOException($"Reads differ at 0x{address:x5}. No backup committed.");
            if (address % (chunk * 2048) == 0)
                Console.WriteLine($"Pass {pass}/2, {address * 100 / size}%");
        }
    }
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    var temp = output + "." + Guid.NewGuid().ToString("N") + ".partial";
    try
    {
        File.WriteAllBytes(temp, first);
        if (!first.AsSpan().SequenceEqual(File.ReadAllBytes(temp)))
            throw new IOException("Saved backup failed readback.");
        File.Move(temp, output, overwrite: false);
    }
    finally { File.Delete(temp); }
    Console.WriteLine($"{Convert.ToHexString(SHA256.HashData(first))}  {output}");
    return 0;
}
catch (Exception e)
{
    Console.Error.WriteLine(e.Message);
    return 1;
}
