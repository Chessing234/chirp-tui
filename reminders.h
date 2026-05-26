// reminders.h — shared types and constants for the reminder TUI daemon.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rem {

inline constexpr const char* kDefaultDataFilename = ".reminders.json";
inline constexpr const char* kVersion = "1.2.1";

struct Reminder {
  std::uint64_t id = 0;
  std::string title;
  std::string description;  // optional
  std::string due;          // optional free text
  bool done = false;
};

struct AppConfig {
  int t1_minutes = 60;
  /// 0 = second timer disabled (no T2 popups). Use settings or --t2 to enable.
  int t2_minutes = 0;
  bool bell_on_popup = true;
};

// Thread-safe store for reminders + settings + timer anchors.
struct AppState {
  mutable std::mutex mu;
  std::vector<Reminder> reminders;
  AppConfig config;

  // Last time each timer fired (steady_clock). Used for countdown display.
  std::chrono::steady_clock::time_point last_t1_fire{};
  std::chrono::steady_clock::time_point last_t2_fire{};

  // Overlay requests from timer threads (main thread clears after showing).
  std::atomic<bool> overlay_t1_pending{false};
  std::atomic<bool> overlay_t2_pending{false};
  std::atomic<bool> shutdown_requested{false};
};

}  // namespace rem
