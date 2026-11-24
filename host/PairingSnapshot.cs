// SPDX-License-Identifier: GPL-3.0-or-later
using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text.Json;
using Pad8Lab.Core.Hid;
using Pad8Lab.Core.Protocol;

internal static class PairingSnapshot
{
    // Read only. Never opens the vendor session that can unprotect flash.
    public static int Save(IChannel channel, string destination)
    {
        var folder = Path.GetFullPath(destination);
        if (Directory.Exists(folder)) throw new IOException("Choose a new private diagnostic folder.");
        var journal = ReadVerified(channel, 0x3d000, 0x2000);
        var metadata = ReadVerified(channel, 0x3f000, 0x34);
        var json = Describe(journal, metadata);
        Directory.CreateDirectory(folder);
        File.WriteAllBytes(Path.Combine(folder, "bond-journal.bin"), journal);
        File.WriteAllBytes(Path.Combine(folder, "update-metadata.bin"), metadata);
        File.WriteAllText(Path.Combine(folder, "summary.json"), json);
        Console.WriteLine(json);
        Console.WriteLine($"Saved private diagnostic snapshot: {folder}");
        return 0;
    }

    private static string Describe(byte[] journal, byte[] metadata)
    {
        var records = new List<object>();
        var diagnostics = new List<object>();
        int erased = 0, invalid = 0;
        for (int offset = 0; offset < journal.Length; offset += 64)
        {
            var record = journal.AsSpan(offset, 64);
            if (record.IndexOfAnyExcept((byte)255) < 0) { erased++; continue; }
            if (record[..6].SequenceEqual("S2DG\u0001\0"u8) && record[6] == 255 && record[7] == 255 &&
                BinaryPrimitives.ReadUInt32LittleEndian(record[40..]) == Crc32(record[..40]) &&
                BinaryPrimitives.ReadUInt32LittleEndian(record[44..]) == 0x44474f4c &&
                record[48..].IndexOfAnyExcept((byte)255) < 0)
            {
                var p = record[8..40];
                var flags = BinaryPrimitives.ReadUInt16LittleEndian(p[4..]);
                bool ledLengths = p[0] == 2 && (flags & 1024) != 0;
                diagnostics.Add(new {
                    address = $"0x{0x3d000 + offset:x5}",
                    eventType = p[0] switch { 1 => "boot", 2 => "disconnected", 3 => "pair button", 4 => "wake interval elapsed", 5 => "first Bluetooth poll", _ => "unknown" },
                    errorSource = p[1] switch { 0 => "none", 1 => "receive framing", 2 => "ATT", 3 => "controller command", 4 => "key lookup", 5 => "setup timeout", _ => "unknown" },
                    result = (sbyte)p[2], pairingStage = p[3],
                    flags = $"0x{flags:x4}",
                    buttonPressEnqueued = (flags & 2048) != 0, commandRejected = (flags & 4096) != 0,
                    commandQueueWasFull = (flags & 8192) != 0, motorDriveIssued = (flags & 16384) != 0,
                    dpadPressEnqueued = (flags & 32768) != 0,
                    millisecondsModulo65536 = BinaryPrimitives.ReadUInt16LittleEndian(p[6..]),
                    receivedLinkPackets = BinaryPrimitives.ReadUInt16LittleEndian(p[8..]),
                    acceptedTransmitEnqueues = BinaryPrimitives.ReadUInt16LittleEndian(p[10..]),
                    lastAttOpcode = $"0x{p[12]:x2}", lastHandle = $"0x{p[13]:x2}",
                    lastCommand = $"0x{p[14]:x2}", lastSubcommand = $"0x{p[15]:x2}",
                    mtu = BinaryPrimitives.ReadUInt16LittleEndian(p[16..]), subscriptions = p[18],
                    encrypted = p[19] != 0, csa2 = p[20] != 0, phy = p[21],
                    connectionEventCounter = BinaryPrimitives.ReadUInt16LittleEndian(p[22..]),
                    queuedResponses = p[24], pendingAttResponse = p[25], pairing = p[26] != 0,
                    failed = p[27] != 0, mtuOffer = p[28], stockLinkState = p[29],
                    disconnectEventByte0 = p[0] == 2 ? $"0x{p[30]:x2}" : null,
                    disconnectEventByte1 = p[0] == 2 && !ledLengths ? $"0x{p[31]:x2}" : null,
                    ledDeclaredBytesCappedAt15 = ledLengths ? (int?)(p[31] >> 4) : null,
                    ledReceivedBytesCappedAt15 = ledLengths ? (int?)(p[31] & 15) : null,
                    radioStateBeforeRestart = p[0] is 3 or 4 ? (int?)p[30] : null,
                    statusLedModeBeforeRestart = p[0] is 3 or 4 ? (int?)p[31] : null,
                    statusLedModeAtFirstPoll = p[0] == 5 ? (int?)p[31] : null
                });
                continue;
            }
            if (!record[..6].SequenceEqual("S2BD\u0001\0"u8) || record[6] > 1 || record[7] != 255 ||
                BinaryPrimitives.ReadUInt32LittleEndian(record[40..]) != Crc32(record[..40]) ||
                BinaryPrimitives.ReadUInt32LittleEndian(record[44..]) != 0x32444e42 ||
                record[48..].IndexOfAnyExcept((byte)255) >= 0)
            { invalid++; continue; }
            // Never print keys or peer addresses. The raw private dump preserves evidence.
            records.Add(new { address = $"0x{0x3d000 + offset:x5}",
                sequence = BinaryPrimitives.ReadUInt32LittleEndian(record[8..]), peerType = record[6] });
        }
        var report = new
        {
            capturedUtc = DateTimeOffset.UtcNow,
            model = "301b", stockVersion = "1.09", readOnly = true, verifiedReadPasses = 2,
            journalSha256 = Convert.ToHexString(SHA256.HashData(journal)).ToLowerInvariant(),
            journalEntirelyErased = erased == 128,
            validBondRecords = records.Count, erasedSlots = erased, invalidNonemptySlots = invalid,
            records, diagnostics,
            updateMagic = $"0x{BinaryPrimitives.ReadUInt32LittleEndian(metadata.AsSpan(0x18)):x8}",
            updateSize = BinaryPrimitives.ReadUInt32LittleEndian(metadata.AsSpan(0x20)),
            runtimeBleLogAvailable = diagnostics.Count > 0
        };
        return JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true });
    }

    public static int SelfTest()
    {
        var journal = Enumerable.Repeat((byte)255, 8192).ToArray();
        var metadata = new byte[0x34];
        var record = journal.AsSpan(0, 64);
        "S2DG\u0001\0"u8.CopyTo(record);
        record.Slice(8, 32).Clear();
        record[8] = 2; record[9] = 3; record[10] = 255; record[22] = 0x15;
        BinaryPrimitives.WriteUInt32LittleEndian(record[40..], Crc32(record[..40]));
        BinaryPrimitives.WriteUInt32LittleEndian(record[44..], 0x44474f4c);
        using (var doc = JsonDocument.Parse(Describe(journal, metadata)))
        {
            var r = doc.RootElement; var d = r.GetProperty("diagnostics")[0];
            if (r.GetProperty("validBondRecords").GetInt32() != 0 || r.GetProperty("invalidNonemptySlots").GetInt32() != 0 ||
                !r.GetProperty("runtimeBleLogAvailable").GetBoolean() ||
                d.GetProperty("lastCommand").GetString() != "0x15" || d.GetProperty("result").GetInt32() != -1)
                throw new IOException("Diagnostic decode failed.");
        }
        foreach (int offset in new[] { 0, 6, 8, 40, 44, 48 })
        {
            journal[offset] ^= 1;
            using var doc = JsonDocument.Parse(Describe(journal, metadata));
            if (doc.RootElement.GetProperty("diagnostics").GetArrayLength() != 0 ||
                doc.RootElement.GetProperty("invalidNonemptySlots").GetInt32() != 1)
                throw new IOException("Damaged diagnostic accepted.");
            journal[offset] ^= 1;
        }
        foreach (byte kind in new byte[] { 3, 4 })
        {
            record[8] = kind; record[38] = 1; record[39] = 3;
            BinaryPrimitives.WriteUInt32LittleEndian(record[40..], Crc32(record[..40]));
            using var doc = JsonDocument.Parse(Describe(journal, metadata));
            var d = doc.RootElement.GetProperty("diagnostics")[0];
            if (d.GetProperty("eventType").GetString() != (kind == 3 ? "pair button" : "wake interval elapsed") ||
                d.GetProperty("radioStateBeforeRestart").GetInt32() != 1 ||
                d.GetProperty("statusLedModeBeforeRestart").GetInt32() != 3 ||
                d.GetProperty("disconnectEventByte0").ValueKind != JsonValueKind.Null)
                throw new IOException("Bluetooth restart diagnostic decode failed.");
        }
        record[8] = 5; record[38] = 0; record[39] = 3;
        BinaryPrimitives.WriteUInt32LittleEndian(record[40..], Crc32(record[..40]));
        using (var doc = JsonDocument.Parse(Describe(journal, metadata)))
        {
            var first = doc.RootElement.GetProperty("diagnostics")[0];
            if (first.GetProperty("eventType").GetString() != "first Bluetooth poll" ||
                first.GetProperty("statusLedModeAtFirstPoll").GetInt32() != 3)
                throw new IOException("Bluetooth first-poll diagnostic decode failed.");
        }
        record[8] = 2; record[13] |= 4; record[38] = 0x13; record[39] = 0x81;
        BinaryPrimitives.WriteUInt32LittleEndian(record[40..], Crc32(record[..40]));
        using (var doc = JsonDocument.Parse(Describe(journal, metadata)))
        {
            var d = doc.RootElement.GetProperty("diagnostics")[0];
            if (d.GetProperty("ledDeclaredBytesCappedAt15").GetInt32() != 8 ||
                d.GetProperty("ledReceivedBytesCappedAt15").GetInt32() != 1 ||
                d.GetProperty("disconnectEventByte0").GetString() != "0x13" ||
                d.GetProperty("disconnectEventByte1").ValueKind != JsonValueKind.Null)
                throw new IOException("Player-light length diagnostic decode failed.");
        }
        Console.WriteLine("PASS: metadata diagnostic decoding, signed status, record isolation and corruption rejection. No device accessed.");
        return 0;
    }

    private static byte[] ReadVerified(IChannel channel, int start, int size)
    {
        var result = new byte[size];
        for (int pass = 0; pass < 2; pass++)
        for (int offset = 0; offset < size; offset += 46)
        {
            int count = Math.Min(46, size - offset);
            var data = AppProtocol.Read(channel, start + offset, count)
                ?? throw new IOException($"Read failed at 0x{start + offset:x5}.");
            if (data.Length != count) throw new IOException("Unexpected diagnostic read length.");
            if (pass == 0) data.CopyTo(result, offset);
            else if (!result.AsSpan(offset, count).SequenceEqual(data))
                throw new IOException("Diagnostic reads changed between passes; no snapshot saved.");
        }
        return result;
    }

    private static uint Crc32(ReadOnlySpan<byte> bytes)
    {
        uint value = uint.MaxValue;
        foreach (byte b in bytes)
        {
            value ^= b;
            for (int bit = 0; bit < 8; bit++) value = (value >> 1) ^ ((value & 1) != 0 ? 0xedb88320u : 0);
        }
        return ~value;
    }
}
