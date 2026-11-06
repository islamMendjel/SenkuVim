# SenkuVim

> **A zero-dependency, modal terminal IDE written in pure C17 for Arch Linux and POSIX systems, styled after Dimension C-137 Rick Sanchez aesthetics.**

SenkuVim blends the modal editing efficiency of Neovim with the ergonomics of modern graphical editors (VS Code-style smart tabs, auto-indentation, and auto-pairing). Built from the ground up without heavy external libraries, it features an immutable transaction-based Undo/Redo stack, an asynchronous FSM syntax highlighter, and a floating Waybar-inspired Nerd Font pill status line with true 24-bit TrueColor canvas transparency.

---

## Features

- **Rick Sanchez Arch Rice Palette (24-bit TrueColor):**
  - Waybar-styled rounded pills (`\ue0b6` and `\ue0b4`) with dynamic mode indication:
    - `NORMAL`: Rick Hair Ice Blue (`#A2D2FF`)
    - `INSERT`: Portal Toxic Green (`#52B788`)
    - `COMMAND`: Amber Yellow (`#FFD166`)
  - Lab Coat Crisp White text (`#F4F6F8`) on true transparent terminal backgrounds (zero black-box artifacts).
  - Dynamic relative line number gutter with Ice Blue highlights on active rows.
- **Hybrid Ergonomics (Neovim + Modern IDE):**
  - Modal navigation (`NORMAL`, `INSERT`, `COMMAND`).
  - **Smart Tabs:** Real 4-space tab stops on Tab.
  - **Soft-Tab Unindent:** Backspace strips up to 4 spaces at once on leading indents.
  - **Intelligent Auto-Indent:** Preserves block indentation on Enter and automatically expands `{}` brace blocks into multi-line indented structures.
  - **Auto-Pairs:** Real-time auto-closing and atomic deletion for `()`, `[]`, `{}`, `""`, and `''`.
- **Memory-Safe Architecture:**
  - Dynamic line array (`erow`) with render/raw byte separation.
  - Transaction-based Undo/Redo linked list stack (`u` / `Ctrl+R`).
  - Double-buffered append buffer (`abuf`) with continuous chunked flush to prevent Linux PTY truncation.
  - 100% leak-free and buffer-safe verified under GCC AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`).
- **Syntax Highlighting Engine:**
  - Finite State Machine (FSM) tokenizer for C/C++, Python, and Shell scripts.
  - Real-time delimiter matching and live incremental search highlighting (`/`).

---

## Modal Keybindings

### Normal Mode (`Esc` to enter)
| Key | Action |
| --- | --- |
| `h`, `j`, `k`, `l` / Arrows | Cursor navigation |
| `w`, `b`, `0`, `$` | Word forward, word back, start of line, end of line |
| `gg`, `G` | Jump to top / bottom of document |
| `i`, `a`, `o` | Insert at cursor, append after cursor, open new line below |
| `dd` | Cut current line |
| `yy` / `p` | Copy (yank) current line / paste below |
| `x` | Delete character under cursor |
| `u` / `Ctrl+R` | Undo / Redo action |
| `/` | Live incremental search |
| `:` | Enter Command Mode (`:w`, `:q`, `:wq`, `:<line_number>`) |
| `Ctrl+S` / `Ctrl+Q` | Instant save / safe quit |

### Insert Mode (`i`, `a`, `o` to enter)
| Key | Action |
| --- | --- |
| `Esc` | Return to Normal Mode |
| `Tab` | Insert 4 spaces aligned to next tab stop |
| `Backspace` | Soft-tab unindent on indentations; paired delimiter deletion |
| `Enter` | Smart auto-indent with bracket block expansion |
| `Ctrl+S` | Save document directly without leaving Insert Mode |
| `Ctrl+Z` | Undo last edit directly in Insert Mode |

### Command Mode (`:` to enter)
| Command | Action |
| --- | --- |
| `:w` | Write (save) buffer to current file |
| `:w <filename>` | Save buffer to a new file |
| `:q` | Quit (fails if unsaved changes exist) |
| `:q!` | Force quit discarding unsaved changes |
| `:wq` | Save and quit |
| `:<number>` | Jump directly to line `<number>` (e.g. `:42`) |

---

## Building & Installation

### Requirements
- Arch Linux or any POSIX-compliant Linux distribution
- GCC supporting ISO C17 (`gcc -std=c17`)
- GNU `make`
- A terminal with TrueColor and Nerd Font support (e.g., Kitty, Alacritty, Foot)

### Build Commands
```bash
# Clone the repository
git clone https://github.com/islamMendjel/SenkuVim.git
cd SenkuVim

# Build release binary
make

# Test with AddressSanitizer & test suite
make asan
make test

# Install to ~/.local/bin and /usr/local/bin
make install
```

---

## Project Structure

```text
.
├── Makefile          # Compiler flags (-std=c17 -O2), test & install rules
├── senkuvim.h        # Core data structures, TrueColor palette, ANSI definitions
├── main.c            # State machine, keypress router, live search, command parser
├── terminal.c        # Low-level POSIX termios raw mode, SIGWINCH handling, write loops
├── buffer.c          # Dynamic line buffers (erow), row insertions/deletions, memory cleanup
├── ide_features.c    # Smart tabs, soft unindent, auto-pairs, brace expansion, undo/redo
├── syntax.c          # FSM syntax tokenizer and highlight matching
├── ui_rice.c         # Arch rice rendering engine, gutter numbers, Waybar pill bars
└── test_senkuvim.py  # PTY automation test suite verifying all 10 core behaviors
```

---

## License

MIT License. Crafted for Arch Linux power users.
