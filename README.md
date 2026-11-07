# SenkuVim

[![SenkuVim CI](https://github.com/islamMendjel/SenkuVim/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/islamMendjel/SenkuVim/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/islamMendjel/SenkuVim?color=00ff9f&logo=github)](https://github.com/islamMendjel/SenkuVim/releases)
[![C17](https://img.shields.io/badge/standard-C17-blue.svg)](https://en.wikipedia.org/wiki/C17_(C_standard_revision))
[![License: MIT](https://img.shields.io/badge/License-MIT-9775fa.svg)](LICENSE)

![SenkuVim Demo](assets/demo.gif)

<div align="center">

```
  ____                  _          __     ___           
 / ___|  ___ _ __  _  _| | ___   __\ \   / (_)_ __ ___  
 \___ \ / _ \ '_ \| |/| | |/ / | | | | / /| | '_ ` _ \ 
  ___) |  __/ | | | /  | |   <| |_| | | V / | | | | | |
 |____/ \___|_| |_|_/   |_|_|\_\\__,_| |_/  |_|_| |_| |
```

**A zero-dependency modal terminal IDE · Pure C17 · POSIX · Arch Linux**

*Dimension C-137 Rick Sanchez Aesthetics*

[![Language](https://img.shields.io/badge/Language-C17-blue?style=for-the-badge&logo=c)](https://en.wikipedia.org/wiki/C17_(C_standard_revision))
[![Platform](https://img.shields.io/badge/Platform-Arch%20Linux%20%2F%20POSIX-1793D1?style=for-the-badge&logo=arch-linux)](https://archlinux.org)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)
[![Version](https://img.shields.io/badge/Version-1.0.0-orange?style=for-the-badge)]()
[![Dependencies](https://img.shields.io/badge/Dependencies-Zero-brightgreen?style=for-the-badge)]()

</div>

---

## What is SenkuVim?

**SenkuVim** is a hand-crafted modal text editor and terminal IDE written from scratch in pure C17 — no ncurses, no external libraries, nothing but the POSIX libc and raw termios. It blends the modal efficiency of Vim with VS Code-style smart editing ergonomics, all wrapped in a Waybar-inspired 24-bit TrueColor UI themed around the Rick Sanchez Arch Linux rice palette.

Every character on screen is drawn through a double-buffered append buffer flushed in a single `write()` syscall. Every edit is backed by an immutable transaction-based Undo/Redo stack. Every line stores raw and rendered representations separately so tab expansion and syntax highlights never corrupt file bytes.

> Zero dependencies. Zero screen tearing. Zero memory leaks — verified under GCC AddressSanitizer and UndefinedBehaviorSanitizer.

---

## 🎨 The Rick Sanchez Arch Rice Palette

The color system is defined once in `senkuvim.h` as named 24-bit TrueColor ANSI macros. Every UI element maps to a palette role, never a raw hex string scattered through rendering code.

| Role | Color | Hex | Usage |
|---|---|---|---|
| **Ice Blue** | 🔵 | `#A2D2FF` | NORMAL mode pill · active line gutter · accent |
| **Portal Green** | 🟢 | `#52B788` | INSERT mode pill · string literals |
| **Amber** | 🟡 | `#FFD166` | COMMAND mode pill · numbers · search highlights |
| **Neon Pink-Red** | 🔴 | `#EF476F` | Unsaved changes indicator · errors |
| **Aqua Teal** | 🩵 | `#7AE5C6` | Type keywords · Rick's shirt accent |
| **Crisp White** | ⬜ | `#F4F6F8` | Main text · lab coat |
| **Slate Grey** | ⬛ | `#4A5568` | Muted gutter · inactive separators |
| **Canvas** | ⚫ | `#000000` | Transparent terminal background (zero black-box artifacts) |

The status bar renders Nerd Font rounded pill glyphs (`\ue0b6` / `\ue0b4`) Waybar-style, with per-mode color switching in real time. No black box around the pills — true transparent terminal canvas.

---

## ✨ Features

### 🔲 Modal Editing

Three modes, each with a distinct colored pill in the status bar:

- **NORMAL** — Vim-style navigation and operations (Ice Blue pill)
- **INSERT** — Direct typing into the buffer (Portal Green pill)
- **COMMAND** — Ex-command prompt at the bottom (Amber pill)

### 🧠 Smart IDE Ergonomics

- **Smart Tabs** — `Tab` inserts real 4-space tab stops aligned to the next column, never a literal `\t` in the buffer
- **Soft-Tab Unindent** — `Backspace` strips up to 4 leading spaces at once, matching the tab stop grid
- **Intelligent Auto-Indent** — `Enter` preserves the current block's indentation level; typing `{` and pressing `Enter` automatically expands a multi-line indented brace block
- **Auto-Pairs** — Real-time auto-closing for `()`, `[]`, `{}`, `""`, and `''`; `Backspace` deletes both characters atomically when the cursor is between a pair

### 📜 Immutable Undo / Redo Stack

A linked-list transaction stack stores fine-grained delta records for every edit type:

| Transaction | What it captures |
|---|---|
| `UNDO_INSERT_CHAR` | Character inserted at cursor |
| `UNDO_DELETE_CHAR` | Character removed |
| `UNDO_INSERT_STRING` | String inserted (smart tab, auto-indent) |
| `UNDO_DELETE_STRING` | String removed (soft-tab backspace) |
| `UNDO_SPLIT_LINE` | Line broken on `Enter` |
| `UNDO_JOIN_LINE` | Line joined on `Backspace` at column 0 |
| `UNDO_DELETE_LINE` | Full line removed (`dd`) |
| `UNDO_PASTE_LINE` | Line pasted below (`p`) |

Each record stores the cursor position before the edit, so undo teleports the cursor back exactly.

### 🎨 FSM Syntax Highlighting

An asynchronous Finite State Machine tokenizer runs per-row, tracking multi-line comment state across the whole file. Supported languages:

- **C / C++** — keywords, type names, string literals, single/multi-line comments, hex/float numbers, bracket matching
- **Python** — keywords, types, `#` comments, strings
- **Shell scripts** — keywords, `#` comments

Highlight categories: `HL_KEYWORD` · `HL_TYPE` · `HL_STRING` · `HL_NUMBER` · `HL_COMMENT` · `HL_MLCOMMENT` · `HL_MATCH` · `HL_BRACKET`

Live incremental search highlights all matches in real time as you type.

### 🖥️ Double-Buffered Rendering

All draw operations collect into an in-memory append buffer (`abuf`) and flush to the terminal in a single `write()` syscall. No partial draws. No screen flicker. `SIGWINCH` handling resizes the editor live when the terminal window changes.

### 💾 Raw / Render Row Separation

Each line (`erow`) stores two representations:
- `chars` — the pristine file bytes, never touched by display logic
- `render` — tab-expanded visual output with per-byte syntax highlight tags (`hl[]`)

Tab offsets and UTF-8 character positions never corrupt stored data.

---

## ⌨️ Keybindings

### Normal Mode

| Key | Action |
|---|---|
| `h` `j` `k` `l` / Arrows | Move cursor left / down / up / right |
| `w` / `b` | Jump word forward / backward |
| `0` / `$` | Start of line / end of line |
| `gg` / `G` | Jump to top / bottom of file |
| `i` | Insert before cursor |
| `a` | Append after cursor |
| `o` | Open new line below and enter Insert |
| `x` | Delete character under cursor |
| `dd` | Cut current line |
| `yy` | Yank (copy) current line |
| `p` | Paste yanked line below |
| `u` | Undo last transaction |
| `Ctrl+R` | Redo undone transaction |
| `/` | Enter live incremental search |
| `:` | Enter Command mode |
| `Ctrl+S` | Save without leaving Normal mode |
| `Ctrl+Q` | Safe quit (warns on unsaved changes) |

### Insert Mode

| Key | Action |
|---|---|
| `Esc` | Return to Normal mode |
| `Tab` | Insert 4 spaces to next tab stop |
| `Backspace` | Soft-tab unindent on leading indent; atomic pair deletion |
| `Enter` | Smart auto-indent with brace block expansion |
| `Ctrl+S` | Save without leaving Insert mode |
| `Ctrl+Z` | Undo last edit directly in Insert mode |

### Command Mode

| Command | Action |
|---|---|
| `:w` | Write (save) buffer to current file |
| `:w <filename>` | Save to a new filename |
| `:q` | Quit (blocked if unsaved changes exist) |
| `:q!` | Force quit, discarding changes |
| `:wq` | Save and quit |
| `:<number>` | Jump to line number (e.g. `:42`) |

---

## 🏗️ Architecture

```
SenkuVim/
├── senkuvim.h       # Master header — palette macros, all structs, all prototypes
├── main.c           # State machine · keypress router · live search · command parser
├── terminal.c       # POSIX termios raw mode · SIGWINCH · abuf flush · die()
├── buffer.c         # Dynamic erow array · row insert/delete · cx↔rx mapping · file I/O
├── ide_features.c   # Smart tab · soft unindent · auto-pairs · auto-indent · undo/redo stack
├── syntax.c         # FSM tokenizer · highlight update · bracket matcher · language detection
├── ui_rice.c        # Arch rice renderer · Waybar pill bars · gutter numbers · dashboard
├── Makefile         # -std=c17 -O2 · asan target · install to ~/.local/bin
└── test_senkuvim.py # PTY automation test suite — 10 core behavior checks
```

### Core Data Structures (from `senkuvim.h`)

**`EditorConfig E`** — the single global state struct:
```c
typedef struct EditorConfig {
    int cx, cy;              // Cursor position in file buffer
    int rx;                  // Rendered X (tab-aware)
    int rowoff, coloff;      // Scroll offsets
    int screenrows, screencols;
    int numrows;
    erow *row;               // Dynamic line array
    int dirty;               // Unsaved changes counter
    char *filename;
    EditorMode mode;         // Normal / Insert / Command
    SyntaxDefinition *syntax;
    struct termios orig_termios;
    UndoAction *undo_stack;
    UndoAction *redo_stack;
    char *clipboard;         // yy / dd / p buffer
    char *search_query;      // Live search string
    char pending_key;        // First key of two-key combos (dd, yy, gg)
} EditorConfig;
```

**`erow`** — a single file line:
```c
typedef struct erow {
    int idx;              // 0-based row index
    char *chars;          // Raw file bytes
    char *render;         // Tab-expanded display text
    unsigned char *hl;    // Per-byte highlight enum array
    int hl_open_comment;  // Multi-line comment state carried forward
} erow;
```

**`UndoAction`** — a transaction stack node:
```c
typedef struct UndoAction {
    UndoType type;         // What kind of edit
    int cx, cy;            // Cursor before edit
    char *data;            // Snapshot of changed text
    size_t len;
    struct UndoAction *next;
} UndoAction;
```

---

## 🔨 Build & Install

### Requirements

- Arch Linux or any POSIX-compliant Linux distribution
- GCC with ISO C17 support (`gcc -std=c17`)
- GNU `make`
- A terminal emulator with **24-bit TrueColor** and **Nerd Font** support
  - Recommended: Kitty · Alacritty · Foot · WezTerm

### Commands

```bash
curl -sL https://github.com/islamMendjel/SenkuVim/releases/latest/download/senkuvim-linux-x86_64.tar.gz | tar -xz -C ~/.local/bin/ senkuvim
```

### Usage

```bash
# Open a file
senkuvim main.c

# Or open empty buffer
senkuvim
```

---

## 🧪 Test Suite

`test_senkuvim.py` drives the editor through a PTY using Python's `pty` module, automating real terminal input sequences and asserting on output. It verifies all 10 core behaviors:

1. File open and initial render
2. Normal mode navigation (`h j k l w b 0 $`)
3. Insert mode character input
4. Smart Tab insertion (4-space alignment)
5. Soft-Tab Backspace unindent
6. Auto-pair insertion and atomic deletion
7. Auto-indent with brace block expansion
8. Undo / Redo stack correctness
9. Yank (`yy`), cut (`dd`), and paste (`p`)
10. Save (`:w`) and quit (`:q`, `:wq`)

```bash
# Run directly
python3 test_senkuvim.py

# Or through Make
make test
```

---

## 🖥️ Terminal Setup

For the full Waybar pill aesthetic and correct Nerd Font glyph rendering:

**Font** — Any Nerd Font patched font works. Recommended:
```
JetBrainsMono Nerd Font
CaskaydiaCove Nerd Font
Iosevka Nerd Font
```

**Kitty config example:**
```ini
font_family      JetBrainsMono Nerd Font Mono
font_size        13.0
background       #000000
background_opacity 0.95
```

**Alacritty config example:**
```yaml
colors:
  primary:
    background: '#000000'
font:
  normal:
    family: "JetBrainsMono Nerd Font Mono"
  size: 13.0
```

The editor auto-detects terminal dimensions via `ioctl(TIOCGWINSZ)` and responds live to `SIGWINCH` resize events.

---

## 🛡️ Memory Safety

SenkuVim is built with memory correctness as a first-class constraint:

- **AddressSanitizer verified** — `make asan` runs the full binary under `-fsanitize=address,undefined`; zero leaks, zero out-of-bounds accesses
- **Raw/render separation** — `erow.chars` is never aliased into display logic; tab expansion and syntax highlight bytes land in `erow.render` and `erow.hl` only
- **Double-buffered rendering** — the `abuf` append buffer grows via `realloc` and flushes atomically, preventing PTY truncation on large screens
- **Graceful `die()`** — terminal raw mode is always restored via `atexit(disableRawMode)` before the process exits, even on fatal errors

---

## 📄 License

MIT License — crafted for Arch Linux power users. See [LICENSE](LICENSE) for the full text.

---

<div align="center">

Built from scratch by [islamMendjel](https://github.com/islamMendjel)

*Pure C17 · Zero dependencies · Dimension C-137 aesthetics*

</div>
