# Blades of Fury PS Vita — Release v1.0.0

**Title ID:** `PSVBOF001`  
**Target Platform:** PlayStation Vita / PlayStation TV  
**Base Game:** *Blades of Fury* (Gameloft, 2009) — iOS version 1.1.3  
**Status:** Fully Playable / In-Game 3D / Complete Audio & Controls  

---

## ⚔️ Introduction

This is the initial public release (**v1.0.0**) of **Blades of Fury** for the PlayStation Vita and PlayStation TV.

*Blades of Fury* is Gameloft's acclaimed 3D weapon-based fighting game, heavily inspired by arcade classics such as *Soulcalibur*. It features 10 distinct warriors, deep combat movesets, armor destruction mechanics, combo chains, special magic attacks, and multiple game modes (Story, Arcade, Survival, and Practice).

This port brings the game to the PS Vita via a custom 32-bit Mach-O loader that runs the original ARMv6 binary natively on the Vita's ARM Cortex-A9 processor, complete with full widescreen rendering, physical button mapping, and hardware-accelerated audio.

---

## 🌟 Features & Highlights

- **Native Execution (No Emulation):** Directly runs the original decrypted 32-bit iOS ARMv6 Mach-O executable on the PS Vita CPU.
- **Widescreen 960x544 Display:** Custom viewport scaling and presentation blit utilizing the full 960x544 Vita screen with zero letterboxing or black bars.
- **Dual Input Subsystem:**
  - Full physical controls: Left Analog Stick, D-Pad, Face Buttons ($\square$, $\Delta$, $\bigcirc$, $\times$), and Shoulder Triggers (L1 / R1).
  - Front capacitive touchscreen support for all menus, character selection, and dialogs.
- **VFP Vector Mode Replacement:** Legacy ARMv6 hardware vector math (`FPSCR.LEN = 3`) has been dynamically hooked and replaced with scalar math, eliminating hardware illegal instruction exceptions on Cortex-A9.
- **Complete Sound System:**
  - **SFX & Character Voices:** Integrated RIFF/WAVE parser connecting directly to OpenAL PCM buffers.
  - **Background Music (BGM):** Dedicated OpenAL music player streaming high-quality 32 kHz stereo audio across all menus and battle arenas.
- **Runtime Apple CgBI PNG Decoding:** On-the-fly decompression and BGR-to-RGB byte-swapping for all game textures.
- **LiveArea Integration:** Custom icon, startup gate, and background graphics.

---

## 🎮 Controls

| Button | Combat Action | Menu Action |
| :--- | :--- | :--- |
| **Left Analog Stick** | 8-Way Character Movement | — |
| **D-Pad (Cruceta)** | 8-Way Character Movement | Menu Navigation |
| **$\square$ (Square)** | Horizontal Slash (Attack A) | — |
| **$\Delta$ (Triangle)** | Vertical Slash (Attack B) | — |
| **$\bigcirc$ (Circle) / R1** | Magic / Special / Kick (Attack C) | Confirm |
| **$\times$ (Cross) / L1** | Guard / Block / Defense (Attack D) | Cancel / Back |
| **START** | Pause Combat Menu | Pause |
| **Touchscreen** | Menus, Selectors, Dialogs | Direct 1:1 Touch |

---

## 📋 Installation Requirements

Make sure your PS Vita has Henkaku / Enso installed with the following plugins active in `ur0:tai/config.txt`:

```ini
*KERNEL
ur0:tai/kubridge.skprx
ur0:tai/fd_fix.skprx
```

- **kubridge.skprx** (v0.8.1 or higher)
- **fd_fix.skprx** (or rePatch)
- **libshacccg.suprx** (Extract using [ShaRKBR33D](https://github.com/OsirizX/ShaRKBR33D))

---

## 📦 How to Install

1. Download and install **`bladesoffury.vpk`** on your PS Vita using VitaShell.
2. Obtain a legal, decrypted copy of *Blades of Fury* for iOS (v1.1.3 `.ipa`).
3. Extract the IPA file (rename to `.zip` and extract).
4. Inside `Payload/`, locate the `BladesOfFury.app` folder.
5. On your computer, run the staging tool to generate the relocation table and convert BGM tracks:
   ```bash
   tools/stage_data.sh path/to/BladesOfFury.app
   ```
6. Copy the contents of `ux0_data/bladesoffury/` to `ux0:data/bladesoffury/` on your PS Vita via FTP or USB.
   
   The directory on your console should look like this:
   ```
   ux0:data/bladesoffury/
     ├── BladesOfFury.app/
     │     ├── BladesOfFury
     │     ├── Info.plist
     │     ├── *.bar, *.epk
     │     ├── sfx_*.wav
     │     ├── m_*.wav
     │     └── ...
     ├── rebase.bin
     ├── saves/
     └── logs/
   ```
7. Launch **Blades of Fury** from your LiveArea and enjoy!

---

## 🛠️ Release Artifacts

- **`bladesoffury.vpk`**: Standalone PlayStation Vita installable package (`PSVBOF001`).
- **`eboot.bin`**: Standalone executable SELF for quick deployment via FTP.

---

## 👥 Credits

- **Gameloft:** Original game developer and publisher.
- **Rinnegatamante:** Author of `vitaGL` and core contributor to the Vita homebrew ecosystem.
- **TheFloW:** Author of `kubridge` and `fd_fix`.
- **Wonder Diaz:** Port developer (loader, ABI layer, shaders, audio, controls, and optimization).
