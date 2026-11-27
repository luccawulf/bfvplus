# bfvplus 0.6 — for Battlefield Vietnam v1.21

A small `dinput8.dll` mod for **Battlefield Vietnam v1.21**. Five features:

1. **Buddy colors** — the scoreboard's "Add Buddy" button cycles through preset
   colors instead of always assigning green.
2. **Look sensitivity fix** — corrects the horizontal look sensitivity that feels
   doubled while moving on foot. Off by default.
3. **Stationary MG sensitivity** — keeps your on-foot mouse feel on a stationary MG.
   Off by default.
4. **Keyhash changer** *(new in 0.6)* — present a chosen key hash to servers
   instead of the one made from your CD key. Off until you set one.
5. **Client replay recorder** *(new in 0.6)* — records each game you join, from your
   own client, for the replay viewer. Based on Skandia's replay recorder for
   Battlefield 1942. Off by default.

Inspired by uuuzbf/bf42plus.

## What's new in 0.6

- **Keyhash changer.** Set `[keyhash] override` in `bfvplus.ini` (or drop a
  `keyhash.txt` next to `BfVietnam.exe`) and that 32-character hash is what a
  server records for you. See [section 4](#4-keyhash-changer).
- **Client replay recorder.** Turn on `[replay] recordReplays` and every game you
  join is written to `replays\` as a replay file: the server's events, every
  soldier and vehicle your client knows at up to 30 samples a second, damage,
  rounds, tickets, control points, radios, chat and your own view and input.
  See [section 5](#5-client-replay-recorder).

## Install

1. Download `bfvplus-0.6.zip` from the [Releases](../../releases) page.
2. Extract `dinput8.dll` and `bfvplus.ini` into your BFV folder (the one
   containing `BfVietnam.exe`).
3. Start the game.

If you don't use the shipped `bfvplus.ini`, one is created next to
`BfVietnam.exe` on first launch. To build the DLL yourself, see [Build](#build).

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
`dinput8.dll` (so input still works), then patch the few places in the game
each feature needs. Every patch first checks that the bytes it replaces are
the ones it expects, and the feature switches itself off if they aren't.

For buddy colors, the patched instruction is at `0x0057A1E4` and originally
called `addPlayerToBuddyList(playerId)` with default green. Our replacement
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

## 4. Keyhash changer

When you join a server, your client answers the server's CD-key challenge. The
first field of that answer is your **key hash**: the MD5 of your CD key, 32 hex
characters. Servers use it as your identity (bans, admin rights, stats).

The keyhash changer lets you present a **chosen** 32-character hash instead.
Almost every server checks CD keys, and the hash you set is the identity a
server records for you either way — checking or not. Use a **real** key hash,
one made from a valid CD key: that is what servers accept.

```ini
[keyhash]
override = <your 32 hex characters>   ; e.g. 32 characters from 0-9 and a-f
```

Or drop a `keyhash.txt` next to `BfVietnam.exe` whose first 32 characters are the
hash (used only when `bfvplus.ini` sets none). With neither, nothing is changed and
the hook isn't installed.

- **How it works:** bfvplus detours the game's CD-key answer builder
  (`gcd_compute_response`, `0x005C70A0`). The game builds its real answer first, so
  you still need a valid CD key to connect; bfvplus then replaces the 32-character
  first field with your override, the same length, and touches nothing else.
- **What it logs:** `logs\bfvplus_keyhash.log` notes whether an override is active
  and each time it is applied, **including your real key hash**. Don't post that
  log publicly. With `[capture] enabled = on` and an override active, the capture
  log also gets one `LocalKeyHash` line with your real hash.
- **Please use it fairly:** on your own server, or where the server's admin is
  fine with it. Key hashes are how admins tell players apart.
- To build a DLL without it, see [Build](#build).

## 5. Client replay recorder

The Battlefield Vietnam client recorder, based on Skandia's replay recorder for
Battlefield 1942: it records each game you join, from your own client, for the
replay viewer. **Off by default.** New in 0.6 and still in active development.

```ini
[replay]
recordReplays = on   ; off by default
sampleHz      = 30   ; soldier and vehicle samples per second, 1-30
```

- **One file per join:** `replays\replay_<date>-<time>.ndjson` next to `BfVietnam.exe`, in the
  same replay format as the BF1942 recorder (version 5, marked `"game":"bfv"`). It is written to
  disk every second, so a game crash loses at most about a second, and it is closed with an `end`
  record when you leave or quit. A file a crash cut short has no `end` record and still plays, to
  its last record.
- **What it holds:**
  - every game event the server sends (players joining and leaving, teams, vehicles entered and
    left, kills and scores, objects made and destroyed, the level and rules), in the same form as
    the server replay recorder writes them
  - the players (the roster marks you)
  - every soldier and vehicle your client knows, at up to 30 samples a second
  - their damage (hit points as your client holds them, in whole points), turrets, rotors and other
    moving parts, engines, soldiers' animation states, aim and held weapon, and the animation table
  - every round fired that your client sees, each with its muzzle and direction, yours marked
  - the tickets, the control points' owners, the vehicle radios' songs, and your hit marks
  - an `info` record with the server's name, the level, the game mode and your own name, for the
    website's replay list (again if the level changes on the same server)
  - every rocket, bomb and missile in flight that your client flies (the SA-7 included), placed each
    sample, in the server recorder's `pn`/`pj`/`pd` rows
  - which soldiers are beyond about 200 m from you (`far` rows), whose recorded states are stale
  - your own view as your game drew it (`cam`: place, turn, field of view, on change) and your camera's
    view mode, camera and vehicle (`camv`), so a replay can show exactly what you saw
  - the action word your client sends each tick (`in`: the six 12-bit axes and the button mask, as
    the server receives them), on change, every tick's word read from your client's buffer, so a key
    held for one tick between two samples is kept
  - these two were found by reading the game's code only; `tools/check_view.py <file>` compares them
    with your soldier, aim and rounds in the same file
  - the chat box's lines as your game shows them (`chat`: the line, its sender and his side), your own
    lines among them: the server never sends a player his own line back, so the chat pieces it sends
    (also kept) miss everything you say. The file's header then says `"chatLines":1`, and the viewer
    reads these lines alone. Found by reading the game's code only (the chat box's
    `addPlayerChatMessage`, 0x004CC2A0, and its seven call sites)
- **Read-only:** it reads what your game already holds. To see each round fired, it watches the six
  places the game fires a barrel (the round itself is untouched); to see each chat line, the seven
  places the game hands one to the chat box (the line itself is untouched). It sends nothing and changes nothing
  about gameplay, input or network traffic.
- **Your view of the round:** a client only hears about what the server sends it. Far-away
  players update less often, and fast aircraft are shown as your client extrapolated them. Your client
  also stops animating soldiers beyond about 200 m, so their recorded states can lag behind; the
  `far` rows mark them and the viewer poses them from their movement instead. The bots' AI level of
  detail (the server recorder's `lod`) lives only on the server, so a client file has none.
- If BFV is installed under `Program Files`, Windows may keep the files in
  `%LOCALAPPDATA%\VirtualStore\Program Files\...\replays` instead (it redirects writes into
  protected folders).
- If a game structure doesn't match what bfvplus expects (another patch level), the recorder
  switches itself off and writes one line to `logs\bfvplus.log`. Everything else keeps working. If only
  the firing places differ, it records everything but the rounds and says so in the same log.

## INI reference

`bfvplus.ini` sits next to `BfVietnam.exe` and is read once at game start
(restart the game to apply changes). The release ships this one:

```ini
[presetcolors]               ; the Add Buddy color cycle, in order
color = #00ff00              ; green (BFV default)
color = #ffff00              ; yellow
color = #ff0000              ; red
color = #00aaff              ; light blue

[sensitivity]
correctedLookSensitivity = off
correctedLookScale = 0.5
correctedLookTrigger = strafe
stationaryMGInfSensitivity = off

[keyhash]
;override = <32 hex characters>

[replay]
recordReplays = off
sampleHz = 30

[capture]                    ; every server GameEvent to logs\bfvplus_server.log (JSON Lines)
enabled = off
dumpObjects = off
dumpObjectsInterval = 30
```

- `[presetcolors]`: order matters; after the last entry the cycle wraps to the
  first. Add as many `color = #RRGGBB` lines as you want.
- `[capture]` is a debugging log of the server's game events, for modders.

## Build

You need **Visual Studio 2026** with the "Desktop development with C++" workload
(free Community edition is fine). The project uses platform toolset `v145`; with
Visual Studio 2022, retarget it to `v143` first (right-click the project →
Retarget Projects).

1. Open `bfvplus.sln`.
2. Set the active configuration to **Release / Win32** (toolbar dropdown).
   `Win32` means 32-bit x86 — BFV is 32-bit.
3. Build → Build Solution (`Ctrl+Shift+B`).

Output: `Release\dinput8.dll`. This is the build the release ships, with every
feature including the keyhash changer.

### Building without the keyhash changer

Define `BFVPLUS_PUBLIC` to compile the keyhash changer out entirely (none of its
code or strings are in the binary):

```bash
msbuild bfvplus.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:ExtraDefines=BFVPLUS_PUBLIC /p:OutDir=ReleasePublic\ /p:IntDir=ReleasePublic\obj\
```

Output: `ReleasePublic\dinput8.dll`.

### Tests and tools

- `tests/run.sh` builds and runs the replay recorder's unit tests with g++ (Linux
  or WSL).
- `tools/check_exes.py` checks every patched site in `src/bfv_addresses.h`
  against one or more `BfVietnam.exe` files.
- `tools/check_view.py <replay file>` checks a recording's `cam` and `in` rows
  against the soldier, aim and rounds in the same file.

## Uninstall

Delete `dinput8.dll` from the BFV folder. The game falls back to the system
`dinput8.dll` automatically. Existing buddy colors stay in
`settings\profiles\<profile>\buddylist.con` regardless of whether bfvplus
is loaded — they're a native BFV feature. Delete `bfvplus.ini`, `keyhash.txt`,
`logs\bfvplus*.log` and `replays\` too if you want every trace gone.

## Notes

- Targets BFV **v1.21**. Every hook checks the bytes it patches before
  patching, so on a different patch level or a substantially modified
  `BfVietnam.exe` the affected feature does nothing (sound and input still work)
  and, for the recorder and the keyhash changer, says so in its log.
  `tools/check_exes.py` shows which sites match a given exe.
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

**No replay files appear.**
Check `recordReplays = on` under `[replay]`, then look in `logs\bfvplus.log`
for a line saying why the recorder switched off. Under `Program Files`, look in
`%LOCALAPPDATA%\VirtualStore` (see section 5).

**The keyhash changer does nothing.**
Check `logs\bfvplus_keyhash.log`: it says whether an override was read, whether
the hook was installed, and each time it was applied. The value must be exactly
32 hex characters, and a real key hash (see section 4).

## Acknowledgements

- uuuzbf/bf42plus for the original BF1942 implementation and the pattern
  this is modeled after.
- Skandia for the Battlefield 1942 replay recorder the client recorder is
  based on, and the replay format and viewer it writes for.
