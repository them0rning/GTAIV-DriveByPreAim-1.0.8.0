DriveByPreAim v1.0 - aim from vehicles without shooting (GTA IV 1.0.8.0)
=======================================================================

WHAT IT DOES
  Controller: hold LB/L1 in a vehicle to aim without firing; press RB/R1 while aiming to shoot.
              (The handbrake is only disabled while you are aiming.)
  Mouse:      hold the right mouse button to aim; left click shoots as normal.
  The crosshair shows while the car is stopped, not just when moving.
  Drivers you aim at speed away; pedestrians flee. Police are not affected. (Can be turned off.)

REQUIREMENTS
  - GTA IV 1.0.8.0 ONLY (not the Complete Edition or other patches)
  - An ASI loader (Ultimate ASI Loader, or the one included with FusionFix)

INSTALL
  Copy DriveByPreAim.asi into your "plugins" folder (or next to GTAIV.exe).
  DriveByPreAim.ini and DriveByPreAim.log are created next to it on first run.
  To uninstall, delete those three files.

SETTINGS (DriveByPreAim.ini, 1 = on, 0 = off)
  Enabled                   whole mod on/off
  Controller_L1Aim_R1Shoot  controller scheme
  Mouse_RightButtonAim      mouse scheme
  TargetsFlee               aimed-at drivers/pedestrians flee

TROUBLESHOOTING
  DriveByPreAim.log lists every game patch and whether it was applied or skipped.
  The mod checks the game code before every patch; if something doesn't match (wrong game
  version, or another mod changed that code) it skips that patch instead of risking a crash.
  If another mod already hooks the same drive-by function, this one chains onto it.
  Please include your log when reporting a problem.

CREDITS / HONESTY NOTE
  - Idea and approach from [original mod name] by [original author] ([link]), which is for the
    Complete Edition. No code from it is included: its compiled .asi was analysed to learn how it
    works, and the equivalent game code was found in 1.0.8.0 and patched from scratch.
  - Written with the help of Claude, an AI assistant by Anthropic (analysis, code, building).
    Tested in-game by [your name].
  - Built on zolika1351's iv-sdk: https://github.com/zolika1351/iv-sdk

LICENSE
  GPL-3.0 (see LICENSE.txt), because it is built on iv-sdk which is GPL-3.0.
  Full source is in the "source" folder.
