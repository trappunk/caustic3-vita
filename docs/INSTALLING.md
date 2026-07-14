# Installing Caustic 3 Vita

This guide covers installing the ready-to-use VPK on a homebrew-enabled
PlayStation Vita. End users do not need to build anything or place an APK in a
data folder. It does not cover modifying a stock Vita; use the maintained
[Vita Hacks Guide](https://vita.hacks.guide/) if your system is not already
prepared for homebrew.

## 1. Download Caustic from its developer

Caustic 3 Vita's primary enhanced build is based on
`Caustic_3.2.2_64b.apk`, the most recent official Android maintenance build
published by Rej Poirier / Single Cell Software. Obtain that exact upstream
version from the developer's website:

- [Official Caustic 3 download page](https://www.singlecellsoftware.com/caustic3.html)
- [Direct official `Caustic_3.2.2_64b.apk` download](https://www.singlecellsoftware.com/download/Caustic_3.2.2_64b.apk)

The currently supported official APK has this SHA-256:

```text
7cf80508530e041821ab04693c6fc7bbd1fcd4c598cef825d3fd212b568ebf
```

The complete VPK Release already packages this supported version. The links
are provided for attribution, reference, and developers performing their own
source build. The build script checks this value and also verifies the embedded
ARMv7 native library; unknown or modified APKs fail closed.

## 2. Prepare the Vita

You need:

- A homebrew-enabled Vita or Vita TV
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell/releases/latest)
- [`kubridge.skprx`](https://github.com/TheOfficialFloW/kubridge/releases/latest)
- [`fd_fix.skprx`](https://github.com/TheOfficialFloW/FdFix/releases/latest),
  unless rePatch is installed
- `libshacccg.suprx`, installed legally using
  [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D)
- Enough free space for the application and `ux0:/data/CAUSTIC3/`

Install `kubridge.skprx` and, when applicable, `fd_fix.skprx` through your
normal taiHEN plugin setup. A typical configuration places them under
`ur0:tai/` and adds the following lines under the existing `*KERNEL` section in
`ur0:tai/config.txt`:

```text
*KERNEL
ur0:tai/kubridge.skprx
ur0:tai/fd_fix.skprx
```

Do not create duplicate `*KERNEL` sections. Reboot the Vita after changing
kernel plugins.

**Do not install `fd_fix.skprx` when rePatch is installed.** rePatch already
provides the relevant behavior, and combining them can create a conflict.

## 3. Download the complete VPK

Open the project's [GitHub Releases](https://github.com/trappunk/caustic3-vita/releases)
page and download the `.vpk` attached to the latest stable release. You do not
need the repository source archive and you do not need to download or rename an
APK separately.

Compare the downloaded file against the SHA-256 printed in that release's
notes. A mismatch normally means the download or transfer was incomplete.

Developers who specifically want to reproduce or modify the wrapper can follow
[BUILDING.md](BUILDING.md). That is an optional developer workflow, not an
installation requirement.

## 4. Transfer and install the VPK

### USB with VitaShell

1. Connect the Vita to the computer with a USB cable.
2. Open VitaShell and press **Select** to start USB mode. If Select starts FTP,
   change VitaShell's Select-button setting to USB first.
3. Copy the downloaded Caustic 3 Vita VPK to a convenient location such as
   `ux0:/data/`.
4. End the USB connection and return to VitaShell's file browser.
5. Highlight the VPK, press **Cross**, and confirm installation.
6. Return to LiveArea and launch **Caustic 3 Vita**.

### FTP with VitaShell

1. Open VitaShell and press **Select** to display its FTP address.
2. Connect an FTP client on the same network to the displayed address.
3. Upload the VPK to `ux0:/data/`.
4. End FTP mode, select the VPK in VitaShell, press **Cross**, and confirm.

## 5. First launch

- Keep the Vita awake while Caustic prepares its data directory. Initial
  content extraction can take noticeably longer than later launches.
- Do not interrupt the first launch merely because extraction appears slow.
- Touch controls are enabled by default. Press **Triangle** to toggle the
  optional physical-control mode.
- The bundled skin collection is Vita-normalized and hardware-tested. Skins
  added separately by users may still contain incompatible Android font-scale
  overrides.

## Updating an existing installation

Installing a newer VPK with the same title ID normally updates the application
without requiring an uninstall. Before updating, back up:

```text
ux0:/data/CAUSTIC3/
```

That directory contains projects, presets, samples, skins, and diagnostics.
Do not delete it unless you intentionally want a clean reset.

## Troubleshooting

- **Returns to LiveArea immediately:** verify `kubridge.skprx`, the applicable
  `fd_fix`/rePatch setup, `libshacccg.suprx`, and the taiHEN configuration,
  then reboot.
- **Installation error:** copy the VPK again and confirm the transfer completed;
  an interrupted USB/FTP copy can leave a truncated package.
- **Very slow VPK installation:** use the normal compressed Vita VPK, keep the
  system awake, and ensure the storage device has sufficient free space.
- **Missing text in a separately added skin:** switch back to a bundled skin;
  the added skin may contain Android-specific BMFont or GLF scale overrides.
- **Crash or unexpected behavior:** attach the latest log from:

  ```text
  ux0:/data/CAUSTIC3/debug.log
  ```

When reporting a problem, include the wrapper version, Vita model, storage
setup, installed plugins, and the exact action that triggered it.
