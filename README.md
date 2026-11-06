# FLASHWIN — Legacy Adobe Flash Player Port (FP32 + FP10)

FLASHWIN runs the **real** Adobe Flash Player on legacy Windows by wrapping it in
compatibility **proxy DLLs**. It is a preservation / retro-computing project: the
player's own code is unchanged; the proxies close the gaps between what the
player imports and what old Windows provides.

- **FP32** — Flash Player 32 (32.0.0.465), the last release.
- **FP10** — Flash Player 10 (10.3.183.90), for pre-SSE2 machines.

Supported: **Windows NT 3.1 / 3.5x / 4.0 / 2000 / XP / XP x64** and
**Windows 95 / 98 / ME**.

> This is distinct from the author's from-scratch SWF player ([RetroFlashPlayer](https://github.com/ages2001/RetroFlashPlayer)). FLASHWIN
> ports the genuine Adobe binaries.

## Package layout

```
BIN\SETUP\      FP10SET.EXE, FP32SET.EXE    installers
BIN\PORTABLE\   FP10PORT.EXE, FP32PORT.EXE  single-file portables
SRC\            all sources, README.TXT (full notes), build scripts, audits
```

All four EXEs carry a red Flash icon.

## Usage

**Portable** — no install, no registry change:

```
FP10PORT.EXE movie.swf        (or FP32PORT.EXE movie.swf, or File > Open)
```

Each portable unpacks the player plus all its proxy DLLs to an 8.3-safe scratch
folder (valid on NT 3.1 FAT), runs it, then deletes the folder.

**Installer** — `FP10SET.EXE` / `FP32SET.EXE` install files, Start-menu
shortcuts and a `.swf` association, and register an uninstaller. A reboot is
not required.

## SSE2 and WINSIMD (separate)

FP32 requires SSE2; FP10 uses SSE2 when available and falls back otherwise.
On NT 3.1/3.5x/4.0 and Windows 95/98/ME the OS does not preserve XMM across
threads, so an **SSE2 enabler must be installed separately — [WINSIMD](https://github.com/ages2001/WINSIMD)
recommended**. Windows 2000/XP (incl. XP x64) preserve XMM themselves and need
no driver. FLASHWIN neither bundles nor installs WINSIMD.

The About box CPU-mode line reflects the real runtime mode: FP32 shows `SSE2`
(it only runs with SSE2 active); FP10 shows `SSE2` or `non-SSE2` dynamically.

## How it works (brief)

- **Proxy DLLs** with 8.3 names (`F32K`, `FPU`, `FPCPU`, `FP10CPU`, `F32CD`, …)
  replace the system DLLs the player imports, so it loads the ported, gap-closed
  shims instead. The names are required, not placeholders.
- **FP9X.DLL** is a Unicode layer for Windows 9x: it implements ~160 `W` Win32
  functions (which 9x exports only as stubs) on top of the `A` functions, and
  provides `RtlInitUnicodeString`. It loads only on 9x (`GetVersion`), never on NT.
- **Unforwarding** (`SRC\UNFORWRD.PY`) turns forwarded exports into lazy stubs,
  because 9x cannot resolve forwarders.

See `SRC/README.TXT` for the complete design notes, per-OS verification results,
and revision history.

## Build

Build scripts are in `SRC\` (`BUILDF10.PY`, `BUILDF32.PY`, `BUILDREL.PY`, …);
they use clang/LLD, NASM and Python 3 with pefile. See `SRC/README.TXT`.

## License and third-party notice

The **FLASHWIN source** in `SRC\` (the compatibility proxies, the Win9x Unicode
layer, the installers, the build and audit tools — the author's original work)
is licensed under the **GNU General Public License v3.0**; see `LICENSE`.

**Adobe Flash Player is proprietary software owned by Adobe Systems Incorporated.**
It is **not** part of this project's source and is **not** covered by the GPL.
The license above applies only to the author's own port code, not to the Adobe
Flash Player binaries that the installers and portables wrap. Adobe Flash Player
reached end-of-life on 31 December 2020. You are responsible for ensuring you
have the right to use Adobe Flash Player in your jurisdiction and situation.
Flash, Flash Player and Adobe are trademarks of Adobe.

Author: ages2001.
