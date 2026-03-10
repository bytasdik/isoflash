# ⬡ ISO Writer — Linux USB Burner

A terminal-based GUI (TUI) application for writing Linux ISO images to USB drives.
Built in C++17 with no external dependencies — just a modern terminal.

---

## Features

- 🎨 **Full TUI interface** — tab-based panels, colour highlighting, real-time progress
- 🔍 **Auto-detects USB drives** via `/sys/block` — no guessing
- 📊 **Live progress bar** — MB/s speed, ETA, bytes written
- ✔ **ISO validation** — checks ISO 9660 magic bytes before writing
- 🛡 **Double-confirm dialog** — protects against accidental data loss
- ⚡ **4 MB chunk writes** — fast, efficient, with `O_SYNC` for safety
- 🔄 **Refresh drives** with F5 at any time

---

## Build

```bash
chmod +x build.sh
./build.sh
```

Requires only `g++` (C++17) and `pthread`. No GTK, Qt, or other libraries needed.

Or manually:
```bash
g++ -std=c++17 -O2 -o iso_writer iso_writer.cpp -lpthread
```

---

## Usage

```bash
sudo ./iso_writer
```

> **Root is required** to write to block devices (e.g. `/dev/sdb`).

### Keyboard controls

| Key | Action |
|-----|--------|
| `Tab` / `← →` | Switch panels |
| `↑ ↓` | Navigate drive list |
| `Enter` | Select / confirm |
| `F5` | Refresh USB drive list |
| `q` / `Esc` | Quit |

---

## Workflow

1. **ISO FILE tab** — press Enter, type the full path to your `.iso` file (e.g. `/home/user/ubuntu-24.04.iso`), press Enter to validate
2. **USB DRIVE tab** — use ↑↓ to select your USB drive, press F5 if it's not listed
3. **WRITE tab** — press Enter, confirm the destructive dialog, watch the progress bar

---

## Safety notes

- Only **removable USB devices** appear in the drive list (checked via `/sys/block/*/removable`)
- The confirmation dialog defaults to **NO** — you must explicitly select YES
- Writes use `O_SYNC` and `fsync()` to ensure data is fully flushed before completion

---

## Requirements

- Linux (kernel 2.6+)
- C++17 compiler (`g++` or `clang++`)
- `pthread`
- A terminal with 256-colour and UTF-8 support (xterm-256color, kitty, alacritty, etc.)

---

## CMake build (optional)

```bash
mkdir build && cd build
cmake ..
make
sudo make install   # installs to /usr/local/bin/iso_writer
```
