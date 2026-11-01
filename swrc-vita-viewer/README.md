# SWRC Vita level viewer (experimental)
Native VitaGL viewer for the user's reconstructed PC Republic Commando map. This is NOT the game engine.

This isolated build branch does not affect psp2-tooling's main branch.

The VPK contains an executable only. Without a scene file, a fallback colored triangle is displayed, proving basic rendering.
For the reconstructed level, copy GEO_01A_v9.swv from the SWRC v9 scene package to:

    ux0:data/SWRV00001/scene.swv

Create the SWRV00001 directory using VitaShell. No SDK installation is required on the player's PC.
The viewer also supports app0:scene.swv, so the precompiled VPK can be repackaged to include the scene.

Controls:
Left analog: move; Right analog: look; R: faster; Up/down: vertical movement; Start: reset; Circle: exit.
Log: ux0:data/SWRV00001-viewer.log

Limitations:
Only BSP and the Arena_HR static-mesh placements; no textures, dynamic actors, other mesh packages, gameplay or collision.
Geometry transforms have not been checked on Vita hardware.
Needs libshacccg.suprx on the Vita.

Build in GitHub Actions from branch swrc-vita-viewer-build. If successful, Actions artifacts contain swrc_vita_viewer.vpk.

Version 12 fixes the native app0 scene loading fallback and records every failed file path and error code. If the device still displays the fallback, read ux0:data/SWRV00001-viewer.log; VPK must contain scene.swv in its root.
