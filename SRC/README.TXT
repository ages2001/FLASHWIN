LEGACY FLASH PLAYER PORT - FLASHWIN  (FP32 + FP10)
================================================
PACKAGE LAYOUT (this revision)
------------------------------
  BIN\SETUP\     FP10SET.EXE, FP32SET.EXE   - installers
  BIN\PORTABLE\  FP10PORT.EXE, FP32PORT.EXE - single-file portables
  SRC\           all sources + README + EVIDENCE + audits + manifests
All four EXEs carry a red-Flash icon.

PORTABLE (single .exe)
----------------------
FP10PORT.EXE / FP32PORT.EXE each pack the player plus ALL its compatibility
proxy DLLs inside one EXE.  On run they unpack to a fresh scratch folder
(8.3-safe name, valid on NT 3.1 FAT), launch the player (pass a .swf as an
argument, e.g. FP10PORT.EXE movie.swf, or use File > Open), wait, then delete
the scratch folder.  Nothing is installed; no registry change.

WINSIMD (SSE2 enabler) IS SEPARATE
----------------------------------
This package does not include or install WINSIMD.  On NT 3.1/3.5x/4.0 and
Windows 95/98/ME, FP32 (and FP10 when it uses the SSE2 fast path) needs an SSE2
enabler installed separately (WINSIMD recommended).  NT 2000/XP preserve SSE2
themselves and need no driver.

WHY THE PROXY DLLs HAVE NAMES LIKE F32K / FPU / FPCPU
----------------------------------------------------
These are REQUIRED compatibility-proxy names, not placeholders.  The player
imports F32K instead of kernel32, F32U/FPU instead of user32, FPCPU/FP10CPU for
the SSE2 gate, F32CD/FPCD for the comdlg32 Browse shim, etc. (F32 = FP32 edition;
the trailing letters = the system DLL being shimmed).  They are renamed on
purpose so the player loads the ported, gap-closed shim instead of the real
system DLL - and NT 3.1's FAT allows only 8.3 names, so long descriptive names
are impossible.  Renaming them to the real system names would disable the whole
compatibility layer.

ABOUT BOX / CPU MODE
--------------------
The player's About box CPU-mode line now reflects reality, and no longer carries
the Win3.1x/Win32s line or the "Experimental development build" line:
  * FP32: shows "SSE2" (FP32 runs only when SSE2 is active - its startup gate
    blocks it otherwise, so when it is running, SSE2 is on).
  * FP10: shows "SSE2" or "non-SSE2" DYNAMICALLY, from the actual CPU-mode gate
    (FP10CPU.FpSse2Enabled): SSE2 when an SSE2 CPU + a working XMM-preservation
    path (WINSIMD on legacy, or NT 2000/XP) is present, else non-SSE2.  The label
    is the real runtime mode, not a fixed string.
FP10 is still the SSE2-free-capable edition: it RUNS without SSE2; when SSE2 is
safely available it uses it (and the About then says SSE2).

INSTALLER: SWF ASSOCIATION TAKE-OVER
------------------------------------
Setup now takes over the .swf association even if it is already owned (by a
previous install of this player, or another program) instead of refusing; the
previous owner is still saved for uninstall restore.

REVISION 2026-11-04 (2) - WINSIMD 1.2 SUPPORT
-------------------------------------------
* The FP32 SSE2 gate (FPCPU.DLL) and the FP10 CPU-mode gate (FP10CPU.DLL) now
  accept WINSIMD 1.2. WINSIMD 1.2 preserves the XMM registers itself on
  Windows NT 3.1/3.5x/4.0, Windows 95 and Windows 98 first edition. XMMGUARD
  and LOADXMM.EXE are no longer needed.
  How the gates detect it:
    NT  the volatile key HKLM\SYSTEM\CurrentControlSet\Services\winsimd\
        Runtime, value GuardFlags, must have bits 1+2 set;
    9x  DeviceIoControl(\\.\WINSIMD, 2).
  The older exact-kernel XMMGUARD query is still accepted.
* Windows 98 is now checked by build: 98 SE (4.10.2222) and ME save XMM
  themselves. 98 first edition needs WINSIMD 1.2, like Windows 95.
* Verified:
    NT 3.1          FP32 runs with WINSIMD 1.2 alone.
    NT 3.1, 2 CPUs  FP32 + Super Mario 63, 4 minutes of random input, no fault.
    NT 4.0          FP32 plays with WINSIMD 1.2 alone.
    Windows 95 RTM  FP32 runs with WINSIMD.VXD 1.2.
                    FP9X now answers GetModuleHandle("ntdll.dll") (95 has no
                    NTDLL), so FP32's unchecked RtlInitUnicodeString call
                    works there too.
                    FP10 About reports SSE2 mode with WINSIMD.VXD 1.2.
* The SSE2 message now says: install WINSIMD 1.2 or later, or use Flash
  Player 10.
WINSIMD itself is still not bundled or installed by these packages.

REVISION 2026-11-04 - WINDOWS 95 / 98 / ME
------------------------------------------
Both players now run on Windows 95, 98 and ME in addition to Windows NT.
Verified in virtual machines (QEMU):
  Windows 98 SE  FP10 + FP32: World's Hardest Game, Tetris, menus, About,
                 File > Open and Browse, mouse and keyboard, SharedObject
                 saves (runs=2 on the second start). FP10PORT/FP32PORT
                 portables run. FP10SET installs (files, shortcuts, .swf
                 association) and uninstalls (removal of UNINST.EXE is
                 scheduled in WININIT.INI).
  Windows 95 RTM FP10 (non-SSE2 path): game and SharedObject saves work.
                 FP32: without WINSIMD 1.2 it shows its SSE2 message and
                 closes; with WINSIMD.VXD 1.2 it runs (see above).
  Windows ME     FP10 + FP32 in normal mode and in Safe Mode (standard VGA,
                 16 colours, 640x480).
  Regression     The same files run on Windows NT 3.1 (FP10, and FP32 with
                 XMMGUARD) and Windows XP x64 (FP10 + FP32).
Not tested: Windows 98 first edition. Its kernel does not save SSE registers,
so FP32 will show its SSE2 message there.

How it works
  1. FP9X.DLL (new, SRC\FP9X.C) is a Unicode layer for Windows 9x. The
     players call the Unicode ("W") Win32 API, which Windows 9x exports only
     as non-working stubs. FP9X implements about 160 W functions on top of
     the ANSI functions. These include window classes and procedures
     (thunks convert WM_CHAR, WM_SETTEXT, WM_GETTEXT and WM_CREATE text),
     dialogs, menus, files, the registry, fonts, version resources,
     common dialogs and wave device names. It also provides
     RtlInitUnicodeString, which FP32 looks up in NTDLL and calls without
     checking: on 9x that call went to address 0 and locked the whole
     system.
     FP9X is loaded only when GetVersion() reports Windows 9x:
       * from FPK.DLL / F32K.DLL (entry point FpK32Entry);
       * from the FP32 launcher (SRC\PRELOAD9.PY adds a small prologue);
       * on first use by any proxy stub (below).
     When it loads, it rewrites the imports of the player and its proxies
     that point into system DLLs; proxies keep their own code. DLLs loaded
     later are patched too (hooked LoadLibrary*), and GetProcAddress
     lookups of system W functions are redirected.
     On Windows NT, FP9X.DLL is never loaded.
  2. Windows 9x does not resolve forwarded exports (e.g.
     "kernel32.CreateFileW"), so no proxy built from forwarders could load.
     SRC\UNFORWRD.PY turns every forwarder into a small stub that resolves
     its target on first call: FP9X first on 9x, otherwise the original
     target. On NT the result is the same function the forwarder named.
  3. Installer on 9x:
       * the uninstall shortcut uses the 8.3 folder name (the 9x shell
         cannot quote a command line given over DDE);
       * the uninstaller accepts being started through an 8.3 path;
       * removal of UNINST.EXE is scheduled through WININIT.INI (9x has no
         MoveFileEx);
       * the DDE timeout was raised from 10 s to 60 s.
  Diagnostics: create an empty file FP9X.LOG next to FP9X.DLL to get a trace
  of patched imports, API calls and dynamic lookups. Delete it for normal
  speed.
  Build: SRC\BUILD9X.PY (FP9X.DLL), SRC\MK9XSTG.PY (applies UNFORWRD.PY
  and PRELOAD9.PY to the verified NT file set), SRC\BUILDSET.PY (installer
  stubs).

Still open (not changed by this revision)
  * Windows 11: these builds have ASLR (DYNAMIC_BASE) turned off in FP10.EXE
    and APP\FP32.EXE, and include the F32K PATH fix. I could not test them on
    Windows 11 because no Windows 10/11 VM is available here. Please test.
  * Missing glyphs: I could not reproduce this on NT 3.1, 2000, XP x64, 95,
    98, ME or 16-colour VGA. I need a failing SWF, or your system's font list
    or VM image.

REVISION 2026-11-03 (evening) - FIXES IN THIS BUILD
---------------------------------------------------
1. Windows 2000 / XP start-up crash ("failed to initialize properly
   0xc0000005") - FIXED and verified on Windows 2000.
   FP10.EXE and APP\FP32.EXE had no IMAGE_DIRECTORY_ENTRY_IAT. The NT 5 loader
   then only unprotects the section holding the import directory and faults
   writing thunks in read-only .rdata (NT 3.1 and 9x do not enforce this).
   The IAT data directory is now set (IATFIX.PY); only header bytes change.
   Verified: FP10 and FP32 start, play SWFs, About shows SSE2, Browse works.
2. Browse: the returned path now has an upper-case drive letter
   ("D:\swf\x.swf", not "d:\..."). FPCD.DLL / F32CD.DLL (FPCD.C, COMDLGF.C).
3. FP10 saved games (the SM63 "first Shine impossible / passage closed"
   logic errors). On NT 3.1 FP10's SharedObject.getLocal() returned null, so
   the game could never read or write its save data. Three causes, all fixed
   (FP10SAVE.C, SHELL10.C, KERNEL10.C):
   a) Flash resolves shell32!SHGetFolderPathW with GetProcAddress; NT 3.x
      (and NT 4 / 95 without SHFOLDER) has none. FPK.DLL's GetProcAddress now
      hands out the FPS.DLL bridge, which uses a native SHGetFolderPath when
      present and otherwise <player folder>\UserData\<user SID pieces>
      (\Default on 9x).
   b) Flash names its storage folder with CryptGenRandom; NT 3.x has no
      CryptoAPI. A fallback random source is supplied only when the OS lacks
      it (not cryptographic; used for folder naming).
   c) NT 3.1 FAT has no long names. When the save volume is limited to 8.3,
      long Flash names (Macromedia, #SharedObjects, *.sol) are mapped to
      verified 8-character aliases (FP10MAP.DAT), as FP32 already did.
      Flash's dynamic kernel32!CreateDirectoryW is routed through the adapter.
   Verified on NT 3.1: write + flush=true, then read back every value after
   restarting the player (numbers, string, boolean, array, object, 300-item
   list), identical to FP32.
   Keep the whole UserData tree (including hidden FP10MAP.DAT/FP10LOCK.LCK)
   when backing up saves.
4. Portable saves: the portables unpack to a scratch folder that is deleted
   on exit, so saves now go next to the portable EXE: FP10DATA\ (FP10) and
   FP32DATA\ (FP32), or %TEMP% if that folder is read-only. Systems with a
   native AppData folder keep using it.
5. FPG.DLL no longer writes diagnostic files into C:\FP10 (TRACE.LOG lines and
   DIB200.BMP / DIB1000.BMP snapshots). Tracing is now opt-in (FP32_TRACE=1).
6. DBGRUN.EXE (DBGRUN.C): crash catcher for legacy Windows. Run
   "DBGRUN.EXE <player> <swf>"; it writes DBGLOG.TXT next to itself with every
   exception (module+offset, registers, stack), exit code and a memory/thread
   sample every 15 s. Use it to report a crash.

7. RANDOM CRASH AFTER A WHILE ON NT 3.1 (both players) - ROOT CAUSE FIXED.
   Reproduced on NT 3.1 + AC97 (AUDIONT) with SM63: the player closed itself
   after 4-5 minutes (exit code 128); every time Flash's memory manager
   (MMgc GCHeap) found its block table overwritten with sound sample data.
   XMMSTRS.EXE (TOOLS\XMMSTRS.EXE, source XMMSTRS.C) keeps patterns in
   XMM0-XMM6 in three threads and checks them without system calls:
     general WINSIMD driver only : 18% (no sound) / 27% (sound) of checks
                                   corrupted - XMM is NOT preserved
     WINSIMD XMMGUARD loaded     : 0 failures
   An earlier revision of this port accepted "WINSIMD service installed" as
   proof of XMM preservation. That was wrong: the general WINSIMD driver only
   ENABLES SSE on NT 3.x. The SSE2 gate (CPUFEAT.H, FP10CPU.C, CPUGATE.C) now
   accepts only: NT 2000/XP (OS support), Windows 98/ME (OS support), or a
   valid \\.\FpXmmGuard report (WINSIMD XMMGUARD, LOADXMM.EXE on NT 3.1).
   Effect:
     FP10: SSE2 only with XMMGUARD on NT 3.x; otherwise the safe non-SSE2
           (x87) mode, shown in Help > About.
     FP32: needs XMMGUARD on NT 3.x; without it, it refuses to start and
           says so (use FP10 instead).
   The missing-character symptom (both players, NT 3.1 only, appearing after
   a while) is consistent with the same XMM corruption; it could not be
   reproduced directly, so it is not claimed as verified.
8. NT 3.1 InterlockedIncrement/InterlockedDecrement return only the sign of
   the result. Both players' kernel proxies forwarded them; they now return
   the exact value (APCEMU.C).
9. QueueUserAPC (absent on NT 3.1/3.51) is emulated for its only use in
   Flash: waking a thread in SleepEx(..., TRUE) (APCEMU.C). Native APIs are
   used wherever they exist.
10. Windows 95/98/ME: NOT working. Tested on the supplied Windows 98 image
   (safe mode under QEMU): FP10 stops with "linked to missing export
   FPM.DLL:waveInOpen" because Windows 9x does not resolve the export
   forwarders the proxies use, and both players call Unicode (W) APIs that
   Windows 9x does not implement. Supporting 9x needs a Unicode layer and
   forwarder-free proxies; that is not done.

======================================================================
(Original gap-closure / fix notes follow.)

LEGACY FLASH PLAYER - FULL API GAP CLOSURE (FP32 + FP10)
========================================================
Date: 2026-11-03.  All runtime tests ran in QEMU on COPIES of the supplied
disk images; originals were never modified.  No missing API is ever reported
as a success (project integrity rule).

GOAL
----
Close every remaining API gap in both players - the FpMissing abort stubs that
terminate the player when the Flash engine calls an unimplemented import -
"except the impossible ones".  Closing a gap = removing the abort stub so the
player no longer crashes: either a real implementation, or a clean failure
(correct error / empty result) when the subsystem genuinely does not exist on
Windows NT 3.1.

RESULT
------
  Edition   Abort stubs before   Abort stubs after   Proxies rebuilt
  FP32      85 (64 mid-way)      0                   11
  FP10      49                   0                   10

Verified statically across ALL 18 proxies in each edition (AUDITF32.JSN,
AUDITF10.JSN).  The only intentionally non-functional function is QueueUserAPC
- the single "impossible" case (NT 3.1 has no user-mode APC-queue API).  It is
present and no longer an abort stub: returns 0 / ERROR_CALL_NOT_IMPLEMENTED so
the player continues instead of crashing.

RUNTIME VERIFICATION ON WINDOWS NT 3.1  (see EVIDENCE\)
------------------------------------------------------
  FP32 + Super Mario 63 (15 MB) -> interactive title screen   F32SM63.PNG
  FP32 + Tetris                 -> title / scoreboard          F32TETRS.PNG
  FP10 + Super Mario 63         -> interactive title screen    F10SM63.PNG
  FP10 + Bloxorz                -> title menu                  F10BLOX.PNG
  FP32 A/B control: gap-closed vs original proxy set load
                    identically                                F32ABGAP.PNG /
                                                               F32ABORG.PNG
FP32 also needs the WINSIMD XMM guard driver for SSE2 context preservation on
legacy NT / 9x.  FP32 setup does NOT install WINSIMD: it is a separate component
the user installs themselves (run WINSIMD.EXE from BIN\WINSIMD once - see "FP32
SSE2" below).  FP10 is the SSE2-free edition and needs no driver.

BROWSE BUTTON FIX (File > Open > Browse on NT 3.1)  [NEW]
--------------------------------------------------------
On NT 3.1 the player's "Browse..." button did nothing: the Flash engine passes
the newer 88-byte OPENFILENAME structure, and NT 3.1's 1993 comdlg32 rejects any
lStructSize other than the classic 76 bytes (CDERR_STRUCTSIZE), so
GetOpenFileName/GetSaveFileName failed silently and no file picker appeared.
Fix - a thin comdlg32 shim that clamps lStructSize to 76 for the duration of the
call (the struct layout is identical through lpTemplateName, so the real dialog
opens) and then restores it:
  * FP32: the engine already loads its comdlg proxy by the name F32CD.DLL, so
    F32CD.DLL is rebuilt FROM A FORWARDER INTO THE SHIM (GetOpenFileNameW /
    GetSaveFileNameW fixed; the rest forwarded to the real comdlg32).
  * FP10: FP10.EXE imported COMDLG32.dll DIRECTLY, and a same-named shadow DLL
    cannot win because the system comdlg32 is already resident (Program Manager
    loaded it) and NT binds by base name.  So FP10.EXE's import is renamed
    COMDLG32.dll -> FPCD.DLL (a unique name, exactly as the other FP10 proxies
    are renamed), and FPCD.DLL (all six comdlg entry points, A+W) ships beside
    it.  FPCD.DLL is therefore always loaded and forwards the rest to the real
    comdlg32.
Verified on NT 3.1: Browse now opens the file picker, returns the chosen path to
the player, and the movie loads - both editions (EVIDENCE\F32BROWS/F32BRPTH, F10BROWS/F10BRPTH/F10BRRUN).

HOW EACH GAP WAS CLOSED
-----------------------
Implemented for real (functional):
  kernel32 (F32K/FPK): EnumSystemLocalesW; waitable timers; timer-queue timers
    (FP32); fibers (FP32, real cooperative asm context switch).
  user32  (F32U/FPU): menu-item APIs onto classic NT 3.1 menu calls; SendInput
    onto mouse_event/keybd_event; ShowWindowAsync, FlashWindowEx,
    GetKeyboardLayout, EnumDisplaySettingsW, UpdateLayeredWindow.
  gdi32   (F32G/FPG): EnumFontFamiliesExW via EnumFontFamiliesW; GetICMProfileA.
  shell32 (F32S/FPS): SHGetFolderPathW/A from environment + system dirs;
    SHGetSpecialFolderLocation, SHGetPathFromIDListW forwarded when present.
  ole32   (F32O/FPO): PropVariantClear, ReleaseStgMedium direct; CoInitializeEx
    -> CoInitialize; clipboard / bind-context forwarded when present.
  mscms   (F32CM/FPCLR): identity colour pipeline (pass-through).
  ws2_32  (F32W/FPW): WSASocketA/W, getaddrinfo, freeaddrinfo, getnameinfo,
    WSAAddressToStringA over the classic WSOCK32 BSD-socket stack, now loaded
    DYNAMICALLY so these proxies carry NO static WSOCK32.DLL import (see below).
  urlmon  (F32R/FPR): CopyStgMedium.

Clean graceful failure (NON-FUNCTIONAL by design; subsystem absent on NT 3.1):
  QueueUserAPC - returns 0 / ERROR_CALL_NOT_IMPLEMENTED (the "impossible" case).
  crypt32 (F32C/FPC) - certificate-store / signed-message verification fails.
  wininet (F32I, FP32 only) - HTTP returns ERROR_INTERNET_CANNOT_CONNECT.
  dsound  (F32D/FPD) - DirectSoundCreate -> DSERR_NODRIVER, capture -> no
    devices.  Player falls back to the waveOut mixer.
  ws2_32 async (F32W/FPW) - WSAEventSelect, WSAEnumNetworkEvents, WSAIoctl
    return SOCKET_ERROR (Winsock-2 async event model absent).

BUILD METHOD
------------
All proxy objects: clang-19 -> lld-link-19 -> pad_legacy_pe.py (pad_image),
which flattens the PE so NT 3.1's 1993 loader maps it (VA==raw,
SizeOfImage==file size, section tails materialised).  clang-19 + pad is
mandatory; clang-18 output loads but malfunctions on NT 3.1.  Each proxy keeps
its original F32*/FP* library name (renaming the engine's imports breaks NT 3.1
startup).

INSTALLING ON WINDOWS NT 3.1
----------------------------
Two ways, both ship the gap-closed proxies:

1. Installer (fresh install).  Run BIN\SETUP\FP10SET.EXE or FP32SET.EXE from
   Program Manager / the Start menu (Run).  It installs the player + all
   gap-closed compatibility DLLs (default C:\FP10 / C:\FP32; the folder can be
   changed in the wizard), creates shortcuts and associates .swf.  The installer
   stub was recompiled from the original tested source WITH THE "Windows NT 3.1
   only" OS RESTRICTION REMOVED, so it now runs on other Windows versions too;
   its payload carries the gap-closed proxies and every file's CRC is
   re-verified before extracting.  Verified: FP10SET.EXE opens its wizard on
   Windows NT 4.0 (where the old build refused) - EVIDENCE\SETNT4.PNG - and
   installs end-to-end on NT 3.1 with the installed player rendering a game
   (EVIDENCE\SET10OK.PNG, SET10RUN.PNG).
   FP32 setup does NOT install or register WINSIMD.  WINSIMD is a SEPARATE
   component the user installs themselves: run WINSIMD.EXE from BIN\WINSIMD once
   (it registers the SIMD guard as an auto-start service / static VxD).  Only
   then does FP32 have its SSE2 context preservation on legacy NT / 9x.  (FP10
   needs no driver; NT 2000/XP preserve SSE2 themselves.)
   Note: the player binaries themselves were ported/validated against NT 3.1;
   removing the installer gate lets setup run elsewhere, but full player
   behaviour on other OSes is a separate, unfinished matter.

2. Portable (no install, no registry).  BIN\PORTABLE\FP10\ and
   BIN\PORTABLE\FP32\ are complete ready-to-run folders with the gap-closed
   proxies already in place - copy anywhere and run (FP32: run WINSIMD.EXE once
   for SSE2 on legacy NT / 9x).  See BIN\PORTABLE\READPORT.TXT.

3. Drop-in patch (existing install).  Copy BIN\FP32\* / BIN\FP10\* over the
   proxies in an existing C:\FP32 / C:\FP10 install.  BIN\FP32 adds the fixed
   F32CD.DLL (Browse fix).  BIN\FP10 adds FPCD.DLL AND the repatched FP10.EXE
   (its comdlg import was renamed to FPCD.DLL, so both must be copied together
   for the FP10 Browse fix).

INSTALLER BEHAVIOUR / FIXES
---------------------------
- OS restriction removed: setup runs on NT 3.1/3.5x/4/2000/XP and 9x (the
  welcome screen states the ported-to list).
- Default install folder is OS-aware: C:\FP10 / C:\FP32 on Windows NT 3.x (8.3
  shell); %ProgramFiles%\Flash Player 10 / 32 on NT 4.0+, 2000/XP and 9x.  The
  "no spaces" rule applies only to the 8.3 shell.
- SWF file-class registration falls back from HKEY_CLASSES_ROOT to
  HKEY_CURRENT_USER\Software\Classes when HKCR is not writable (e.g. a
  non-elevated account on modern Windows), so association no longer fails there.
- FP32 setup does NOT install, bundle or register WINSIMD.  WINSIMD is installed
  separately by the user (run WINSIMD.EXE from BIN\WINSIMD once).  There is NO
  "Enable SSE2 / Load SSE2 driver" shortcut.  Verified on NT 3.1: fresh FP32
  install carries no WINSIMD files, and FP32 then launches and renders a game
  because WINSIMD was installed separately beforehand - EVIDENCE\F32SEPW
  (welcome), F32SEPR.
- Setup wizard text no longer clips: the wizard window and its text area were
  enlarged so the full welcome/requirements text (including the last line) is
  visible, with the buttons below it - EVIDENCE\F32SEPW, SETRDY.
- Browse button fixed on NT 3.1 (OPENFILENAME lStructSize; see "BROWSE BUTTON
  FIX" above).  FP32 via the rebuilt F32CD.DLL; FP10 via FP10.EXE's comdlg import
  renamed to FPCD.DLL plus the FPCD.DLL shim.
- Program-group name no longer has the " - ages2001" suffix (just "Flash
  Player 10" / "Flash Player 32").
- Setup/uninstall icon is the real red Flash "f" with a green install arrow.

FP32 SSE2 (WINSIMD is installed separately) + GENERIC SSE2 GATE (FPCPU.DLL)
---------------------------------------------------------------------------
WINSIMD IS SEPARATE: the FP32 installer does NOT install or register WINSIMD.
The user installs it themselves - run WINSIMD.EXE from BIN\WINSIMD once, which
registers the SIMD guard as an auto-start kernel service (NT) or a static VxD
(Win9x).  FP32 setup only installs the player + its proxies and associates .swf;
it ships no WINSIMD files.  Verified on NT 3.1: a fresh FP32 install carries no
WINSIMD files and still runs because WINSIMD was installed separately beforehand
(EVIDENCE\F32SEPW, F32SEPR).  2000/XP and later need no driver at all.

GENERIC GATE: FP32's start-up gate used to demand a very specific driver: the
exact
\\.\FpXmmGuard query device AND one of two hard-coded NT 3.1 kernel timestamps.
That rejected NT 3.5x / NT 4.0 and any WINSIMD build that did not expose that
device.  The gate (FPCPU.DLL) is rebuilt to be GENERIC:
  * NT 2000 / XP (NT 5+): trust the OS's own IsProcessorFeaturePresent - no
    driver needed there.
  * NT 3.1 / 3.5x / 4.0: accept the \\.\FpXmmGuard query (kernel-timestamp
    whitelist removed, so any supported kernel works), OR, as a generic
    fallback, accept any installed WINSIMD (the "winsimd" NT service or the
    Win9x WINSIMD VxD) together with a hardware SSE2 bit.
  * Win9x: same generic fallback (WINSIMD VxD present + SSE2).
So installing WINSIMD once is enough for FP32 to run - on every supported OS,
with no \\.\FpXmmGuard requirement.  Verified on NT 3.1: FP32 launches both with
the query driver and with the generic WINSIMD 1.2 service (EVIDENCE\F32QUERY / F32GEN).
CAVEAT: the fallback trusts that an installed WINSIMD actually preserves XMM on
that kernel.  Whether a given WINSIMD build hooks a given kernel is WINSIMD's
job; if it enables SSE2 but does not preserve it, playback can be unstable.

OS / SSE2 SUPPORT MATRIX (FP32)
-------------------------------
  Windows NT 2000 / XP (x86)  : OS preserves XMM itself - no driver needed.
  Windows NT 3.1 / 3.5x / 4.0 : needs WINSIMD (guard preserves XMM/MXCSR).
  Windows 95 / 98 / ME        : needs WINSIMD VxD (guard preserves XMM/MXCSR).
  FP10 (SSE2-free edition)    : needs no SIMD driver on any OS.

WS2_32 / WINSOCK DEPENDENCY FIX
-------------------------------
The ws2_32 proxies (F32W / FPW) previously carried a STATIC import of
WSOCK32.DLL (present in both the original shipped build and the earlier
gap-closed build), which made the whole player fail to load on any host without
WinSock.  That static dependency is removed: the proxies now load WSOCK32 on
demand and import only KERNEL32, while networking still works where WinSock is
present.  Verified on NT 3.1 - FP10/FP32 still render (WHG, Tetris).  The
players are verified on Windows NT 3.1; runtime support beyond NT 3.1 is not
yet complete.

CONTENTS
--------
  BIN\SETUP\    FP10SET.EXE, FP32SET.EXE - installers carrying the gap-closed
                proxies, with the "NT 3.1 only" OS restriction removed
                (+ .JSN payload manifests)
  BIN\PORTABLE\ FP10\ and FP32\ - complete ready-to-run players (no install),
                gap-closed proxies included (+ READPORT.TXT)
  BIN\FP32\     11 gap-closed FP32 proxies + fixed F32CD.DLL (drop-in patch)
  BIN\FP10\     10 gap-closed FP10 proxies + FPCD.DLL + repatched FP10.EXE
                (drop-in patch; copy FPCD.DLL and FP10.EXE together)
  BIN\WINSIMD\  WINSIMD 1.2 - generic SSE2/XMM guard (i386\WINSIMD.SYS +
                WINSIMD.EXE + WINSINFO.EXE, 9x\WINSIMD.VXD, amd64\).  A SEPARATE
                component the user installs themselves (run WINSIMD.EXE once);
                FP32 setup does not install it.
  SRC\          gap implementation sources + both build scripts
  EVIDENCE\     screenshots: both players x multiple games on NT 3.1; FP32 A/B
                test; FP10SET wizard on NT 4.0 (SETNT4); FP10SET install + run
                on NT 3.1 (SET10OK / SET10RUN); Browse button fixed, both
                editions (F32BROWS/F10BROWS ...); FP32 install without WINSIMD +
                run with separately-installed WINSIMD (F32SEPW welcome, F32SEPR);
                setup text no longer clipped (F32SEPW, SETRDY)
  AUDITF32.JSN / AUDITF10.JSN   per-proxy export audit (implemented /
                                forwarded / remaining abort stubs)

SRC FILE MAP (8.3 name  <-  original)
-------------------------------------
  CRYPT.C    <- gap_crypt.c       OLE.C      <- gap_ole.c
  DSOUND.C   <- gap_dsound.c      SHELL.C    <- gap_shell.c
  FIBER.ASM  <- gap_fiber.asm     SHELL2.C   <- gap_shell2.c
  GDI.C      <- gap_gdi.c         SHELL3.C   <- gap_shell3.c
  KERNEL.C   <- gap_kernel.c      URLMON.C   <- gap_urlmon.c
  KERNEL2.C  <- gap_kernel2.c     USER.C     <- gap_user.c
  MSCMS.C    <- gap_mscms.c       USER2.C    <- gap_user2.c
  WININET.C  <- gap_wininet.c     WS2.C      <- gap_ws2.c
  WSKEVT.C   <- winsock_events_dyn.c (dynamic WSASetLastError for F32W/FPW)
  BUILDF32.PY<- buildgaps2.py     BUILDF10.PY<- buildfp10gaps.py
Installer / gate / Browse-fix sources (8.3 name  <-  purpose):
  SETUP.C    <- native NT 3.1 installer (OS gate removed; OS-aware default path;
                HKCU class fallback; does NOT install WINSIMD; enlarged wizard
                window + text area so setup text no longer clips)
  PRODUCT.H  <- per-edition install settings and strings
  WIZARD.H   <- setup wizard pages / welcome text
  MKICONS.PY <- builds the red-Flash + green-arrow setup icon
  CPUFEAT.H  <- generic FP32 SSE2 gate (no kernel-timestamp whitelist)
  CPUGATE.C  <- FPCPU.DLL gate entry
  COMDLGF.C  <- F32CD.DLL comdlg32 shim (FP32 Browse lStructSize fix)
  FPCD.C     <- FPCD.DLL comdlg32 shim (FP10 Browse lStructSize fix)
The two build scripts are reference (they carry the original flat gap_*.obj
names and absolute build paths); the files in BIN\ are the ready-to-use output.
