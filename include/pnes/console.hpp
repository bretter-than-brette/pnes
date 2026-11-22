/**
 * @file console.hpp
 * @brief Debug builds only: makes sure PC stdout (log()) is visible even when the
 *        game was launched by something that attached no terminal.
 *
 * Windows has AllocConsole() for exactly this. Linux/macOS have no equivalent,
 * so a terminal emulator is spawned on a FIFO this process redirects into --
 * which needs a GUI session, and on Linux a short list of emulators tried in
 * turn. Finding none is not an error.
 */
#pragma once

#if !defined(NDEBUG) && (defined(TARGET_LINUX) || defined(TARGET_MAC) || defined(TARGET_WINDOWS))

#include <cstdio>

// Every #include lives before any namespace opens: a header that reopens
// `namespace std` while nested inside `namespace tech` creates a bogus tech::std
// instead. Confirmed the hard way.
#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#include <conio.h>
#else
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <spawn.h>
#include <termios.h>
extern char **environ;
#endif

namespace tech {

#if defined(_WIN32)

inline bool StdoutIsTerminal() {
    return _isatty(_fileno(stdout)) != 0;
}

inline void PlatformEnsureConsole() {
    if (!AllocConsole()) return; // e.g. a console is already attached
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // "freopen may be unsafe" -- no user input involved
#endif
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

inline void WaitForKeypress() {
    _getch();
}

#else // POSIX: Linux/macOS

inline bool StdoutIsTerminal() {
    return isatty(STDOUT_FILENO) != 0;
}

inline bool SpawnTerminalReading(const std::string &fifoPath) {
    const std::string tailCmd = "tail -f -n +1 " + fifoPath;

#if defined(TARGET_MAC)
    pid_t pid = -1;
    const char *argv[] = { "open", "-a", "Terminal", "-n", "--args",
                            "sh", "-c", tailCmd.c_str(), nullptr };
    return posix_spawnp(&pid, "open", nullptr, nullptr,
                         const_cast<char *const *>(argv), environ) == 0;
#else
    static const char *kCandidates[] = {
        "x-terminal-emulator", // Debian/Ubuntu's configured default, if any
        "xterm", "konsole", "gnome-terminal", "xfce4-terminal",
        "alacritty", "kitty", nullptr
    };
    for (int i = 0; kCandidates[i]; ++i) {
        pid_t pid = -1;
        const char *argv[] = { kCandidates[i], "-e", "sh", "-c", tailCmd.c_str(), nullptr };
        if (posix_spawnp(&pid, kCandidates[i], nullptr, nullptr,
                          const_cast<char *const *>(argv), environ) == 0) {
            return true;
        }
    }
    return false;
#endif
}

inline void PlatformEnsureConsole() {
    char path[] = "/tmp/pnes_console_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return;
    close(fd);
    unlink(path);          // reclaim the name as a FIFO instead of a plain file
    if (mkfifo(path, 0600) != 0) return;

    if (!SpawnTerminalReading(path)) {
        unlink(path);
        return; // no terminal emulator found (or no GUI session) -- give up quietly
    }

    // O_RDWR, not O_WRONLY, which would block until a reader attached. The FIFO
    // is never unlinked: unlinking after our own open() races the terminal's
    // `tail -f`, whose open() then gets ENOENT and exits silently.
    const int rw = open(path, O_RDWR);
    if (rw < 0) return;

    dup2(rw, STDOUT_FILENO);
    dup2(rw, STDERR_FILENO);
    if (rw > STDERR_FILENO) close(rw);

    // stdio chose fully-buffered at startup from the original fd 1, so output
    // would otherwise sit there instead of appearing in the new console.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
}

/// Raw mode for one blocking read, then whatever mode it found is restored. If
/// stdin is not a terminal there is no raw mode to enter, so this just blocks on
/// a byte from whatever stdin actually is.
inline void WaitForKeypress() {
    termios oldAttrs{};
    if (tcgetattr(STDIN_FILENO, &oldAttrs) != 0) {
        char c;
        read(STDIN_FILENO, &c, 1);
        return;
    }
    termios raw = oldAttrs;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    char c;
    read(STDIN_FILENO, &c, 1);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldAttrs);
}

#endif // POSIX

inline void EnsureConsoleOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    if (StdoutIsTerminal()) return;
    PlatformEnsureConsole();
}

} // namespace tech

#else

namespace tech {
inline void EnsureConsoleOnce() {}
inline void WaitForKeypress() {}
} // namespace tech

#endif
