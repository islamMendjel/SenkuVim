/**
 * ============================================================================
 * SENKUVIM - main.c
 * Entry Point, Modal State Machine, Live Incremental Search & Event Loop
 * ============================================================================
 *
 * Modal Architecture:
 * - MODE_NORMAL:
 *   Vim-style navigation and editing commands (h/j/k/l, w, b, 0, $, gg, G,
 *   i, a, o, dd, yy, p, u, Ctrl+R, /, :).
 * - MODE_INSERT:
 *   VS Code-style fluid text editing with smart tab stops, auto-indent,
 *   auto-pairs, and shortcuts (Ctrl+S, Ctrl+F, Ctrl+Z, Ctrl+Q, Esc).
 * - MODE_COMMAND:
 *   Ex-commands (:w, :w <filename>, :q, :q!, :wq, :<linenumber>, :help).
 *
 * Pedagogical Notes:
 * - The event loop operates non-blockingly: keystrokes trigger instant state mutations,
 *   re-indexing of dynamic rows, and immediate double-buffered screen refreshes.
 * - Live search works incrementally: typing each character scans forward in the buffer,
 *   repositions the viewport, and renders glowing Amber highlights in real time.
 */

#include "senkuvim.h"

/* Master Global Editor State */
EditorConfig E;

/* ============================================================================
 * WORD NAVIGATION (w, b)
 * ============================================================================ */

/*
 * editorMoveWordForward() - Vim 'w' motion:
 * Advances cursor to the beginning of the next word.
 */
static void editorMoveWordForward(void) {
    if (E.numrows == 0 || E.row == NULL || E.cy >= E.numrows) return;
    erow *row = &E.row[E.cy];
    if (row == NULL || row->chars == NULL) return;

    /* If at end of line, wrap to start of next line */
    if (E.cx >= row->size) {
        if (E.cy < E.numrows - 1) {
            E.cy++;
            E.cx = 0;
            row = &E.row[E.cy];
            /* Skip leading whitespace on new line */
            while (E.cx < row->size && isspace((unsigned char)row->chars[E.cx])) {
                E.cx++;
            }
        }
        return;
    }

    /* 1. Skip current word characters */
    bool starting_in_sep = isSeparator(row->chars[E.cx]);
    while (E.cx < row->size && (isSeparator(row->chars[E.cx]) == starting_in_sep) && !isspace((unsigned char)row->chars[E.cx])) {
        E.cx++;
    }

    /* 2. Skip subsequent whitespace */
    while (E.cx < row->size && isspace((unsigned char)row->chars[E.cx])) {
        E.cx++;
    }

    /* If reached end of line, wrap to next line start */
    if (E.cx >= row->size && E.cy < E.numrows - 1) {
        E.cy++;
        E.cx = 0;
        row = &E.row[E.cy];
        while (E.cx < row->size && isspace((unsigned char)row->chars[E.cx])) {
            E.cx++;
        }
    }
}

/*
 * editorMoveWordBackward() - Vim 'b' motion:
 * Retracts cursor to the beginning of the previous word.
 */
static void editorMoveWordBackward(void) {
    if (E.numrows == 0 || E.row == NULL || E.cy >= E.numrows) return;

    /* If at start of line, wrap to end of previous line */
    if (E.cx == 0) {
        if (E.cy > 0) {
            E.cy--;
            E.cx = (E.row != NULL) ? E.row[E.cy].size : 0;
        } else {
            return;
        }
    }

    erow *row = &E.row[E.cy];
    if (row == NULL || row->chars == NULL) return;
    if (E.cx > 0) E.cx--;

    /* Skip whitespace backwards */
    while (E.cx > 0 && isspace((unsigned char)row->chars[E.cx])) {
        E.cx--;
    }

    /* Skip word characters backwards */
    bool starting_in_sep = isSeparator(row->chars[E.cx]);
    while (E.cx > 0 && (isSeparator(row->chars[E.cx - 1]) == starting_in_sep) && !isspace((unsigned char)row->chars[E.cx - 1])) {
        E.cx--;
    }
}

/* ============================================================================
 * CURSOR NAVIGATION & CLAMPING
 * ============================================================================ */

/*
 * editorMoveCursor() - Arrow key and h/j/k/l cursor movement.
 */
static void editorMoveCursor(int key) {
    if (E.numrows == 0 || E.row == NULL) {
        E.cx = 0;
        E.cy = 0;
        return;
    }

    erow *row = (E.cy < E.numrows && E.row != NULL) ? &E.row[E.cy] : NULL;

    switch (key) {
        case ARROW_LEFT:
        case 'h':
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                /* Wrap to end of previous line */
                E.cy--;
                E.cx = (E.row != NULL) ? E.row[E.cy].size : 0;
            }
            break;

        case ARROW_RIGHT:
        case 'l':
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size && E.cy < E.numrows - 1) {
                /* Wrap to start of next line */
                E.cy++;
                E.cx = 0;
            }
            break;

        case ARROW_UP:
        case 'k':
            if (E.cy > 0) {
                E.cy--;
            }
            break;

        case ARROW_DOWN:
        case 'j':
            if (E.cy < E.numrows - 1) {
                E.cy++;
            }
            break;
    }

    /* Clamp horizontal cursor position to line length */
    if (E.cy >= E.numrows) E.cy = (E.numrows > 0) ? E.numrows - 1 : 0;
    if (E.cy < 0) E.cy = 0;
    row = (E.cy < E.numrows && E.row != NULL) ? &E.row[E.cy] : NULL;
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
    if (E.cx < 0) E.cx = 0;
}

/* ============================================================================
 * LIVE INCREMENTAL SEARCH ENGINE (/)
 * ============================================================================ */

/*
 * editorFindCallback() - Search step called on each keystroke in search mode.
 */
static void editorFindCallback(char *query, int key) {
    if (E.search_query != NULL) {
        free(E.search_query);
        E.search_query = NULL;
    }

    if (query != NULL && strlen(query) > 0) {
        E.search_query = strdup(query);
    }

    if (key == '\r' || key == '\x1b') {
        E.search_last_match = -1;
        E.search_dir = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        E.search_dir = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        E.search_dir = -1;
    } else {
        E.search_last_match = -1;
        E.search_dir = 1;
    }

    if (query == NULL || strlen(query) == 0) return;
    if (E.numrows == 0 || E.row == NULL) return;

    int current = E.search_last_match;
    int qlen = (int)strlen(query);

    for (int i = 0; i < E.numrows; i++) {
        current += E.search_dir;
        if (current == -1) current = E.numrows - 1;
        else if (current == E.numrows) current = 0;

        erow *row = &E.row[current];
        char *match = NULL;

        /* Case-insensitive search */
        if (row->render != NULL && row->rsize >= qlen) {
            for (int j = 0; j <= row->rsize - qlen; j++) {
                if (strncasecmp(&row->render[j], query, (size_t)qlen) == 0) {
                    match = &row->render[j];
                    break;
                }
            }
        }

        if (match != NULL) {
            E.search_last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, (int)(match - row->render));
            E.rowoff = E.numrows; /* Forces editorScroll to center the match */
            break;
        }
    }
}

/*
 * editorFind() - Interactive live incremental search prompt.
 */
void editorFind(void) {
    if (E.numrows == 0 || E.row == NULL) {
        editorSetStatusMessage("Buffer is empty");
        return;
    }

    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    char query[128];
    query[0] = '\0';
    int qlen = 0;

    while (1) {
        editorSetStatusMessage("Search: %s (ESC to cancel | Enter to select | Arrows for next)", query);
        editorRefreshScreen();

        int c = editorReadKey();

        if (c == DEL_KEY || c == BACKSPACE || c == 8 || c == 127) {
            if (qlen > 0) {
                query[--qlen] = '\0';
                editorFindCallback(query, c);
            }
        } else if (c == '\x1b') {
            /* Cancel search and restore previous position */
            editorSetStatusMessage("");
            editorFindCallback(NULL, c);
            E.cx = saved_cx;
            E.cy = saved_cy;
            E.coloff = saved_coloff;
            E.rowoff = saved_rowoff;
            return;
        } else if (c == '\r') {
            /* Confirm selection */
            editorSetStatusMessage("");
            editorFindCallback(query, c);
            return;
        } else if (c == ARROW_RIGHT || c == ARROW_DOWN || c == ARROW_LEFT || c == ARROW_UP) {
            editorFindCallback(query, c);
        } else if (!iscntrl(c) && c < 128) {
            if (qlen < (int)sizeof(query) - 1) {
                query[qlen++] = (char)c;
                query[qlen] = '\0';
                editorFindCallback(query, c);
            }
        }
    }
}

/* ============================================================================
 * EX-COMMAND PROCESSOR (:)
 * ============================================================================ */

/*
 * editorExecuteCommand() - Parses and executes colon commands (:w, :q, :wq, etc.)
 */
void editorExecuteCommand(void) {
    char *cmd = E.command_buf;

    /* Trim leading whitespace */
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;

    if (strlen(cmd) == 0) {
        E.mode = MODE_NORMAL;
        return;
    }

    if (strcmp(cmd, "q") == 0) {
        if (E.dirty && E.quit_warn > 0) {
            editorSetStatusMessage("[WARNING] Unsaved changes! Use :q! to force quit or :w to save.");
            E.quit_warn--;
            E.mode = MODE_NORMAL;
            return;
        }
        editorFreeAll();
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
    } else if (strcmp(cmd, "q!") == 0) {
        editorFreeAll();
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
    } else if (strcmp(cmd, "w") == 0) {
        editorSave();
    } else if (strncmp(cmd, "w ", 2) == 0) {
        char *new_filename = cmd + 2;
        while (*new_filename && isspace((unsigned char)*new_filename)) new_filename++;
        if (strlen(new_filename) > 0) {
            free(E.filename);
            E.filename = strdup(new_filename);
            editorSelectSyntaxHighlight();
            editorSave();
        } else {
            editorSetStatusMessage("[ERROR] Invalid filename");
        }
    } else if (strcmp(cmd, "wq") == 0) {
        editorSave();
        if (E.dirty == 0) {
            editorFreeAll();
            write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
            exit(0);
        }
    } else if (cmd[0] >= '0' && cmd[0] <= '9') {
        /* Numeric line jump: e.g. ":42" */
        int target_line = atoi(cmd);
        if (target_line > 0 && target_line <= E.numrows) {
            E.cy = target_line - 1;
            E.cx = 0;
            editorSetStatusMessage("Jumped to line %d", target_line);
        } else if (E.numrows == 0) {
            E.cy = 0;
            E.cx = 0;
        } else {
            editorSetStatusMessage("[ERROR] Line number out of range (1 - %d)", E.numrows);
        }
    } else if (strcmp(cmd, "help") == 0) {
        editorSetStatusMessage("SenkuVim Help: :w (save), :q (quit), :wq (save&quit), :<num> (jump to line)");
    } else {
        editorSetStatusMessage("[ERROR] Unknown command: :%s", cmd);
    }

    E.command_len = 0;
    E.command_buf[0] = '\0';
    E.mode = MODE_NORMAL;
}

/* ============================================================================
 * MASTER KEYPRESS DISPATCHER
 * ============================================================================ */

/*
 * editorProcessKeypress() - Reads a key and routes to current modal handler.
 */
void editorProcessKeypress(void) {
    int c = editorReadKey();

    /* ========================================================================
     * 1. COMMAND MODE (':')
     * ======================================================================== */
    if (E.mode == MODE_COMMAND) {
        if (c == '\r') {
            editorExecuteCommand();
        } else if (c == '\x1b') {
            /* Cancel command mode */
            E.command_len = 0;
            E.command_buf[0] = '\0';
            E.mode = MODE_NORMAL;
            editorSetStatusMessage("");
        } else if (c == BACKSPACE || c == DEL_KEY || c == 8 || c == 127) {
            if (E.command_len > 0) {
                E.command_buf[--E.command_len] = '\0';
            } else {
                /* Exiting command mode if backspacing on empty prompt */
                E.mode = MODE_NORMAL;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (E.command_len < (int)sizeof(E.command_buf) - 1) {
                E.command_buf[E.command_len++] = (char)c;
                E.command_buf[E.command_len] = '\0';
            }
        }
        return;
    }

    /* ========================================================================
     * 2. INSERT MODE (VS Code Ergonomics)
     * ======================================================================== */
    if (E.mode == MODE_INSERT) {
        switch (c) {
            case '\x1b':
                /* Return to Normal mode */
                E.mode = MODE_NORMAL;
                /* If cursor is past line end, nudge left */
                if (E.cy < E.numrows && E.cx > 0 && E.cx == E.row[E.cy].size) {
                    E.cx--;
                }
                return;

            case '\r':
                editorAutoIndentNewline();
                return;

            case '\t':
                editorSmartTab();
                return;

            case BACKSPACE:
            case DEL_KEY:
            case 8:
                editorSmartBackspace();
                return;

            case 19: /* Ctrl+S: Instant Save */
                editorSave();
                return;

            case 6:  /* Ctrl+F: Live Search */
                editorFind();
                return;

            case 26: /* Ctrl+Z: Undo */
                editorUndo();
                return;

            case 17: /* Ctrl+Q: Safe Quit */
                if (E.dirty && E.quit_warn > 0) {
                    editorSetStatusMessage("[WARNING] Unsaved changes! Press Ctrl+Q again to force quit.");
                    E.quit_warn--;
                    return;
                }
                editorFreeAll();
                write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
                exit(0);

            case ARROW_UP:
            case ARROW_DOWN:
            case ARROW_LEFT:
            case ARROW_RIGHT:
                editorMoveCursor(c);
                return;

            case HOME_KEY:
                E.cx = 0;
                return;

            case END_KEY:
                if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
                return;

            default:
                /* Check Auto-Pairs for brackets and quotes */
                if (editorHandleAutoPairs(c)) {
                    return;
                }
                /* Standard character insertion */
                if (!iscntrl(c) && c < 256) {
                    char ch = (char)c;
                    undoPush(UNDO_INSERT_CHAR, E.cx, E.cy, &ch, 1);
                    editorInsertChar(c);
                }
                return;
        }
    }

    /* ========================================================================
     * 3. NORMAL MODE (Neovim Ergonomics)
     * ======================================================================== */
    if (E.mode == MODE_NORMAL) {
        /* Reset quit warning on any user action */
        E.quit_warn = SENKUVIM_QUIT_TIMES;

        /* Handle two-key commands (dd, yy, gg) */
        if (E.pending_key != 0) {
            char first = E.pending_key;
            E.pending_key = 0;

            if (first == 'd' && c == 'd') {
                editorDeleteCurrentLine();
                return;
            } else if (first == 'y' && c == 'y') {
                editorCopyCurrentLine();
                return;
            } else if (first == 'g' && c == 'g') {
                E.cy = 0;
                E.cx = 0;
                editorSetStatusMessage("Top of file");
                return;
            }
            /* If combo didn't match, fall through to process `c` normally */
        }

        switch (c) {
            /* Navigation */
            case 'h':
            case 'j':
            case 'k':
            case 'l':
            case ARROW_UP:
            case ARROW_DOWN:
            case ARROW_LEFT:
            case ARROW_RIGHT:
                editorMoveCursor(c);
                break;

            case 'w':
                editorMoveWordForward();
                break;

            case 'b':
                editorMoveWordBackward();
                break;

            case '0':
                E.cx = 0;
                break;

            case '$':
                if (E.numrows > 0 && E.row != NULL && E.cy < E.numrows) {
                    E.cx = (E.row[E.cy].size > 0) ? E.row[E.cy].size - 1 : 0;
                } else {
                    E.cx = 0;
                }
                break;

            case 'G':
                E.cy = (E.numrows > 0) ? E.numrows - 1 : 0;
                E.cx = 0;
                editorSetStatusMessage("Bottom of file");
                break;

            case 'g':
                /* Wait for second 'g' */
                E.pending_key = 'g';
                break;

            case PAGE_UP:
            case PAGE_DOWN: {
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } else if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy >= E.numrows) E.cy = (E.numrows > 0) ? E.numrows - 1 : 0;
                }
                int times = E.screenrows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
                break;
            }

            case HOME_KEY:
                E.cx = 0;
                break;

            case END_KEY:
                if (E.numrows > 0 && E.row != NULL && E.cy < E.numrows) {
                    E.cx = E.row[E.cy].size;
                } else {
                    E.cx = 0;
                }
                break;

            /* Mode Switching */
            case 'i':
                E.mode = MODE_INSERT;
                break;

            case 'a':
                if (E.numrows > 0 && E.row != NULL && E.cy < E.numrows && E.cx < E.row[E.cy].size) {
                    E.cx++;
                }
                E.mode = MODE_INSERT;
                break;

            case 'o':
                if (E.numrows == 0 || E.row == NULL) {
                    editorInsertRow(0, "", 0);
                    E.cy = 0;
                    E.cx = 0;
                } else {
                    if (E.cy < E.numrows) {
                        E.cx = E.row[E.cy].size;
                    }
                    editorAutoIndentNewline();
                }
                E.mode = MODE_INSERT;
                break;

            case ':':
                E.mode = MODE_COMMAND;
                E.command_len = 0;
                E.command_buf[0] = '\0';
                break;

            case '/':
                editorFind();
                break;

            /* Line & Char Editing */
            case 'd':
                E.pending_key = 'd';
                break;

            case 'y':
                E.pending_key = 'y';
                break;

            case 'p':
                editorPasteBelow();
                break;

            case 'x':
                /* Delete character under cursor */
                if (E.numrows > 0 && E.row != NULL && E.cy < E.numrows && E.cx < E.row[E.cy].size) {
                    char del_ch = E.row[E.cy].chars[E.cx];
                    undoPush(UNDO_DELETE_CHAR, E.cx, E.cy, &del_ch, 1);
                    editorRowDelChar(&E.row[E.cy], E.cx);
                }
                break;

            case '\x1b':
                /* Clear search highlights on Esc in Normal mode */
                if (E.search_query != NULL) {
                    free(E.search_query);
                    E.search_query = NULL;
                }
                break;

            /* Undo / Redo */
            case 'u':
                editorUndo();
                break;

            case 18: /* Ctrl+R: Redo */
                editorRedo();
                break;

            /* Direct File Operations */
            case 19: /* Ctrl+S: Save */
                editorSave();
                break;

            case 17: /* Ctrl+Q: Quit */
                if (E.dirty && E.quit_warn > 0) {
                    editorSetStatusMessage("[WARNING] Unsaved changes! Use :q! or press Ctrl+Q again to force quit.");
                    E.quit_warn--;
                    return;
                }
                editorFreeAll();
                write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
                exit(0);

            default:
                break;
        }
    }
}

/* ============================================================================
 * INITIALIZATION & ENTRY POINT
 * ============================================================================ */

/*
 * initEditor() - Zero-initializes all components in the master configuration.
 */
void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.mode = MODE_NORMAL;
    E.syntax = NULL;
    E.undo_stack = NULL;
    E.redo_stack = NULL;
    E.clipboard = NULL;
    E.clipboard_len = 0;
    E.search_last_match = -1;
    E.search_dir = 1;
    E.search_query = NULL;
    E.command_len = 0;
    E.command_buf[0] = '\0';
    E.pending_key = 0;
    E.quit_warn = SENKUVIM_QUIT_TIMES;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }

    /* Reserve 2 rows: row 1 for status bar, row 2 for command/message line */
    E.screenrows = (E.screenrows > 2) ? E.screenrows - 2 : 1;
    if (E.screencols < 10) E.screencols = 10;
}

/*
 * main() - Application entry point.
 */
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    setupSignalHandlers();

    if (argc >= 2) {
        editorOpen(argv[1]);
        editorSetStatusMessage("Opened '%s' | [i] Insert  [:] Commands  [/] Search  [u] Undo", argv[1]);
    } else {
        editorSetStatusMessage("SenkuVim v%s -- Rick Sanchez Arch Rice Edition | Press 'i' to edit", SENKUVIM_VERSION);
    }

    /* Core Event Loop */
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
