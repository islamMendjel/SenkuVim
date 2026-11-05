/**
 * ============================================================================
 * SENKUVIM - Production Modal & IDE Terminal Text Editor
 * Rick Sanchez Arch Rice Edition (C17 / POSIX)
 * ============================================================================
 *
 * senkuvim.h - Shared types, color palettes, data structures, and prototypes.
 *
 * Memory Architecture & Design Philosophy:
 * - Zero external library dependencies: built directly on POSIX libc, termios,
 *   and VT100 / xterm ANSI escape sequences.
 * - Dynamic rows (erow) keep raw text and render representations separated
 *   so tab expansions, UTF-8 offsets, and syntax tags do not corrupt original data.
 * - Complete undo/redo history modeled as a linked-list transaction stack.
 * - Double-buffered terminal rendering via append buffer (`abuf`) prevents screen tearing.
 */

#ifndef SENKUVIM_H
#define SENKUVIM_H

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>

/* ============================================================================
 * CONFIGURATION MACROS
 * ============================================================================ */
#define SENKUVIM_VERSION "1.0.0"
#define SENKUVIM_TAB_STOP 4
#define SENKUVIM_QUIT_TIMES 1

/*
 * Use Nerd Font rounded pill glyphs for the Waybar aesthetic.
 * Left cap:  U+E0B6 (\xee\x82\xb6)
 * Right cap: U+E0B4 (\xee\x82\xb4)
 * Branch:    U+E0A0 (\xee\x82\xa0)
 */
#define USE_NERD_FONT 1
#define NERD_PILL_LEFT  "\xee\x82\xb6"
#define NERD_PILL_RIGHT "\xee\x82\xb4"
#define NERD_BRANCH     "\xee\x82\xa0"

/* ============================================================================
 * RICK SANCHEZ ARCH RICE PALETTE (24-BIT TRUECOLOR ANSI)
 * Matches Hyprland / Waybar custom dark palette:
 * - Canvas Background:   Pitch Black     #000000
 * - Primary Accent:      Ice Blue        #A2D2FF (Rick's spiky hair)
 * - Secondary Accent:    Aqua Teal       #7AE5C6 (Rick's shirt)
 * - Code Main Text:      Crisp White     #F4F6F8 (Lab coat)
 * - Muted Unibrow:       Slate Grey      #4A5568 (Inactive gutter / separators)
 * - Portal Green:        Neon Green      #52B788 (Strings & Insert mode pill)
 * - Warning / Number:    Warm Amber      #FFD166 (Numbers / Search highlights)
 * - Error / Dirty:       Neon Pink-Red   #EF476F (Unsaved changes indicator)
 * ============================================================================ */

/* Text foreground colors */
#define COLOR_FG_CANVAS       "\x1b[38;2;0;0;0m"
#define COLOR_FG_ICE_BLUE     "\x1b[38;2;162;210;255m"
#define COLOR_FG_AQUA_TEAL    "\x1b[38;2;122;229;198m"
#define COLOR_FG_WHITE        "\x1b[38;2;244;246;248m"
#define COLOR_FG_MUTED        "\x1b[38;2;74;85;104m"
#define COLOR_FG_PORTAL_GREEN "\x1b[38;2;82;183;136m"
#define COLOR_FG_WARNING      "\x1b[38;2;255;209;102m"
#define COLOR_FG_ERROR        "\x1b[38;2;239;71;111m"
#define COLOR_FG_DARK_TEXT    "\x1b[38;2;9;13;22m"
#define COLOR_FG_SLATE        "\x1b[38;2;42;52;65m"

/* Background colors (used exclusively for pills and highlight tokens) */
#define COLOR_BG_CANVAS       ""
#define COLOR_BG_ICE_BLUE     "\x1b[48;2;162;210;255m"
#define COLOR_BG_AQUA_TEAL    "\x1b[48;2;122;229;198m"
#define COLOR_BG_MUTED        "\x1b[48;2;74;85;104m"
#define COLOR_BG_PORTAL_GREEN "\x1b[48;2;82;183;136m"
#define COLOR_BG_WARNING      "\x1b[48;2;255;209;102m"
#define COLOR_BG_ERROR        "\x1b[48;2;239;71;111m"
#define COLOR_BG_DARK_SURFACE ""
#define COLOR_BG_SLATE        "\x1b[48;2;42;52;65m"
#define COLOR_BG_PILL_DARK    "\x1b[48;2;24;31;48m"

/* Text attributes */
#define COLOR_RESET           "\x1b[0m"
#define COLOR_BOLD            "\x1b[1m"
#define COLOR_DIM             "\x1b[2m"
#define COLOR_ITALIC          "\x1b[3m"
#define COLOR_UNDERLINE       "\x1b[4m"

/* ============================================================================
 * KEY DEFINITIONS
 * Values >= 1000 avoid collisions with standard ASCII / control keys (0-255).
 * ============================================================================ */
enum EditorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/* ============================================================================
 * MODAL STATE MACHINE
 * ============================================================================ */
typedef enum {
    MODE_NORMAL = 0,   /* Command navigation, Vim shortcuts (h,j,k,l,dd,yy,p,etc.) */
    MODE_INSERT,       /* Typing characters directly into the buffer */
    MODE_COMMAND       /* Ex-command prompt at bottom (:w, :q, :wq, :<line>) */
} EditorMode;

/* ============================================================================
 * SYNTAX HIGHLIGHT CATEGORIES
 * ============================================================================ */
typedef enum {
    HL_NORMAL = 0,
    HL_KEYWORD,        /* Control flow: if, for, while, return, etc. */
    HL_TYPE,           /* Data types: int, void, struct, size_t, etc. */
    HL_STRING,         /* String literals "..." and chars '...' */
    HL_NUMBER,         /* Decimals, hex 0x, floats */
    HL_COMMENT,        /* Single line comments // or # */
    HL_MLCOMMENT,      /* Multi-line comments */
    HL_MATCH,          /* Search matches */
    HL_BRACKET         /* Matching brackets () [] {} */
} EditorHighlight;

#define HL_FLAG_NUMBERS (1 << 0)
#define HL_FLAG_STRINGS (1 << 1)

/* ============================================================================
 * UNDO / REDO TRANSACTIONS
 * Each entry stores enough delta information to reverse or replay an edit.
 * ============================================================================ */
typedef enum {
    UNDO_INSERT_CHAR,   /* Character inserted at cursor */
    UNDO_DELETE_CHAR,   /* Character removed at cursor */
    UNDO_INSERT_STRING, /* String inserted (e.g. smart tab or auto-indent) */
    UNDO_DELETE_STRING, /* String removed (e.g. soft tab backspace) */
    UNDO_SPLIT_LINE,    /* Line broken into two at cursor */
    UNDO_JOIN_LINE,     /* Line joined with the previous row */
    UNDO_DELETE_LINE,   /* Entire line removed (dd) */
    UNDO_PASTE_LINE     /* Line pasted below (p) */
} UndoType;

typedef struct UndoAction {
    UndoType type;              /* What kind of mutation occurred */
    int cx;                     /* Cursor X position before edit */
    int cy;                     /* Cursor Y position before edit */
    char *data;                 /* Snapshot of modified text */
    size_t len;                 /* Length of snapshot text */
    struct UndoAction *next;    /* Next transaction in stack */
} UndoAction;

/* ============================================================================
 * ROW DATA STRUCTURE (erow)
 * Represents a single line of text in the editor.
 *
 * Why split chars and render?
 * - `chars` holds the exact pristine file bytes (e.g. containing `\t`).
 * - `render` contains expanded visual representations (tabs -> 4 spaces).
 * - `hl` holds an array matching `render` byte-for-byte, storing highlight enums.
 * This separation ensures editing never mangles tab indentation or character offsets.
 * ============================================================================ */
typedef struct erow {
    int idx;             /* Row index in editor file (0-based) */
    int size;            /* Number of bytes in chars */
    int rsize;           /* Number of bytes in render */
    char *chars;         /* Raw character storage */
    char *render;        /* Display text with tab expansion */
    unsigned char *hl;   /* Highlight tokens for each byte in render */
    int hl_open_comment; /* True if line ends inside an unclosed multi-line comment */
} erow;

/* ============================================================================
 * SYNTAX DEFINITION
 * File extension matches, keywords, and tokenizer grammar.
 * ============================================================================ */
typedef struct SyntaxDefinition {
    const char *filetype;
    const char **filematch;
    const char **keywords;
    const char **types;
    const char *singleline_comment_start;
    const char *multiline_comment_start;
    const char *multiline_comment_end;
    int flags;
} SyntaxDefinition;

/* ============================================================================
 * DOUBLE-BUFFERED APPEND BUFFER (abuf)
 * Collects terminal draw calls in memory so the entire frame is written
 * in one single write() syscall. Eliminates screen flicker completely.
 * ============================================================================ */
typedef struct abuf {
    char *b;
    int len;
} abuf;

#define ABUF_INIT { NULL, 0 }

/* ============================================================================
 * GLOBAL EDITOR CONFIGURATION (E)
 * Master struct encapsulating terminal size, cursor coordinates, rows,
 * undo stacks, and modal state.
 * ============================================================================ */
typedef struct EditorConfig {
    int cx, cy;                  /* Cursor (X, Y) relative to file buffer */
    int rx;                      /* Rendered X index (accounting for tabs) */
    int rowoff;                  /* Row scroll offset (vertical scrolling) */
    int coloff;                  /* Column scroll offset (horizontal scrolling) */
    int screenrows;              /* Available terminal text rows */
    int screencols;              /* Available terminal columns */
    int numrows;                 /* Total count of lines in buffer */
    erow *row;                   /* Dynamic array of erow lines */
    int dirty;                   /* Dirty flag: incremented on any unsaved edit */
    char *filename;              /* Path to current file or NULL */
    char statusmsg[256];         /* Temporary notification message */
    time_t statusmsg_time;       /* Timestamp of notification */
    EditorMode mode;             /* Active editor mode */
    SyntaxDefinition *syntax;    /* Active syntax tokenizer */
    struct termios orig_termios; /* Restored upon exit */
    UndoAction *undo_stack;      /* Undo history stack */
    UndoAction *redo_stack;      /* Redo history stack */
    char *clipboard;             /* Line yank buffer for dd / yy / p */
    size_t clipboard_len;        /* Length of clipboard content */
    int search_last_match;       /* Row index of current search match */
    int search_dir;              /* Search direction: 1 forward, -1 backward */
    char *search_query;          /* Active live search query string */
    char command_buf[256];       /* Command input buffer for : prompt */
    int command_len;             /* Length of current command input */
    char pending_key;            /* Stores first key in two-key combos (e.g. 'd', 'y', 'g') */
    int quit_warn;               /* Warn counter for unsaved exit */
} EditorConfig;

extern EditorConfig E;

/* ============================================================================
 * FUNCTION PROTOTYPES
 * ============================================================================ */

/* --- Terminal Handling (terminal.c) --- */
void enableRawMode(void);
void disableRawMode(void);
int getWindowSize(int *rows, int *cols);
int editorReadKey(void);
void setupSignalHandlers(void);
void die(const char *s);
void abAppend(abuf *ab, const char *s, int len);
void abFree(abuf *ab);
void editorWriteAll(const char *buf, size_t len);

/* --- Buffer & Line Operations (buffer.c) --- */
int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorUpdateRow(erow *row);
void editorInsertRow(int at, const char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int at);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowDelChar(erow *row, int at);
void editorRowAppendString(erow *row, const char *s, size_t len);
void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);
char *editorRowsToString(int *buflen);
void editorOpen(const char *filename);
void editorSave(void);
void editorFreeAll(void);

/* --- IDE Features & Undo Stack (ide_features.c) --- */
void editorSmartTab(void);
void editorSmartBackspace(void);
void editorAutoIndentNewline(void);
bool editorHandleAutoPairs(int c);
void undoPush(UndoType type, int cx, int cy, const char *data, size_t len);
void editorUndo(void);
void editorRedo(void);
void undoFreeAll(void);
void editorCopyCurrentLine(void);
void editorDeleteCurrentLine(void);
void editorPasteBelow(void);

/* --- Syntax Highlighting Engine (syntax.c) --- */
void editorUpdateSyntax(erow *row);
const char *editorSyntaxToColor(int hl);
void editorSelectSyntaxHighlight(void);
bool isSeparator(int c);
void editorFindMatchingBracket(int *match_row, int *match_rx);

/* --- UI Aesthetic & Rick Sanchez Rice (ui_rice.c) --- */
void editorDrawRows(abuf *ab);
void editorDrawStatusBar(abuf *ab);
void editorDrawMessageBar(abuf *ab);
void editorDrawDashboard(abuf *ab);
void editorRefreshScreen(void);
void editorSetStatusMessage(const char *fmt, ...);

/* --- Main Event Loop & Interaction (main.c) --- */
void initEditor(void);
void editorProcessKeypress(void);
void editorFind(void);
void editorExecuteCommand(void);

#endif /* SENKUVIM_H */
