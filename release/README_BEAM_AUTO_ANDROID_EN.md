# Beam Auto Android — Installation and Launch

The release contains two ZIP archives:

- `beam_auto_android_probe_mod.zip` — the BeamNG mod;
- `beam_auto_android_media_runtime.zip` — native GUI, scrcpy video producer,
  runtime DLLs, bundled ADB/scrcpy and ImDisk installer.

Python is not required at runtime.

## 1. Requirements

- Windows x64;
- BeamNG.drive 0.40.1.0 or a compatible version;
- an Android phone connected over USB;
- USB debugging enabled and the RSA authorization accepted on the phone;
- ImDisk/ImDisk Toolkit installed, with `imdisk.exe` available in `PATH`.

ImDisk is used for RAM-backed frame staging. The ImDisk installer archive is
included in the runtime ZIP at `dependencies\ImDisk\ImDiskTk64.zip`, but it is
not run automatically because it installs a system driver.

To install ImDisk from the package:

1. Extract `dependencies\ImDisk\ImDiskTk64.zip` to a temporary directory.
2. Open the extracted `ImDiskTk20260615` directory.
3. Run `install.bat` as Administrator.
4. Restart the runtime GUI if it was already open.

Verify the installation:

```powershell
Get-Command imdisk.exe
```

## 2. Install the BeamNG mod

Extract `beam_auto_android_probe_mod.zip` into the BeamNG user mods directory,
for example:

```text
D:\BeamNGUSER\current\mods\
```

The ZIP already contains `mod_info.json`, `scripts/` and `lua/` at the correct
level. Do not add another wrapper directory around those files.

The mod uses a universal vehicle-relative ImGui carrier. It does not require
Vivace, JBeam nodes/beams/flexbodies or the stock multimedia screen.

## 3. Install the native runtime

Extract `beam_auto_android_media_runtime.zip` to a separate directory, for
example:

```text
D:\BeamAutoAndroidRuntime\
```

Keep the extracted directory structure intact. The GUI looks for bundled
scrcpy next to itself in `scrcpy-win64-v5.0.1\`.

## 4. Check ADB

From the runtime directory:

```powershell
.\scrcpy-win64-v5.0.1\adb.exe devices
```

The phone must be listed as `device`, not `unauthorized`.

## 5. Start the stream

1. Start BeamNG.drive and load a vehicle.
2. Run `beam_auto_android_media_gui.exe` as Administrator.
3. Check the ADB path, producer path, BeamNG config path and optional serial.
   The Lua mod creates the config handshake at `temp/beam_auto_android_frame_probe.json`
   when it is missing.
4. Leave `Scale` at `0.5` initially, or choose `0.25`, `0.75` or `1`.
5. Press `Start`.
6. If the screen does not appear immediately, press `Ctrl+L` once in BeamNG.

The video path is:

```text
scrcpy Matroska over a Windows named pipe → native FFmpeg demux/decode
→ JPEG → ImDisk RAM → BeamNG VFS → ImTextureHandler
→ vehicle-relative AddImageQuad
```

Neither video nor individual frames are written to a normal persistent directory.
Video projection was checked on BeamNG 0.40.1.0 with Nothing A059P / Android 16;
actual touch alignment after the coordinate-range fix still needs verification.

## 6. Rotate the phone screen

The `Rotate` button toggles the selected Android device between `0°` and `90°`:

- `Rotate 90` locks the device at 90 degrees;
- `Rotate 0` returns it to 0 degrees.

The command uses the ADB path and serial configured in the GUI. The Lua carrier
detects the new frame dimensions and updates its geometry and touch mapping.

To return Android to automatic rotation manually:

```powershell
.\scrcpy-win64-v5.0.1\adb.exe -s SERIAL shell wm user-rotation free
```

## 7. Stop and restart

- Press `Stop` and wait for the `Stopped` status.
- Press `Start` again for a new session.
- Do not force-close the GUI during a normal stop.

The video producer uses scrcpy Matroska over a named pipe; audio runs through a
separate scrcpy instance. Press `Stop` and wait for cleanup before starting a new
session.

If the GUI was already running before an update, close it completely and launch
the new EXE. A running process does not load a replacement binary.

## 8. In-game controls

- `Debug` and `Move` appear when the mouse is moved into the activation zone
  slightly above the phone's top edge.
- `Move` enables mouse placement editing.
- In Move mode, the mouse wheel changes size and `Shift + wheel` changes depth.
- Double-clicking `Move` resets placement for the current vehicle.
- `Hide` removes the phone carrier, texture and touch input.
- When hidden, hover the same zone and press `Show` to restore it.
- `Debug` opens the touch/frame/queue diagnostics only when requested.

Placement is saved per vehicle.

## 9. Troubleshooting

```powershell
.\scrcpy-win64-v5.0.1\adb.exe devices
Get-Command imdisk.exe
```

If Start stops immediately:

1. confirm the phone is listed as `device`;
2. close any older GUI instance;
3. wait for `Stop` to finish before pressing `Start` again;
4. confirm that `Producer EXE` points to the producer from this package;
5. reconnect USB and accept the RSA prompt if necessary.

The producer treats a textual Android error or a stream that ends before the
first frame as an error, rather than reporting a false successful stop.
