// main.cpp — cross-platform TUI reminder list (C++17). See README.md for build & install.
#include "json_store.h"
#include "platform.h"
#include "reminders.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace rem;

bool try_save(const std::string& path, AppState& st) {
  if (!json_store::save_reminders(path, st)) {
    std::cerr << "reminders: warning: could not save to " << path << '\n';
    return false;
  }
  return true;
}

// Line input during dialogs; EOF requests app shutdown.
bool read_dialog_line(std::string& out, AppState& st) {
  if (!std::getline(std::cin, out)) {
    std::cin.clear();
    if (std::cin.eof()) st.shutdown_requested = true;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// ANSI rendering (Windows 10+ VT + POSIX)
// ---------------------------------------------------------------------------

const char* ANSI_RESET = "\033[0m";
const char* ANSI_HIDE_CURSOR = "\033[?25l";
const char* ANSI_SHOW_CURSOR = "\033[?25h";
const char* ANSI_ALT_SCREEN = "\033[?1049h";
const char* ANSI_MAIN_SCREEN = "\033[?1049l";
const char* ANSI_CLEAR = "\033[2J\033[H";

std::string ansi_goto(int row, int col) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "\033[%d;%dH", row, col);
  return buf;
}

std::string row_style(bool done) {
  if (done) return "\033[2;37m";
  return "\033[1;37m";
}

std::string strikethrough_on() { return "\033[9m"; }
std::string strikethrough_off() { return "\033[29m"; }

void flash_screen() {
  for (int i = 0; i < 8; ++i) {
    std::fputs((i % 2) ? "\033[?5h" : "\033[?5l", stdout);
    term::flush_out();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
  }
  std::fputs("\033[?5l", stdout);
  term::flush_out();
}

std::string truncate_utf8_safe(const std::string& s, std::size_t max_display_cols) {
  if (max_display_cols == 0) return "";
  std::size_t cols = 0;
  std::size_t cut = 0;
  for (std::size_t i = 0; i < s.size() && cols < max_display_cols;) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t adv = 1;
    if (c < 0x80) {
      adv = 1;
      cols += 1;
    } else if ((c & 0xE0) == 0xC0) {
      adv = 2;
      cols += 1;
    } else if ((c & 0xF0) == 0xE0) {
      adv = 3;
      cols += 1;
    } else if ((c & 0xF8) == 0xF0) {
      adv = 4;
      cols += 2;
    } else {
      adv = 1;
      cols += 1;
    }
    if (i + adv > s.size()) break;
    i += adv;
    cut = i;
  }
  if (cut < s.size()) return s.substr(0, cut) + "…";
  return s;
}

std::uint64_t next_id(const AppState& st) {
  std::lock_guard<std::mutex> lock(st.mu);
  std::uint64_t m = 0;
  for (const auto& r : st.reminders) m = std::max(m, r.id);
  return m + 1;
}

void bell_if(const AppState& st) {
  bool on = false;
  {
    std::lock_guard<std::mutex> lock(st.mu);
    on = st.config.bell_on_popup;
  }
  if (on) term::bell();
}

std::vector<std::string> popup_titles(const AppState& st) {
  std::vector<std::string> out;
  {
    std::lock_guard<std::mutex> lock(st.mu);
    for (const auto& r : st.reminders) {
      if (r.done) continue;
      out.push_back(r.title);
    }
  }
  return out;
}

void render_overlay(AppState& st, bool /*from_t1*/) {
  bell_if(st);
  term::bring_host_terminal_forward();
  term::flush_out();
  int rows = 24, cols = 80;
  (void)term::get_size(rows, cols);
  // Avoid negative layout if the terminal shrank mid-session.
  cols = std::max(cols, 24);
  rows = std::max(rows, 12);
  std::fputs(ANSI_ALT_SCREEN, stdout);
  std::fputs(ANSI_HIDE_CURSOR, stdout);
  std::fputs(ANSI_CLEAR, stdout);
  term::flush_out();
  flash_screen();

  auto titles = popup_titles(st);
  const int box_w = std::max(8, std::min(cols - 2, 72));
  const int inner_w = std::max(4, box_w - 2);
  const int box_lines = 12;
  const int start_row = std::min(std::max(1, rows / 2 - box_lines / 2), std::max(1, rows - box_lines));
  const int start_col = std::max(1, (cols - box_w) / 2 + 1);

  auto hline = [&](int row, const char* left, const char* mid, const char* right) {
    std::string line;
    line += ansi_goto(row, start_col);
    line += left;
    for (int x = 0; x < inner_w; ++x) line += mid;
    line += right;
    line += ANSI_RESET;
    std::fputs(line.c_str(), stdout);
  };

  hline(start_row, "╔", "═", "╗");
  for (int r = 1; r <= 10; ++r) {
    std::string line = ansi_goto(start_row + r, start_col) + "║";
    for (int x = 0; x < inner_w; ++x) line += " ";
    line += "║";
    std::fputs(line.c_str(), stdout);
  }
  hline(start_row + 11, "╚", "═", "╝");

  int row = start_row + 1;
  {
    std::string hdr = ansi_goto(row++, start_col + 2);
    hdr += "\033[1;5;91m⏰  REMINDER CHECK-IN  ⏰\033[0m";
    std::fputs(hdr.c_str(), stdout);
  }
  ++row;

  if (titles.empty()) {
    std::string msg = ansi_goto(row++, start_col + 2);
    msg += "\033[1;36m(No open reminders — you're clear!)\033[0m";
    std::fputs(msg.c_str(), stdout);
  } else {
    for (const std::string& t : titles) {
      if (row >= start_row + 9) break;
      std::string title = truncate_utf8_safe(t, static_cast<std::size_t>(inner_w - 4));
      std::string line = ansi_goto(row++, start_col + 2) + "\033[1;37m• " + title + "\033[0m";
      std::fputs(line.c_str(), stdout);
    }
  }

  std::string foot = ansi_goto(start_row + 9, start_col + 2);
  foot += "\033[1;96mPress ENTER (or any key) to dismiss\033[0m";
  std::fputs(foot.c_str(), stdout);
  std::fputs(ANSI_SHOW_CURSOR, stdout);
  term::flush_out();

  term::wait_any_key();

  std::fputs(ANSI_MAIN_SCREEN, stdout);
  std::fputs(ANSI_CLEAR, stdout);
  term::flush_out();
}

void timer_thread_fn(AppState* st, int which) {
  const bool is_t1 = (which == 1);
  while (!st->shutdown_requested.load()) {
    int minutes = 0;
    {
      std::lock_guard<std::mutex> lock(st->mu);
      minutes = is_t1 ? st->config.t1_minutes : st->config.t2_minutes;
    }
    if (!is_t1 && minutes <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }
    if (minutes <= 0) minutes = 1;

    auto end = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);
    while (!st->shutdown_requested.load() && std::chrono::steady_clock::now() < end) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (st->shutdown_requested.load()) break;

    if (is_t1) {
      {
        std::lock_guard<std::mutex> lock(st->mu);
        st->last_t1_fire = std::chrono::steady_clock::now();
      }
      st->overlay_t1_pending.store(true);
    } else {
      {
        std::lock_guard<std::mutex> lock(st->mu);
        st->last_t2_fire = std::chrono::steady_clock::now();
      }
      st->overlay_t2_pending.store(true);
    }
  }
}

enum class UiMode { List, Settings };

struct UiState {
  UiMode mode = UiMode::List;
  std::size_t sel_index = 0;
};

void sort_reminders_inplace(std::vector<Reminder>& v) {
  std::sort(v.begin(), v.end(), [](const Reminder& a, const Reminder& b) {
    if (a.done != b.done) return a.done < b.done;
    return a.id < b.id;
  });
}

std::vector<Reminder> snapshot_reminders(AppState& st) {
  std::lock_guard<std::mutex> lock(st.mu);
  auto v = st.reminders;
  sort_reminders_inplace(v);
  return v;
}

long seconds_until_next(std::chrono::steady_clock::time_point last_fire, int interval_min) {
  using clock = std::chrono::steady_clock;
  if (interval_min <= 0) interval_min = 1;
  auto next = last_fire + std::chrono::minutes(interval_min);
  auto now = clock::now();
  if (next <= now) return 0;
  return std::chrono::duration_cast<std::chrono::seconds>(next - now).count();
}

std::string fmt_countdown(std::chrono::steady_clock::time_point last_fire, int interval_min) {
  long sec = seconds_until_next(last_fire, interval_min);
  long mm = sec / 60;
  long ss = sec % 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02ld:%02ld", mm, ss);
  return buf;
}

void draw_main(AppState& st, const UiState& ui, int rows, int cols, const std::string& path_hint) {
  std::ostringstream out;
  out << ANSI_CLEAR << ANSI_HIDE_CURSOR;

  out << ansi_goto(1, 1) << "\033[1;36mReminders\033[0m  ";
  out << "\033[2;37m— " << truncate_utf8_safe(path_hint, 48) << " —\033[0m";

  std::string cd1, cd2, soon;
  {
    std::lock_guard<std::mutex> lock(st.mu);
    cd1 = fmt_countdown(st.last_t1_fire, st.config.t1_minutes);
    long s1 = seconds_until_next(st.last_t1_fire, st.config.t1_minutes);
    long mn = s1;
    if (st.config.t2_minutes > 0) {
      cd2 = fmt_countdown(st.last_t2_fire, st.config.t2_minutes);
      long s2 = seconds_until_next(st.last_t2_fire, st.config.t2_minutes);
      mn = std::min(s1, s2);
    } else {
      cd2 = "off";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld", mn / 60, mn % 60);
    soon = buf;
  }
  {
    std::string corner = "Next popup in: " + soon + "  (T1 " + cd1 + " · T2 " + cd2 + ")";
    if (static_cast<int>(corner.size()) > cols - 1) {
      corner = "Next: " + soon + "  T1:" + cd1 + " T2:" + cd2;
      corner = truncate_utf8_safe(corner, static_cast<std::size_t>(cols - 1));
    }
    int col = std::max(1, cols - static_cast<int>(corner.size()) - 1);
    out << ansi_goto(1, col) << "\033[1;35m" << corner << "\033[0m";
  }

  int row = 3;
  const int max_row = std::max(3, rows - 2);  // keep last row for menu bar
  auto list = snapshot_reminders(st);
  if (list.empty()) {
    if (row < max_row) out << ansi_goto(row++, 1) << "\033[2;37m(no reminders yet — press 'a' to add)\033[0m";
  } else {
    auto emit_section = [&](const char* title) {
      if (row < max_row) out << ansi_goto(row++, 1) << "\033[1;37m" << title << "\033[0m";
    };
    auto emit_rem = [&](const Reminder& r, bool selected) {
      if (row >= max_row) return;
      std::string mark = selected ? "\033[1;7m>\033[0m " : "  ";
      std::string base = row_style(r.done) + (r.done ? strikethrough_on() : "") + r.title + (r.done ? strikethrough_off() : "") + ANSI_RESET;
      if (!r.due.empty()) base += std::string(" \033[2;36m(due ") + r.due + ")\033[0m";
      std::string line = mark + base;
      line = truncate_utf8_safe(line, static_cast<std::size_t>(cols - 1));
      out << ansi_goto(row++, 1) << line;
    };

    emit_section("── Open ──");
    bool any = false;
    for (std::size_t i = 0; i < list.size(); ++i) {
      const Reminder& r = list[i];
      if (!r.done) {
        emit_rem(r, i == ui.sel_index);
        any = true;
      }
    }
    if (!any && row < max_row) out << ansi_goto(row++, 3) << "\033[2;37m(empty)\033[0m";

    emit_section("── Done ──");
    any = false;
    for (std::size_t i = 0; i < list.size(); ++i) {
      const Reminder& r = list[i];
      if (r.done) {
        emit_rem(r, i == ui.sel_index);
        any = true;
      }
    }
    if (!any && row < max_row) out << ansi_goto(row++, 3) << "\033[2;37m(empty)\033[0m";
  }

  int menu_row = std::max(3, rows - 1);
  out << ansi_goto(menu_row, 1) << "\033[30;106m";
  std::string menu =
      " ^v move | a add | e edit | d del | space done | s settings | q quit "
      "\033[0m";
  menu = truncate_utf8_safe(menu, static_cast<std::size_t>(cols));
  out << menu;

  std::fputs(out.str().c_str(), stdout);
  term::flush_out();
  (void)rows;
}

void draw_settings(AppState& st, int /*rows*/, int /*cols*/) {
  int t1 = 0, t2 = 0;
  bool bell = false;
  {
    std::lock_guard<std::mutex> lock(st.mu);
    t1 = st.config.t1_minutes;
    t2 = st.config.t2_minutes;
    bell = st.config.bell_on_popup;
  }
  std::ostringstream out;
  out << ANSI_CLEAR << ANSI_HIDE_CURSOR;
  out << ansi_goto(2, 2) << "\033[1;33mSettings\033[0m";
  out << ansi_goto(4, 2) << "T1 interval (minutes): " << t1;
  out << ansi_goto(5, 2) << "T2 interval (minutes): " << (t2 > 0 ? std::to_string(t2) : std::string("0 (off)"));
  out << ansi_goto(6, 2) << "Bell on popup: " << (bell ? "ON" : "OFF") << "  (press b to toggle)";
  out << ansi_goto(8, 2) << "\033[1;36mKeys:\033[0m 1/2 select field, +/- adjust (T2 min 0=off), b bell, s save & back, q back";
  std::fputs(out.str().c_str(), stdout);
  term::flush_out();
}

void clamp_selection(std::size_t& sel, const std::vector<Reminder>& list) {
  if (list.empty()) {
    sel = 0;
    return;
  }
  if (sel >= list.size()) sel = list.size() - 1;
}

std::optional<Reminder> find_by_id(AppState& st, std::uint64_t id) {
  std::lock_guard<std::mutex> lock(st.mu);
  for (const auto& r : st.reminders)
    if (r.id == id) return r;
  return std::nullopt;
}

struct CliOpts {
  bool want_help = false;
  bool want_version = false;
  bool bad = false;
  bool t1_set = false;
  bool t2_set = false;
  int t1_minutes = 60;
  int t2_minutes = 0;
  bool no_bell = false;
  std::string data_file;
};

void print_help(const char* argv0) {
  std::cerr << "reminders — terminal reminder list with popup check-in timer(s).\n\n";
  std::cerr << "Usage: " << argv0 << " [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  --t1 <min>        Timer 1 interval in minutes (default 60)\n";
  std::cerr << "  --t2 <min>        Timer 2 in minutes; use 0 to disable (default 0)\n";
  std::cerr << "  --data-file <p>   JSON store path (default: ~/" << kDefaultDataFilename << ")\n";
  std::cerr << "  --no-bell         Disable terminal bell on popups\n";
  std::cerr << "  --version         Print version and exit\n";
  std::cerr << "  --help            Show this help\n";
}

CliOpts parse_cli(int argc, char** argv) {
  CliOpts o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      o.want_help = true;
      continue;
    }
    if (a == "--version" || a == "-v") {
      o.want_version = true;
      continue;
    }
    if (a == "--no-bell") {
      o.no_bell = true;
      continue;
    }
    auto need = [&](void) -> const char* {
      if (i + 1 >= argc) return nullptr;
      return argv[++i];
    };
    if (a == "--t1" || a == "-1") {
      const char* v = need();
      if (!v) {
        o.bad = true;
        continue;
      }
      try {
        o.t1_minutes = std::stoi(v);
        o.t1_set = true;
        if (o.t1_minutes <= 0) o.bad = true;
      } catch (...) {
        o.bad = true;
      }
      continue;
    }
    if (a == "--t2" || a == "-2") {
      const char* v = need();
      if (!v) {
        o.bad = true;
        continue;
      }
      try {
        o.t2_minutes = std::stoi(v);
        o.t2_set = true;
        if (o.t2_minutes < 0) o.bad = true;
      } catch (...) {
        o.bad = true;
      }
      continue;
    }
    if (a == "--data-file") {
      const char* v = need();
      if (!v)
        o.bad = true;
      else
        o.data_file = v;
      continue;
    }
    o.bad = true;
  }
  return o;
}

struct TermRawGuard {
  bool ok = false;
  TermRawGuard() {
    if (term::is_tty_in()) ok = term::set_raw();
  }
  void suspend_line() { term::suspend_line(); }
  void resume_raw() { term::resume_raw(); }
  void restore_now() {
    if (!ok) return;
    term::restore();
    ok = false;
  }
  ~TermRawGuard() { restore_now(); }
};

}  // namespace

int main(int argc, char** argv) {
  CliOpts cli = parse_cli(argc, argv);
  if (cli.bad) {
    print_help(argv[0]);
    return 2;
  }
  if (cli.want_version) {
    std::cout << "reminders v" << kVersion << '\n';
    return 0;
  }
  if (cli.want_help) {
    print_help(argv[0]);
    return 0;
  }

  if (!term::is_tty_in() || !term::is_tty_out()) {
    std::cerr << "reminders: stdin/stdout must be an interactive terminal.\n";
    return 1;
  }

  if (!term::init_console()) {
#ifdef _WIN32
    std::cerr << "reminders: note: virtual terminal setup failed; ANSI colors may not display.\n";
#endif
  }

  int rows = 24, cols = 80;
  (void)term::get_size(rows, cols);
  if (cols < 60 || rows < 20) {
    std::cerr << "reminders: terminal too small (need at least 60 columns × 20 rows). "
               << "Current: " << cols << "×" << rows << ".\n";
    return 1;
  }

  const std::string data_path =
      cli.data_file.empty() ? json_store::default_data_path() : json_store::expand_user_path(cli.data_file);

  AppState st;
  if (!json_store::load_reminders(data_path, st)) {
    std::ifstream probe(data_path);
    if (probe.good()) {
      std::cerr << "reminders: error: could not parse " << data_path << " (using defaults).\n";
    }
  }

  {
    std::lock_guard<std::mutex> lock(st.mu);
    if (cli.t1_set) st.config.t1_minutes = cli.t1_minutes;
    if (cli.t2_set) st.config.t2_minutes = cli.t2_minutes;
    if (cli.no_bell) st.config.bell_on_popup = false;
    auto now = std::chrono::steady_clock::now();
    st.last_t1_fire = now;
    st.last_t2_fire = now;
  }
  try_save(data_path, st);

  term::install_ctrl_handler(&st.shutdown_requested);

  std::thread th1(timer_thread_fn, &st, 1);
  std::thread th2(timer_thread_fn, &st, 2);

  TermRawGuard tm;
  if (!tm.ok) {
    std::cerr << "reminders: failed to enable raw terminal mode.\n";
    st.shutdown_requested = true;
    th1.join();
    th2.join();
    return 1;
  }

  UiState ui;
  bool settings_field1 = true;

  while (!st.shutdown_requested.load()) {
    if (st.overlay_t1_pending.exchange(false)) render_overlay(st, true);
    if (st.overlay_t2_pending.exchange(false)) render_overlay(st, false);

    (void)term::get_size(rows, cols);
    if (cols < 60 || rows < 20) {
      std::cerr << "\nreminders: terminal resized too small; exiting.\n";
      st.shutdown_requested = true;
      break;
    }

    if (ui.mode == UiMode::List) {
      draw_main(st, ui, rows, cols, data_path);
    } else {
      draw_settings(st, rows, cols);
    }

    term::KeyEvent ev{};
    if (!term::read_key(ev, 150)) continue;

    if (ui.mode == UiMode::Settings) {
      if (ev.kind == term::KeyKind::Char && (ev.ch == 'q' || ev.ch == 'Q')) {
        ui.mode = UiMode::List;
        continue;
      }
      if (ev.kind == term::KeyKind::Char && ev.ch == '1')
        settings_field1 = true;
      else if (ev.kind == term::KeyKind::Char && ev.ch == '2')
        settings_field1 = false;
      else if (ev.kind == term::KeyKind::Char && (ev.ch == 'b' || ev.ch == 'B')) {
        std::lock_guard<std::mutex> lock(st.mu);
        st.config.bell_on_popup = !st.config.bell_on_popup;
        try_save(data_path, st);
      } else if (ev.kind == term::KeyKind::Char && (ev.ch == '+' || ev.ch == '=')) {
        std::lock_guard<std::mutex> lock(st.mu);
        if (settings_field1)
          st.config.t1_minutes = std::min(st.config.t1_minutes + 5, 24 * 60);
        else
          st.config.t2_minutes = std::min(st.config.t2_minutes + 5, 24 * 60);
        try_save(data_path, st);
      } else if (ev.kind == term::KeyKind::Char && (ev.ch == '-' || ev.ch == '_')) {
        std::lock_guard<std::mutex> lock(st.mu);
        if (settings_field1)
          st.config.t1_minutes = std::max(1, st.config.t1_minutes - 5);
        else
          st.config.t2_minutes = std::max(0, st.config.t2_minutes - 5);
        try_save(data_path, st);
      } else if (ev.kind == term::KeyKind::Char && (ev.ch == 's' || ev.ch == 'S')) {
        try_save(data_path, st);
        ui.mode = UiMode::List;
      }
      continue;
    }

    auto list = snapshot_reminders(st);
    clamp_selection(ui.sel_index, list);

    if (ev.kind == term::KeyKind::Char && (ev.ch == 'q' || ev.ch == 'Q')) {
      st.shutdown_requested = true;
      break;
    }
    if (ev.kind == term::KeyKind::Char && (ev.ch == 's' || ev.ch == 'S')) {
      ui.mode = UiMode::Settings;
      continue;
    }
    if (ev.kind == term::KeyKind::Char && (ev.ch == 'a' || ev.ch == 'A')) {
      tm.suspend_line();
      std::cout << "\033[2J\033[H\033[1;32mAdd reminder\033[0m\n\n";
      std::cout << "Title: ";
      std::cout.flush();
      std::string title;
      if (!read_dialog_line(title, st)) {
        tm.resume_raw();
        continue;
      }
      std::cout << "Description (optional): ";
      std::cout.flush();
      std::string desc;
      if (!read_dialog_line(desc, st)) {
        tm.resume_raw();
        continue;
      }
      std::cout << "Due date/time (optional, free text): ";
      std::cout.flush();
      std::string due;
      if (!read_dialog_line(due, st)) {
        tm.resume_raw();
        continue;
      }
      Reminder nr;
      nr.id = next_id(st);
      nr.title = title.empty() ? "(untitled)" : title;
      nr.description = desc;
      nr.due = due;
      nr.done = false;
      {
        std::lock_guard<std::mutex> lock(st.mu);
        st.reminders.push_back(std::move(nr));
      }
      try_save(data_path, st);
      tm.resume_raw();
      continue;
    }

    if (ev.kind == term::KeyKind::Char && (ev.ch == 'e' || ev.ch == 'E')) {
      if (list.empty() || ui.sel_index >= list.size()) continue;
      std::uint64_t id = list[ui.sel_index].id;
      auto cur = find_by_id(st, id);
      if (!cur) continue;
      tm.suspend_line();
      std::cout << "\033[2J\033[H\033[1;33mEdit reminder #" << id << "\033[0m\n\n";
      std::cout << "Title [" << cur->title << "]: ";
      std::cout.flush();
      std::string title;
      if (!read_dialog_line(title, st)) {
        tm.resume_raw();
        continue;
      }
      if (title.empty()) title = cur->title;
      std::cout << "Description [" << cur->description << "]: ";
      std::cout.flush();
      std::string desc;
      if (!read_dialog_line(desc, st)) {
        tm.resume_raw();
        continue;
      }
      if (desc.empty()) desc = cur->description;
      std::cout << "Due [" << cur->due << "]: ";
      std::cout.flush();
      std::string due;
      if (!read_dialog_line(due, st)) {
        tm.resume_raw();
        continue;
      }
      if (due.empty()) due = cur->due;
      {
        std::lock_guard<std::mutex> lock(st.mu);
        for (auto& r : st.reminders) {
          if (r.id == id) {
            r.title = title;
            r.description = desc;
            r.due = due;
            break;
          }
        }
      }
      try_save(data_path, st);
      tm.resume_raw();
      continue;
    }

    if (ev.kind == term::KeyKind::Char && (ev.ch == 'd' || ev.ch == 'D')) {
      if (list.empty() || ui.sel_index >= list.size()) continue;
      std::uint64_t id = list[ui.sel_index].id;
      {
        std::lock_guard<std::mutex> lock(st.mu);
        st.reminders.erase(std::remove_if(st.reminders.begin(), st.reminders.end(),
                                          [&](const Reminder& r) { return r.id == id; }),
                           st.reminders.end());
      }
      try_save(data_path, st);
      clamp_selection(ui.sel_index, snapshot_reminders(st));
      continue;
    }

    if (ev.kind == term::KeyKind::Char && ev.ch == ' ') {
      if (list.empty() || ui.sel_index >= list.size()) continue;
      std::uint64_t id = list[ui.sel_index].id;
      {
        std::lock_guard<std::mutex> lock(st.mu);
        for (auto& r : st.reminders) {
          if (r.id == id) {
            r.done = !r.done;
            break;
          }
        }
      }
      try_save(data_path, st);
      continue;
    }

    if (ev.kind == term::KeyKind::ArrowUp) {
      if (ui.sel_index > 0) --ui.sel_index;
      continue;
    }
    if (ev.kind == term::KeyKind::ArrowDown) {
      if (ui.sel_index + 1 < list.size()) ++ui.sel_index;
      continue;
    }
  }

  st.shutdown_requested = true;
  th1.join();
  th2.join();
  const bool saved_ok = try_save(data_path, st);
  tm.restore_now();
  std::cout << "\033[2J\033[H\033[?25h";
  if (saved_ok)
    std::cout << "Reminders saved. Goodbye.\n";
  else
    std::cout << "Goodbye. (Warning: last save to disk failed — check permissions or disk space.)\n";
  return saved_ok ? 0 : 1;
}
