# Beam Auto Android

Experimental real-time Android smartphone mirroring for BeamNG.drive.

The project displays the Android screen as a vehicle-relative perspective
projection inside BeamNG.drive and provides one-pointer mouse input, Android
rotation handling and an experimental spatial audio path.

## Current architecture

```text
Android
  -> ADB exec-out screenrecord
  -> H.264
  -> native C++ FFmpeg decoder
  -> JPEG
  -> ImDisk RAM-backed staging
  -> BeamNG VFS
  -> ImTextureHandler
  -> vehicle-relative ImGui AddImageQuad
```

The runtime does not require Python. The BeamNG Lua mod and the native runtime
are distributed separately.

## Repository layout

- `prototype/android_probe_mod/` — Lua mod, native C/C++ sources and tools;
- `release/` — ready-to-use ZIP packages and documentation;
- `scrcpy-win64-v5.0.1/` — bundled ADB/scrcpy release files;
- `docs/` — project research, architecture notes and implementation memory.

## Releases

- [BeamNG mod](release/beam_auto_android_probe_mod.zip)
- [Native media runtime](release/beam_auto_android_media_runtime.zip)
- [Русская инструкция](release/README_BEAM_AUTO_ANDROID_RU.md)
- [English instructions](release/README_BEAM_AUTO_ANDROID_EN.md)

The runtime package includes the native GUI, C++ producer, required runtime
DLLs, bundled ADB/scrcpy and the ImDisk installer archive. ImDisk must be
installed manually with administrator privileges.

## Quick start

1. Install ImDisk from `dependencies\ImDisk\ImDiskTk64.zip` inside the runtime
   package.
2. Extract the BeamNG mod into the BeamNG user `mods` directory.
3. Extract the native runtime into a separate directory.
4. Enable Android USB debugging and accept the RSA authorization prompt.
5. Check the device with:

   ```powershell
   .\scrcpy-win64-v5.0.1\adb.exe devices
   ```

6. Start `beam_auto_android_media_gui.exe` and press `Start`.

See the Russian or English README for the complete installation, rotation,
touch, Hide/Show, Stop/Start and troubleshooting instructions.

## Status and scope

This is an experimental proof-of-concept. The current target is Windows x64
and BeamNG.drive 0.39.4.0 or a compatible version. Compatibility with every
vehicle and Android device has not been established.

The project is not tied to the stock Vivace multimedia screen and does not add
physical JBeam nodes, beams or flexbodies to the vehicle.
