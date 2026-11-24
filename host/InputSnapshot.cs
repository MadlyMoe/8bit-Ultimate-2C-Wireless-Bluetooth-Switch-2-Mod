// SPDX-License-Identifier: MIT
using System.Runtime.InteropServices;
using System.Text.Json;

internal static class InputSnapshot
{
    [StructLayout(LayoutKind.Sequential)]
    private struct State
    {
        public uint Packet;
        public ushort Buttons;
        public byte LeftTrigger, RightTrigger;
        public short LeftX, LeftY, RightX, RightY;
    }

    [DllImport("xinput1_4.dll", ExactSpelling = true)]
    private static extern uint XInputGetState(uint index, out State state);

    public static int Print()
    {
        if (Marshal.SizeOf<State>() != 16)
            throw new InvalidOperationException("XInput native structure layout mismatch.");
        var slots = new List<object>();
        for (uint slot = 0; slot < 4; ++slot)
        {
            var error = XInputGetState(slot, out var state);
            if (error == 1167) continue; // ERROR_DEVICE_NOT_CONNECTED
            if (error != 0) throw new IOException($"XInput slot {slot}: Windows error {error}");
            slots.Add(new { slot, packet = state.Packet, buttons = state.Buttons,
                leftTrigger = state.LeftTrigger, rightTrigger = state.RightTrigger,
                leftX = state.LeftX, leftY = state.LeftY,
                rightX = state.RightX, rightY = state.RightY });
        }
        Console.WriteLine(JsonSerializer.Serialize(new {
            capturedUtc = DateTimeOffset.UtcNow,
            note = "Read-only XInput snapshot. Slot identity and physical control coverage are not inferred.",
            slots
        }, new JsonSerializerOptions { WriteIndented = true }));
        return slots.Count > 0 ? 0 : 1;
    }
}
