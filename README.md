# bfvplus 0.2 — buddy colors + look sensitivity fix for Battlefield Vietnam

A small mod for **Battlefield Vietnam v1.21**. Two features:

1. **Buddy colors** — the scoreboard's "Add Buddy" button cycles through preset
   colors instead of always assigning green.
2. **Mouse sensitivity fixes** — corrects the horizontal look sensitivity that
   feels doubled while moving on foot, and optionally makes stationary MGs use
   your Infantry sensitivity instead of LandSea.

Both sensitivity options are **off by default** — nothing changes until you enable
them in `bfvplus.ini`.

Inspired by [uuuzbf/bf42plus](https://github.com/uuuzbf/bf42plus).

## 1. Buddy colors

Battlefield Vietnam already supports per-buddy colors natively (since patch
1.2 — colors are stored in `settings\profiles\<your profile>\buddylist.con`).
You set them via the console command:

    game.addPlayerToBuddyListByName "PlayerName" 255.0 0.0 0.0

What it *doesn't* do natively is give you a UI for changing those colors.
The scoreboard's "Add Buddy" button always assigns the default green.

bfvplus changes the button's behavior:

- **First click on a non-buddy** — adds them as a buddy in the default green
  (same as before).
- **Subsequent clicks on the same buddy** — cycles them through the colors
  you configured in `bfvplus.ini`.
- **Shift + click on an existing buddy** — assigns a random bright color.

## How it works

The mod is loaded as `dinput8.dll`. BFV's `BfVietnam.exe` imports the
`DirectInput8Create` function from `dinput8.dll` for keyboard/mouse input.
Windows finds our DLL in the BFV folder before the system one, so we get
loaded into the process. We proxy `DirectInput8Create` to the real system
`dinput8.dll` (so input still works), then patch a single 5-byte `call`
instruction in the scoreboard's button handler to redirect through our
custom logic.

The patched instruction is at `0x0057A1E4` and originally called
`addPlayerToBuddyList(playerId)` with default green. Our replacement
checks if the player is already a buddy (via `isPlayerIDInBuddyList`),
and either does the original behavior or calls `addPlayerToBuddyListByName`
with the next color in the cycle.

## 2. Look sensitivity fix

Several players have reported that BFV's look sensitivity feels roughly doubled
while strafing on foot. It is a real asymmetry in the engine, not a placebo.

In `BFSoldier::handlePlayerInput` (retail `0x0065F350`) both look axes start with
the same scaling — each is `* 30.0 * dt`. They diverge afterwards: a loop calls a
routine that produces per-axis modifiers, and the **yaw** delta gets multiplied by
one of them while the **pitch** delta, computed right after the loop, never does.
So horizontal ends up faster than vertical.

The fix scales the mouse X axis to compensate. Crucially it does this at the
**DirectInput layer** — bfvplus is already a `dinput8.dll` proxy, which sits
upstream of everything. The game genuinely receives a smaller mouse movement, so
local simulation and the packet sent to the server are built from the same number
and **there is no desync**. (bf42plus has to hook `Game::addPlayerInput` on BF1942
for exactly this reason; doing it in `handlePlayerInput` desyncs because the
server runs the same code.)

```ini
[sensitivity]
correctedLookSensitivity = off     ; on to enable
correctedLookScale       = 0.5     ; multiplier while the trigger holds
correctedLookTrigger     = strafe  ; strafe (default) or any
```

- `correctedLookTrigger = strafe` only corrects while strafing, which is what
  players actually report on BFV. `any` matches bf42plus's BF1942 behaviour
  (forward **or** strafe).
- `correctedLookScale = 0.5` matches bf42plus. Tune to taste — the right value
  for BFV has not been confirmed by measurement, only by feel.
- Only applies on foot. Vehicles are untouched.

**This scales real mouse input, so it affects your aim, not just your view.**
That is the same net effect bf42plus has, but it is worth knowing.

## 3. Stationary MG sensitivity

BFV keeps separate mouse sensitivity settings for Infantry, LandSea and Air. A
stationary MG counts as a LandSea vehicle, so mounting one changes how the mouse
feels even though you have not really changed what you are doing. This option
rescales the mouse by `InfMouseSensitivity / LandSeaMouseSensitivity` while you are
on a stationary MG, so it matches your on-foot feel, and flips the Y axis if your
two invert settings disagree.

```ini
[sensitivity]
stationaryMGInfSensitivity = off   ; on to enable
```

Ported from bf42plus, where the same option exists for BF1942's MG42/Browning.
Like theirs, the maths is slightly approximate — the engine adds a small constant
to the sensitivity elsewhere that this does not model.

Detection uses the vehicle's template: `queryInterface(0xC4C4)` then
`getVehicleType()`, checking for `VTStationaryMG` (ordinal 9, read off the
enum-to-string jump table at `0x0061818C`). The live sensitivity and invert values
are read from the game's own `InfantryControls` and `LandSeaControls` objects,
which bfvplus captures as the game constructs them.

## Build

You need **Visual Studio 2022** with the "Desktop development with C++"
workload (free Community edition is fine).

1. Open `bfvplus.sln`.
2. Set the active configuration to **Release / Win32** (toolbar dropdown).
   `Win32` means 32-bit x86 — BFV is 32-bit.
3. Build → Build Solution (`Ctrl+Shift+B`).

Output: `Release\dinput8.dll`.

## Install

1. Find your BFV install folder (the one containing `BfVietnam.exe`).
2. Copy `dinput8.dll` into that folder (from a release, or from `Release\` if you built it).
3. Start the game. A `bfvplus.ini` will be created next to `BfVietnam.exe`
   on first launch with the default preset color cycle.

## INI format

`bfvplus.ini` is created next to `BfVietnam.exe` on first launch. Full example:

```ini
[presetcolors]
color = #00ff00   ; green (BFV default)
color = #ffff00   ; yellow
color = #ff0000   ; red
color = #00aaff   ; light blue

[sensitivity]
correctedLookSensitivity   = off     ; look sensitivity fix (section 2)
correctedLookScale         = 0.5
correctedLookTrigger       = strafe  ; strafe | any
stationaryMGInfSensitivity = off     ; stationary MG fix (section 3)

[capture]
enabled             = off   ; log server GameEvents to logsfvplus_server.log
dumpObjects         = off
dumpObjectsInterval = 30
```

- Colour order matters. The cycle follows the order of entries, wrapping after
  the last.
- The INI is read once at game start. Restart the game to apply changes.
- Both sensitivity options default to **off** — nothing changes until you opt in.

## Uninstall

Delete `dinput8.dll` from the BFV folder. The game falls back to the system
`dinput8.dll` automatically. Existing buddy colors stay in
`settings\profiles\<profile>\buddylist.con` regardless of whether bfvplus
is loaded — they're a native BFV feature.

## Notes

- Targets BFV **v1.21** specifically (binary timestamp Oct 2004,
  MD5 `0c3d8058b6e1ae76bb4da7618d2b9478`). Some community patches like
  BFV Redux may keep the same binary; the mod will work if so. If
  `BfVietnam.exe` has been modified more substantially, the mod silently
  does nothing — sound/input still work, but the button cycle won't be
  active.
- Per-buddy colors are persisted to
  `Settings\Profiles\<your profile>\BuddyList.con` after every cycle.
  The mod calls BFV's own save function at `0x54B2D0` immediately after
  changing a color, so your changes survive game restarts.

## Troubleshooting

**Game doesn't start.**
Most likely the system `dinput8.dll` couldn't be loaded. If you built the
Debug configuration, you also need the MSVC redistributable installed —
build Release instead, which links the CRT statically.

**The button still always assigns green.**
The hook didn't install. Either:
- Your `BfVietnam.exe` isn't v1.21 retail (different patch level or a
  modified binary), or
- The DLL isn't in the right folder (must be next to `BfVietnam.exe`).

**My color choices aren't appearing in the cycle.**
Check `bfvplus.ini` is next to `BfVietnam.exe`, not in some working
directory. Restart the game after editing.

## Acknowledgements

- uuuzbf/bf42plus for the original BF1942 implementation and the pattern
  this is modeled after.
