/**
 * ============================================================================
 * SENKUVIM - terminal.c
 * Terminal Raw Mode, Signal Handling, Escape Sequence Parser & Screen Buffer
 * ============================================================================
 *
 * Concepts Explained:
 * 1. Canonical vs. Raw Mode:
 *    By default, POSIX terminals run in "canonical" mode: input is buffered
 *    line-by-line until the user presses Enter, and control characters like
 *    Ctrl+C or Ctrl+Z send OS signals (SIGINT, SIGTSTP).
 *    Raw mode disables terminal echoing, line buffering, and signal generation,
 *    allowing SenkuVim to intercept every single keystroke in real-time.
 *
 * 2. Escape Sequence Parsing:
 *    Keys such as Arrow Keys, Home, End, PageUp, and Delete do not have simple
 *    1-byte ASCII values. Instead, VT100 / ANSI terminals transmit multi-byte
 *    escape sequences starting with '\x1b' (Escape), followed by '[' (CSI) or 'O',
 *    followed by argument characters (e.g. "\x1b[A" for Arrow Up).
 *    We implement a stateful, non-blocking parser with timeout to disambiguate
 *    a standalone Esc keypress from the start of an arrow sequence.
 *
 * 3. Double-Buffered Append Buffer (abuf):
 *    Calling write() for every character or ANSI code causes micro-delays
 *    and visible terminal flicker. We aggregate all screen updates into a
 *    dynamically resized heap buffer and flush it to STDOUT in a single syscall.
 */

#include "senkuvim.h"
#include <signal.h>
#include <string.h>

/* Flag set by asynchronous SIGWINCH handler */
static volatile sig_atomic_t window_resized = 0;

/* Flag tracking whether raw mode and orig_termios have been initialized */
static volatile sig_atomic_t raw_mode_enabled = 0;

/*
 * writeLiteralSafe() - Async-signal-safe helper to write a fixed string literal
 * without unused-result warnings.
 */
static void writeLiteralSafe(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        written += (size_t)n;
    }
}

/*
 * editorFatalSignalHandler() - Async-signal-safe crash/termination handler.
 * Restores terminal state cleanly before terminating with 128 + signal.
 */
void editorFatalSignalHandler(int sig) {
    if (raw_mode_enabled) {
        /* Restore cursor visibility and leave alternate screen buffer */
        static const char seq[] = "\x1b[?25h\x1b[?1049l";
        writeLiteralSafe(STDOUT_FILENO, seq, sizeof(seq) - 1);
        tcsetattr(STDIN_FILENO, TCSANOW, &E.orig_termios);
        raw_mode_enabled = 0;
    }

    static const char msg[] = "\n[SenkuVim] Caught terminating signal. Restored terminal.\n";
    writeLiteralSafe(STDERR_FILENO, msg, sizeof(msg) - 1);

    _exit(128 + sig);
}

void editorSetupSignals(void) {
    setupSignalHandlers();
}

/*
 * die() - Clean emergency exit.
 * Restores original terminal attributes before printing an error message,
 * ensuring the user's terminal shell remains completely usable.
 */
void die(const char *s) {
    if (raw_mode_enabled) {
        /* Clear screen and place cursor at top-left */
        writeLiteralSafe(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    }

    /* Restore original terminal settings */
    disableRawMode();

    perror(s);
    exit(EXIT_FAILURE);
}

/*
 * disableRawMode() - Restore terminal state.
 * Called at exit (via atexit) or on crash to return the terminal to canonical mode.
 */
void disableRawMode(void) {
    if (!raw_mode_enabled) return;

    /* Leave the alternate screen buffer and ensure cursor is visible */
    editorWriteAll("\x1b[?25h\x1b[?1049l", 14);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &E.orig_termios) == -1) {
        /* Silently ignore error during exit cleanup */
    }
    raw_mode_enabled = 0;
}

/*
 * enableRawMode() - Configure terminal into raw unbuffered input mode.
 * Disables canonical flags:
 * - ECHO: Prevents terminal from echoing typed characters automatically.
 * - ICANON: Reads byte-by-byte instead of waiting for a full line / Enter.
 * - ISIG: Disables SIGINT (Ctrl+C) and SIGTSTP (Ctrl+Z) signal emission.
 * - IXON: Disables software flow control (Ctrl+S / Ctrl+Q).
 * - IEXTEN: Disables implementation-defined input processing (Ctrl+V).
 * - ICRNL: Disables mapping of carriage return '\r' to newline '\n'.
 * - OPOST: Disables all output processing (e.g. automatic '\n' -> '\r\n').
 */
void enableRawMode(void) {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }

    /* Register cleanup on normal program termination */
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    /* Input flags: turn off break, CR to NL, parity check, strip 8th bit, flow control */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /* Output flags: disable post-processing (we handle \r\n explicitly) */
    raw.c_oflag &= ~(OPOST);

    /* Control flags: set 8 bits per byte */
    raw.c_cflag |= (CS8);

    /* Local flags: disable echoing, canonical mode, extended input, signals */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /*
     * VMIN and VTIME configure non-blocking read timeouts:
     * - VMIN = 0: read() returns immediately even if 0 bytes are available.
     * - VTIME = 1: 100 millisecond (0.1s) timeout before read() returns 0.
     */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        die("tcsetattr");
    }
    raw_mode_enabled = 1;

    /* Switch to the dedicated alternate screen buffer (standard in Vim / Tmux) */
    editorWriteAll("\x1b[?1049h", 8);
}

/*
 * getWindowSize() - Query terminal dimensions via ioctl or environment defaults.
 * Always guarantees valid rows and cols (> 0).
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    /* Fast path: query kernel via TIOCGWINSZ ioctl */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }

    /* Fallback: environment variables or standard 24x80 terminal default */
    const char *lines_env = getenv("LINES");
    const char *cols_env = getenv("COLUMNS");
    *rows = lines_env ? atoi(lines_env) : 24;
    *cols = cols_env ? atoi(cols_env) : 80;

    if (*rows <= 0) *rows = 24;
    if (*cols <= 0) *cols = 80;

    return 0;
}

/*
 * handleSigwinch() - POSIX signal handler for SIGWINCH (window resized).
 * Asynchronous-safe: sets an atomic flag so the main loop can safely recompute.
 */
static void handleSigwinch(int sig) {
    (void)sig;
    window_resized = 1;
}

/*
 * setupSignalHandlers() - Install signal handlers.
 * Intercepts SIGWINCH for live window resizing without crashing.
 */
/*
 * setupSignalHandlers() - Install all signal handlers.
 * Intercepts SIGWINCH for live window resizing and fatal signals (SIGINT,
 * SIGTERM, SIGSEGV, SIGABRT) so terminal state is always restored cleanly.
 */
void setupSignalHandlers(void) {
    struct sigaction sa_winch;
    memset(&sa_winch, 0, sizeof(sa_winch));
    sa_winch.sa_handler = handleSigwinch;
    sigemptyset(&sa_winch.sa_mask);
    /* Omit SA_RESTART so read() wakes immediately on SIGWINCH resize */
    sa_winch.sa_flags = 0;
    if (sigaction(SIGWINCH, &sa_winch, NULL) == -1) {
        die("sigaction SIGWINCH");
    }

    struct sigaction sa_fatal;
    memset(&sa_fatal, 0, sizeof(sa_fatal));
    sa_fatal.sa_handler = editorFatalSignalHandler;
    sigemptyset(&sa_fatal.sa_mask);
    sa_fatal.sa_flags = SA_RESETHAND;
    sigaction(SIGTERM, &sa_fatal, NULL);
    sigaction(SIGINT,  &sa_fatal, NULL);
    sigaction(SIGSEGV, &sa_fatal, NULL);
    sigaction(SIGABRT, &sa_fatal, NULL);
}

/*
 * handlePendingResize() - Checks and applies pending SIGWINCH window dimensions.
 */
static void handlePendingResize(void) {
    if (!window_resized) return;
    window_resized = 0;
    int rows, cols;
    if (getWindowSize(&rows, &cols) == 0) {
        E.screenrows = (rows > 2) ? rows - 2 : 1;
        E.screencols = (cols > 10) ? cols : 10;
        editorRefreshScreen();
    }
}

/* FIFO pushback buffer for unconsumed characters */
static char unget_buf[32];
static size_t unget_head = 0;
static size_t unget_tail = 0;

static void pushbackByte(char c) {
    size_t next_tail = (unget_tail + 1) % sizeof(unget_buf);
    if (next_tail == unget_head) {
        /* Ring buffer full: drop oldest byte rather than corrupting head */
        unget_head = (unget_head + 1) % sizeof(unget_buf);
    }
    unget_buf[unget_tail] = c;
    unget_tail = next_tail;
}

static int readRawByte(char *c) {
    if (unget_head != unget_tail) {
        *c = unget_buf[unget_head];
        unget_head = (unget_head + 1) % sizeof(unget_buf);
        return 1;
    }
    return (int)read(STDIN_FILENO, c, 1);
}

/*
 * editorReadKey() - Read and parse a single key or ANSI escape sequence.
 * Handles single ASCII characters, control sequences, and multi-byte ANSI sequences.
 */
int editorReadKey(void) {
    int nread;
    char c;

    /* Check if window was resized while waiting */
    handlePendingResize();

    /* Loop until a key is read; read() times out every 100ms per VTIME */
    while ((nread = readRawByte(&c)) != 1) {
        if (nread == -1 && errno != EAGAIN && errno != EINTR) {
            die("read");
        }
        handlePendingResize();
    }

    /* If character is not Escape, return plain ASCII/byte directly */
    if (c != '\x1b') {
        return (unsigned char)c;
    }

    /*
     * If an Escape byte was received, peek the next byte.
     * Valid VT100 / ANSI escape sequences strictly begin with '[' or 'O'.
     * If next byte is not '[' or 'O', it was a standalone Esc followed by normal input!
     */
    char seq[3];
    if (readRawByte(&seq[0]) != 1) return '\x1b';

    if (seq[0] != '[' && seq[0] != 'O') {
        /* Not an escape sequence! Push seq[0] back so next read returns it */
        pushbackByte(seq[0]);
        return '\x1b';
    }

    if (readRawByte(&seq[1]) != 1) {
        pushbackByte(seq[0]);
        return '\x1b';
    }

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            /* Tilde-terminated sequences: e.g. "\x1b[3~" for Delete */
            if (readRawByte(&seq[2]) != 1) {
                pushbackByte(seq[0]);
                pushbackByte(seq[1]);
                return '\x1b';
            }
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                }
            }
        } else {
            /* Letter-terminated cursor movements */
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
    } else if (seq[0] == 'O') {
        /* Application cursor keys: "\x1bOH" (Home) or "\x1bOF" (End) */
        switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
        }
    }

    return '\x1b';
}

/* ============================================================================
 * DOUBLE-BUFFERED APPEND BUFFER (abuf) IMPLEMENTATION
 * ============================================================================ */

/*
 * abAppend() - Dynamically grows the buffer using geometric capacity doubling.
 * Eliminates O(N^2) realloc() calls during frame rendering.
 */
void abAppend(abuf *ab, const char *s, int len) {
    if (ab == NULL || len <= 0 || s == NULL) return;

    if (ab->len + len > ab->cap) {
        int new_cap = (ab->cap == 0) ? 4096 : ab->cap;
        while (new_cap < ab->len + len) {
            if (new_cap > INT_MAX / 2) {
                new_cap = ab->len + len;
                break;
            }
            new_cap *= 2;
        }
        char *new_buf = realloc(ab->b, (size_t)new_cap);
        if (new_buf == NULL) {
            die("realloc abuf");
        }
        ab->b = new_buf;
        ab->cap = new_cap;
    }

    memcpy(&ab->b[ab->len], s, (size_t)len);
    ab->len += len;
}

/*
 * abFree() - Frees the dynamic append buffer memory.
 */
void abFree(abuf *ab) {
    if (ab == NULL) return;
    if (ab->b != NULL) {
        free(ab->b);
        ab->b = NULL;
    }
    ab->len = 0;
    ab->cap = 0;
}

/*
 * editorWriteAll() - Loops write() until all bytes are transmitted.
 * Prevents truncation on large frame buffers due to POSIX pipe/PTY buffer limits.
 */
void editorWriteAll(const char *buf, size_t len) {
    if (buf == NULL || len == 0) return;
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(STDOUT_FILENO, buf + written, len - written);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }
            break;
        }
        written += (size_t)n;
    }
}
