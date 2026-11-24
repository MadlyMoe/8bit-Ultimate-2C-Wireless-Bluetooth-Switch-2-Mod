// SPDX-License-Identifier: GPL-3.0-or-later
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
internal static class FlashImage
{
    public const int Base=0x6000, StockEnd=0x18400, LegacyEnd=0x19000, Size=0x80000;
    public const int BondBase=0x3d000, BondEnd=0x3f000;
    private const string BootHash="D64D7096C29F0F4EE21981DF4B3F738C7895F209CDBDEB81F941838820BE340D";
    private const string StockHash="DFD443C77F831008AA453EE2C1C9CB6871C36F8947C548E61D4387FBA0A9DA70";
    private static readonly JsonDocument Recipe=ReadRecipe();
    public static int End=>Recipe.RootElement.GetProperty("end").GetInt32();
    public static string TargetHash=>Recipe.RootElement.GetProperty("target_sha256").GetString()!.ToUpperInvariant();
    public static string Hash(ReadOnlySpan<byte> bytes)=>Convert.ToHexString(SHA256.HashData(bytes));
    private static JsonDocument ReadRecipe()
    {
        using var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("full-301b-v1.json")
            ?? throw new IOException("Build the full firmware package before the flashing utility.");
        var doc=JsonDocument.Parse(stream);var r=doc.RootElement;
        if(r.GetProperty("format").GetString()!="switch22c-full-301b-v1" ||
           r.GetProperty("base").GetInt32()!=Base || r.GetProperty("end").GetInt32()<=LegacyEnd ||
           r.GetProperty("end").GetInt32()>0x24000 || (r.GetProperty("end").GetInt32()&4095)!=0 ||
           !string.Equals(r.GetProperty("boot_sha256").GetString(),BootHash,StringComparison.OrdinalIgnoreCase) ||
           !string.Equals(r.GetProperty("stock_sha256").GetString(),StockHash,StringComparison.OrdinalIgnoreCase))
            throw new IOException("Invalid compiled full-image manifest.");
        return doc;
    }
    public static void CheckBackup(byte[] backup)
    {
        if(backup.Length!=Size || Hash(backup.AsSpan(0,Base))!=BootHash ||
           Hash(backup.AsSpan(Base,StockEnd-Base))!=StockHash ||
           !backup.AsSpan(0x40000,StockEnd-Base).SequenceEqual(backup.AsSpan(Base,StockEnd-Base)) ||
           backup.AsSpan(StockEnd,BondEnd-StockEnd).IndexOfAnyExcept((byte)255)>=0 ||
           backup.AsSpan(0x3f018,4).IndexOfAnyExcept((byte)0)>=0 ||
           backup.AsSpan(0x3f020,4).IndexOfAnyExcept((byte)0)>=0)
            throw new IOException("Expected the owner's stock 301B v1.09 dump with the reviewed bootloader, stock banks and erased reserved space.");
    }
    public static byte[] Full(byte[] backup)
    {
        CheckBackup(backup);var target=backup[Base..End];var touched=new bool[target.Length];
        foreach(var edit in Recipe.RootElement.GetProperty("edits").EnumerateArray())
        {
            int a=Convert.ToInt32(edit.GetProperty("address").GetString(),16);
            var before=Convert.FromHexString(edit.GetProperty("before").GetString()!);
            var after=Convert.FromHexString(edit.GetProperty("after").GetString()!);
            if(a<Base || a>End || after.Length>End-a || before.Length!=after.Length ||
               !backup.AsSpan(a,before.Length).SequenceEqual(before))
                throw new IOException("Compiled patch preimage or bounds mismatch.");
            for(int i=a-Base;i<a-Base+after.Length;i++)
                if(touched[i]) throw new IOException("Overlapping patch edits."); else touched[i]=true;
            after.CopyTo(target,a-Base);
        }
        if(Hash(target)!=TargetHash) throw new IOException("Compiled patch output hash mismatch.");
        return target;
    }
    public static bool SameProtected(byte[] a,byte[] b)=>a.Length==Size && b.Length==Size &&
        a.AsSpan(0,Base).SequenceEqual(b.AsSpan(0,Base)) &&
        a.AsSpan(End,BondBase-End).SequenceEqual(b.AsSpan(End,BondBase-End)) &&
        a.AsSpan(BondEnd).SequenceEqual(b.AsSpan(BondEnd));
    public static bool KnownPrevious(byte[] flash,byte[] original)
    {
        string[] previousHashes=["8FA2F70D89B558B2B16F07C79C2ADA1ADE276408EF6F7883678991B4FC1A306B",
            "9F48218BF4A35E0BB18450DA6EF0F9A9D0EA2B3733262F7471E5264480CDDE5B",
            "CC7EE68591E61BD7193F85B2157166DB4117EE2614CA686E0E42776A8BB83617",
            "C2299C5F33221F03241D9FE90022DD8731415C94C698A9BC40432614DF1DC8BD",
            "F4BAE49055B06193F6327A81F854E1071DA8742C0AC4A96ED1DB3DF090788B4C",
            "7C2E045BCB314BE805F9D0077E0FECF189EA9185F79071178763A1E348A164D1",
            "668692E86708D34D3CED954F9C4B7ADB55B06DCBD963754B884293BEF97234B5",
            "39B3BB53ABB09687B08DBBCBB7904208792FB7B2C914ACD49D709D24A84649CE",
            "C965762DE0F99D1D06221B65D0F70C8446A8725D27760E260F9CAC56A8428687"];
        // Only byte-exact releases verified on this unit may be upgraded.
        // Any additional extension space must still match the original backup.
        if(flash.Length!=Size || original.Length!=Size) return false;
        return Matches(0x1d000,previousHashes) ||
            Matches(0x1e000,["62D50B64FE9DD3289C8B368365D799121F550385F96FC38990FB8D0B757DBDAD"]);

        bool Matches(int previousEnd,string[] hashes) =>
            End>=previousEnd && hashes.Contains(Hash(flash.AsSpan(Base,previousEnd-Base))) &&
            flash.AsSpan(previousEnd,End-previousEnd).SequenceEqual(original.AsSpan(previousEnd,End-previousEnd));
    }
    public static bool PendingUpdate(byte[] flash)
    {
        foreach(int a in new[]{0x3f018,0x3f020})
        {
            var field=flash[a..(a+4)];
            if(field.Any(b=>b!=0) && field.Any(b=>b!=255)) return true;
        }
        return false;
    }
}
