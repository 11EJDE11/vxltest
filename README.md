# vxltest

CPU self-test that replays the Red Alert 2 / Yuri's Revenge voxel renderer.

## Why this exists

Some players crash inside the game's voxel (tank / aircraft) renderer at the
same instruction every time. Their crash dumps show the renderer's row counter
and row index disagreeing in a way the instruction stream cannot produce:
the loop runs past the end of the section while the counter that should have
stopped it has been decremented more times than rows were drawn. The in-memory
code matches the shipped binary and the voxel data is well-formed, which leaves
the machine itself as the suspect.

`vxltest.exe` contains the same instruction sequence as the renderer variant
that crashes (gamemd.exe `0x7DF9C0`), with the game's static tables replaced by
its own. It renders synthetic voxel sections sized like the crashing ones, both
with that code and with a slow plain-C version of the same loop, and compares
the row index, the position and the full pixel buffer after every run. It does
this pinned to each logical CPU in turn, then on all CPUs at once.

It does not touch the game, your files or the network. It writes one file,
`vxltest_log.txt`, next to the exe.

## Running it

1. Download `vxltest.zip` from the Releases page and extract it.
2. Close the game and anything heavy.
3. Double-click `vxltest.exe`. With 20 CPU threads the default run takes about
   1 min 40 s (4 s per core, then 20 s on all cores).
4. When it says "Press Enter to close", send `vxltest_log.txt` to whoever asked
   you to run it.

Optional: from a command prompt, `vxltest.exe 10 60` runs 10 s per core and
60 s on all cores. Longer runs catch rarer faults. Running once on battery and
once on mains power is worth doing on a laptop.

Windows SmartScreen will probably warn about the exe because it is unsigned.

## Reading the result

- **No errors**: the loop ran correctly this time. This does not fully clear
  the hardware, since faults can be rare or depend on heat and power state,
  but it makes a CPU fault less likely.
- **ERRORS FOUND**: the CPU gave two different answers to the same arithmetic
  with no game, no injector and no other threads involved. Software cannot do
  that. Look at CPU stability (undervolt, overclock, power limits, degraded
  chip), RAM (XMP profile, faulty module), BIOS and chipset firmware, and
  cooling.

## Building

32-bit MSVC only, since the renderer is x86 inline assembly:

```
cl /O2 /MT /W3 vxltest.c /Fe:vxltest.exe /link /SUBSYSTEM:CONSOLE
```

`build.cmd` does this after calling `vcvars32.bat` from a Visual Studio 2022+
Community install. Adjust the path if yours differs.
