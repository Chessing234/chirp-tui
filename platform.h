// platform.h — cross-platform terminal: raw mode, size, non-blocking keys, bell.
#pragma once

#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <atomic>

namespace term {

enum class KeyKind { None, Char, ArrowUp, ArrowDown };

struct KeyEvent {
  KeyKind kind = KeyKind::None;
  unsigned char ch = 0;
};

namespace detail {

#ifdef _WIN32

inline HANDLE con_in() { return GetStdHandle(STD_INPUT_HANDLE); }
inline HANDLE con_out() { return GetStdHandle(STD_OUTPUT_HANDLE); }

inline DWORD saved_in_mode = 0;
inline DWORD saved_out_mode = 0;
inline bool raw_active = false;
inline bool modes_saved = false;

inline bool wait_stdin_ms(int timeout_ms) {
  HANDLE h = con_in();
  if (h == INVALID_HANDLE_VALUE || h == nullptr) return false;
  DWORD r = WaitForSingleObject(h, static_cast<DWORD>(timeout_ms < 0 ? INFINITE : timeout_ms));
  return r == WAIT_OBJECT_0;
}

#else  // POSIX

inline termios orig_termios{};
inline bool tty_saved = false;
inline bool raw_active = false;
inline unsigned char pending_after_esc = 0;
inline bool has_pending_after_esc = false;

inline int tty_fd() { return STDIN_FILENO; }

inline bool poll_fd_ms(int fd, int timeout_ms) {
  for (;;) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int pr = ::poll(&pfd, 1, timeout_ms);
    if (pr == 0) return false;
    if (pr < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    return (pfd.revents & POLLIN) != 0;
  }
}

inline ssize_t read1(int fd, unsigned char& out) {
  for (;;) {
    unsigned char buf[1];
    ssize_t n = ::read(fd, buf, 1);
    if (n == 1) {
      out = buf[0];
      return 1;
    }
    if (n == 0) return 0;
    if (n < 0 && errno == EINTR) continue;
    return n;
  }
}

#endif
}  // namespace detail

inline bool is_tty_in() {
#ifdef _WIN32
  return ::_isatty(::_fileno(stdin)) != 0;
#else
  return ::isatty(STDIN_FILENO) != 0;
#endif
}

inline bool is_tty_out() {
#ifdef _WIN32
  return ::_isatty(::_fileno(stdout)) != 0;
#else
  return ::isatty(STDOUT_FILENO) != 0;
#endif
}

// Enable VT sequences on Windows; set console title. Call once before raw mode.
inline bool init_console() {
#ifdef _WIN32
  HANDLE hi = detail::con_in();
  HANDLE ho = detail::con_out();
  if (hi == INVALID_HANDLE_VALUE || ho == INVALID_HANDLE_VALUE) return false;
  SetConsoleTitleA("Reminders");
  DWORD inm = 0, outm = 0;
  if (!GetConsoleMode(hi, &inm)) return false;
  if (!GetConsoleMode(ho, &outm)) return false;
  DWORD outm2 = outm | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  if (!SetConsoleMode(ho, outm2)) return false;
  // VT input helps some terminals emit CSI sequences; ignore failure.
  DWORD inm2 = inm | 0x0200;  // ENABLE_VIRTUAL_TERMINAL_INPUT
  SetConsoleMode(hi, inm2);
  return true;
#else
  (void)0;
  return true;
#endif
}

inline bool get_size(int& rows, int& cols) {
#ifdef _WIN32
  HANDLE ho = detail::con_out();
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(ho, &info)) {
    rows = 24;
    cols = 80;
    return false;
  }
  rows = info.srWindow.Bottom - info.srWindow.Top + 1;
  cols = info.srWindow.Right - info.srWindow.Left + 1;
  if (rows <= 0 || cols <= 0) {
    rows = 24;
    cols = 80;
    return false;
  }
  return true;
#else
  winsize w{};
  if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 || w.ws_row <= 0 || w.ws_col <= 0) {
    rows = 24;
    cols = 80;
    return false;
  }
  rows = w.ws_row;
  cols = w.ws_col;
  return true;
#endif
}

inline void bell() { std::putchar('\a'); }

inline void flush_out() { std::fflush(stdout); }

inline bool set_raw() {
#ifdef _WIN32
  HANDLE hi = detail::con_in();
  if (hi == INVALID_HANDLE_VALUE) return false;
  if (!detail::modes_saved) {
    if (!GetConsoleMode(hi, &detail::saved_in_mode)) return false;
    if (!GetConsoleMode(detail::con_out(), &detail::saved_out_mode)) return false;
    detail::modes_saved = true;
  }
  DWORD m = detail::saved_in_mode;
  m &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
  m |= ENABLE_VIRTUAL_TERMINAL_INPUT;
  if (!SetConsoleMode(hi, m)) return false;
  detail::raw_active = true;
  return true;
#else
  if (!is_tty_in()) return false;
  termios orig{};
  if (::tcgetattr(STDIN_FILENO, &orig) != 0) return false;
  termios raw = orig;
  raw.c_iflag &= static_cast<unsigned long>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
  raw.c_oflag &= static_cast<unsigned long>(~(OPOST));
  raw.c_cflag |= static_cast<unsigned long>(CS8);
  raw.c_lflag &= static_cast<unsigned long>(~(ECHO | ICANON | IEXTEN | ISIG));
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
  detail::orig_termios = orig;
  detail::tty_saved = true;
  detail::raw_active = true;
  return true;
#endif
}

inline void restore() {
#ifdef _WIN32
  if (detail::modes_saved) {
    SetConsoleMode(detail::con_in(), detail::saved_in_mode);
    SetConsoleMode(detail::con_out(), detail::saved_out_mode);
  }
  detail::raw_active = false;
#else
  if (detail::tty_saved) {
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &detail::orig_termios);
    detail::tty_saved = false;
  }
  detail::raw_active = false;
#endif
}

inline bool is_raw() { return detail::raw_active; }

// Cooked line mode for std::getline (POSIX termios; Windows console modes).
inline void suspend_line() {
#ifdef _WIN32
  HANDLE hi = detail::con_in();
  if (hi == INVALID_HANDLE_VALUE || !detail::modes_saved) return;
  DWORD m = detail::saved_in_mode;
  m |= ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT;
  SetConsoleMode(hi, m);
#else
  if (!detail::tty_saved) return;
  termios t = detail::orig_termios;
  t.c_lflag |= static_cast<unsigned long>(ECHO | ICANON);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
#endif
}

inline void resume_raw() { (void)set_raw(); }

// Non-blocking read (or timeout). Returns false if no key.
inline bool read_key(KeyEvent& ev, int timeout_ms) {
  ev = {};
#ifdef _WIN32
  HANDLE hi = detail::con_in();
  if (hi == INVALID_HANDLE_VALUE) return false;
  if (!detail::wait_stdin_ms(timeout_ms)) return false;
  for (int guard = 0; guard < 256; ++guard) {
    DWORD n = 0;
    if (!GetNumberOfConsoleInputEvents(hi, &n) || n == 0) return false;
    INPUT_RECORD ir{};
    DWORD rd = 0;
    if (!ReadConsoleInputW(hi, &ir, 1, &rd) || rd == 0) return false;
    if (ir.EventType != KEY_EVENT) continue;
    const KEY_EVENT_RECORD& ke = ir.Event.KeyEvent;
    if (!ke.bKeyDown) continue;
    UINT vk = ke.wVirtualKeyCode;
    if (vk == VK_UP) {
      ev = {KeyKind::ArrowUp, 0};
      return true;
    }
    if (vk == VK_DOWN) {
      ev = {KeyKind::ArrowDown, 0};
      return true;
    }
    WCHAR wc = ke.uChar.UnicodeChar;
    if (wc == 0) continue;
    if (wc >= 1 && wc <= 255) {
      ev = {KeyKind::Char, static_cast<unsigned char>(wc)};
      return true;
    }
    ev = {KeyKind::Char, '?'};
    return true;
  }
  return false;
#else
  if (detail::has_pending_after_esc) {
    ev = {KeyKind::Char, detail::pending_after_esc};
    detail::has_pending_after_esc = false;
    return true;
  }
  if (!detail::poll_fd_ms(STDIN_FILENO, timeout_ms)) return false;
  unsigned char c = 0;
  if (detail::read1(STDIN_FILENO, c) != 1) return false;
  if (c != 0x1b) {
    ev = {KeyKind::Char, c};
    return true;
  }
  if (!detail::poll_fd_ms(STDIN_FILENO, 150)) {
    ev = {KeyKind::Char, static_cast<unsigned char>('\033')};
    return true;
  }
  unsigned char b = 0;
  if (detail::read1(STDIN_FILENO, b) != 1) {
    ev = {KeyKind::Char, static_cast<unsigned char>('\033')};
    return true;
  }
  if (b != '[') {
    detail::pending_after_esc = b;
    detail::has_pending_after_esc = true;
    ev = {KeyKind::Char, static_cast<unsigned char>('\033')};
    return true;
  }
  // CSI: consume parameters until final byte 0x40–0x7E (e.g. CSI A / CSI 1 ; 2 A).
  unsigned char fin = 0;
  for (int k = 0; k < 64; ++k) {
    if (!detail::poll_fd_ms(STDIN_FILENO, k == 0 ? 150 : 8)) break;
    unsigned char ch = 0;
    if (detail::read1(STDIN_FILENO, ch) != 1) break;
    if (ch >= 0x40 && ch <= 0x7E) {
      fin = ch;
      break;
    }
  }
  if (fin == 'A') {
    ev = {KeyKind::ArrowUp, 0};
    return true;
  }
  if (fin == 'B') {
    ev = {KeyKind::ArrowDown, 0};
    return true;
  }
  return false;
#endif
}

// Block until any key (overlay dismiss).
inline void wait_any_key() {
#ifdef _WIN32
  HANDLE hi = detail::con_in();
  FlushConsoleInputBuffer(hi);
  suspend_line();
  for (int attempt = 0; attempt < 8; ++attempt) {
    wchar_t wch = 0;
    DWORD got = 0;
    if (ReadConsoleW(hi, &wch, 1, &got, nullptr)) {
      if (got > 0) break;
    }
  }
  resume_raw();
#else
  termios t = detail::orig_termios;
  if (detail::tty_saved) {
    t.c_lflag |= static_cast<unsigned long>(ECHO | ICANON);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
  }
  for (;;) {
    unsigned char buf[8];
    ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) break;
    if (n == 0) break;
    if (n < 0 && errno == EINTR) continue;
    break;
  }
  if (detail::tty_saved) {
    termios raw = detail::orig_termios;
    raw.c_iflag &= static_cast<unsigned long>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_oflag &= static_cast<unsigned long>(~(OPOST));
    raw.c_cflag |= static_cast<unsigned long>(CS8);
    raw.c_lflag &= static_cast<unsigned long>(~(ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }
#endif
}

inline void install_ctrl_handler(std::atomic<bool>* shutdown_flag) {
#ifdef _WIN32
  static std::atomic<bool>* g = nullptr;
  g = shutdown_flag;
  SetConsoleCtrlHandler(
      +[](DWORD ctrl_type) -> BOOL {
        if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
          if (g) g->store(true);
          return TRUE;
        }
        return FALSE;
      },
      TRUE);
#else
  static std::atomic<bool>* g = nullptr;
  g = shutdown_flag;
  struct SigAction {
    static void handler(int) {
      if (g) g->store(true);
    }
  };
  struct sigaction sa {};
  sa.sa_handler = SigAction::handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
#endif
}

}  // namespace term
