Chiptracker - chiptune / keygen module player
=============================================

Plays tracker modules with libopenmpt (the OpenMPT engine):
XM, IT, MOD, S3M, MO3 and ~30 more tracker formats.

Controls
--------
D-pad up/down    previous / next order (instant)
D-pad left/right previous / next channel page (carousel slide)
L / R            previous / next track
A                play / pause
B                library (stays in the list after picking a track)
X                track info (metadata, message, samples)
Y                fullscreen visualizer (Y there = next style)
Start            settings (music folder, theme, lock)
Hold Start 0.7s  lock the screen (Select too, see LOCK BUTTON)
Select (Fn)      stats window: FPS, position, library, battery
Menu             exit

In Settings:
D-pad up/down    item
D-pad left/right change the value
A                change / open
L / R            short description of the selected item
                 (tap: 4 s, hold: until released)

In the visualizer:
Y                next style (SPECTRUM, RADIAL, SCOPE, WATERFALL,
                 PLASMA, TUNNEL, STARS, PATTERN)
A                play / pause
B                back to the tracker
L / R            previous / next track
D-pad right      current style parameter (flow, speed, gain...)
D-pad left       second parameter: mode (VECTOR/XY/DOTS, VORTEX,
                 RINGS, PLANETS...), an effect in SPECTRUM (SAKURA /
                 RAIN / SPARK / FIRE / PULSE) or the record spin in
                 RADIAL; in PATTERN both directions page through
                 9-channel screens

Screen lock
-----------
SYSTEM -> LOCK BUTTON chooses START, SELECT or BOTH.  Hold it for 0.7 s
(BOTH means Start+Select at the same time): "LOCKING..." fades in, then
the screen goes black and the backlight goes to its lowest level; the
music keeps playing.  A press of the chosen key combo shows the dim lock
hint immediately (progress from zero); hold the same combo for 1.5 s to
unlock.  Other keys do not wake the screen.

The hint stays for 1.5 s and names the combo: HOLD START / HOLD SELECT /
HOLD START+SEL.

A full backlight off is not possible on this panel: brightness 0,
bl_power=4 and the minimum level all look the same, so the app uses the
minimum level and a black screen.  Diagnostics are appended to lock.log
next to config.ini.

First start: pick the folder with your modules. It is remembered and
scanned at every start; Start in the library rescans it.  The splash stays
for 3 seconds (any key skips); it can be disabled in Settings.  Button hints
fade out after a few seconds in the player/info; any key brings them back.
Full controls: Settings -> HELP / CONTROLS.

The on-screen hint colours match the physical face buttons on every RG Nano
shell: A red, B yellow, X blue, Y green.

Themes: TEAL (blue RG Nano), BLACK, PURPLE, RED, SILVER, GAME BOY.

Made for the Anbernic RG Nano / FunKey S (FunKey-OS, SDL 1.2).
