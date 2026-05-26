// json_store.h — zero-dependency JSON load/save for ~/.reminders.json schema.
#pragma once

#include "reminders.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#include <filesystem>
namespace rem_fs = std::filesystem;

namespace json_store {

inline std::string default_data_path() {
#ifdef _WIN32
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  if (!home || !*home) return rem::kDefaultDataFilename;
  return (rem_fs::path(home) / rem::kDefaultDataFilename).string();
}

// Expands leading "~" or "~/" using HOME / USERPROFILE; otherwise returns p unchanged.
inline std::string expand_user_path(std::string p) {
  if (p.empty()) return p;
  if (p[0] != '~') return p;
  if (p.size() == 1) return default_data_path();
  if (p.size() >= 2 && (p[1] == '/' || p[1] == '\\')) {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home || !*home) return p;
    return (rem_fs::path(home) / rem_fs::path(p.substr(2))).string();
  }
  return p;
}

namespace detail {

inline std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\b': o += "\\b"; break;
      case '\f': o += "\\f"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += static_cast<char>(c);
        }
    }
  }
  return o;
}

inline void skip_ws(const std::string& s, std::size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

inline bool expect(const std::string& s, std::size_t& i, char c) {
  skip_ws(s, i);
  if (i < s.size() && s[i] == c) {
    ++i;
    return true;
  }
  return false;
}

inline bool parse_bool(const std::string& s, std::size_t& i, bool& out) {
  skip_ws(s, i);
  if (s.compare(i, 4, "true") == 0) {
    i += 4;
    out = true;
    return true;
  }
  if (s.compare(i, 5, "false") == 0) {
    i += 5;
    out = false;
    return true;
  }
  return false;
}

inline bool parse_int(const std::string& s, std::size_t& i, int& out) {
  skip_ws(s, i);
  std::size_t j = i;
  if (j < s.size() && (s[j] == '-' || s[j] == '+')) ++j;
  while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
  if (j == i || (j == i + 1 && !std::isdigit(static_cast<unsigned char>(s[i])))) return false;
  try {
    out = std::stoi(s.substr(i, j - i));
  } catch (...) {
    return false;
  }
  i = j;
  return true;
}

inline bool parse_uint64(const std::string& s, std::size_t& i, std::uint64_t& out) {
  skip_ws(s, i);
  std::size_t j = i;
  while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
  if (j == i) return false;
  try {
    out = std::stoull(s.substr(i, j - i));
  } catch (...) {
    return false;
  }
  i = j;
  return true;
}

inline bool parse_string(const std::string& s, std::size_t& i, std::string& out) {
  skip_ws(s, i);
  if (i >= s.size() || s[i] != '"') return false;
  ++i;
  out.clear();
  while (i < s.size()) {
    char c = s[i++];
    if (c == '"') return true;
    if (c == '\\' && i < s.size()) {
      char e = s[i++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          if (i + 4 > s.size()) return false;
          unsigned int cp = 0;
          for (int k = 0; k < 4; ++k) {
            char h = s[i++];
            cp <<= 4;
            if (h >= '0' && h <= '9')
              cp |= static_cast<unsigned int>(h - '0');
            else if (h >= 'a' && h <= 'f')
              cp |= static_cast<unsigned int>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
              cp |= static_cast<unsigned int>(h - 'A' + 10);
            else
              return false;
          }
          if (cp <= 0x7f)
            out += static_cast<char>(cp);
          else
            out += '?';
          break;
        }
        default: out += e; break;
      }
    } else {
      out += c;
    }
  }
  return false;
}

// Skip one JSON value (object, array, string, number, true/false/null). Used for unknown fields.
inline bool skip_json_value(const std::string& s, std::size_t& i) {
  skip_ws(s, i);
  if (i >= s.size()) return false;
  char c = s[i];
  if (c == '"') {
    std::string dummy;
    return parse_string(s, i, dummy);
  }
  if (c == '{') {
    ++i;
    while (true) {
      skip_ws(s, i);
      if (i < s.size() && s[i] == '}') {
        ++i;
        return true;
      }
      std::string key;
      if (!parse_string(s, i, key)) return false;
      if (!expect(s, i, ':')) return false;
      if (!skip_json_value(s, i)) return false;
      skip_ws(s, i);
      if (i < s.size() && s[i] == ',') {
        ++i;
        continue;
      }
      if (expect(s, i, '}')) return true;
      return false;
    }
  }
  if (c == '[') {
    ++i;
    while (true) {
      skip_ws(s, i);
      if (i < s.size() && s[i] == ']') {
        ++i;
        return true;
      }
      if (!skip_json_value(s, i)) return false;
      skip_ws(s, i);
      if (i < s.size() && s[i] == ',') {
        ++i;
        continue;
      }
      if (expect(s, i, ']')) return true;
      return false;
    }
  }
  if (s.compare(i, 4, "true") == 0) {
    i += 4;
    return true;
  }
  if (s.compare(i, 5, "false") == 0) {
    i += 5;
    return true;
  }
  if (s.compare(i, 4, "null") == 0) {
    i += 4;
    return true;
  }
  if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
    if (c == '-') ++i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i < s.size() && s[i] == '.') {
      ++i;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
      ++i;
      if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    }
    return true;
  }
  return false;
}

inline bool parse_reminder_object(const std::string& s, std::size_t& i, rem::Reminder& r) {
  using rem::Reminder;
  if (!expect(s, i, '{')) return false;
  r = Reminder{};
  while (true) {
    skip_ws(s, i);
    if (i < s.size() && s[i] == '}') {
      ++i;
      return true;
    }
    std::string key;
    if (!parse_string(s, i, key)) return false;
    if (!expect(s, i, ':')) return false;
    if (key == "id") {
      if (!parse_uint64(s, i, r.id)) return false;
    } else if (key == "title") {
      if (!parse_string(s, i, r.title)) return false;
    } else if (key == "description") {
      if (!parse_string(s, i, r.description)) return false;
    } else if (key == "due") {
      if (!parse_string(s, i, r.due)) return false;
    } else if (key == "priority") {
      // Legacy field from older saves; ignore value.
      if (!skip_json_value(s, i)) return false;
    } else if (key == "done") {
      if (!parse_bool(s, i, r.done)) return false;
    } else {
      if (!skip_json_value(s, i)) return false;
    }
    skip_ws(s, i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      continue;
    }
    if (expect(s, i, '}')) return true;
    return false;
  }
}

}  // namespace detail

// Keep disk values in sane ranges so timers and UI stay predictable.
inline void normalize_config(rem::AppConfig& cfg) {
  constexpr int kMaxMin = 525600;  // 365 days
  if (cfg.t1_minutes < 1) cfg.t1_minutes = 60;
  if (cfg.t1_minutes > kMaxMin) cfg.t1_minutes = kMaxMin;
  if (cfg.t2_minutes < 0) cfg.t2_minutes = 0;
  if (cfg.t2_minutes > kMaxMin) cfg.t2_minutes = kMaxMin;
}

inline bool load_reminders(const std::string& path, rem::AppState& st) {
  using namespace detail;
  std::ifstream in(path);
  if (!in) return false;
  std::ostringstream oss;
  oss << in.rdbuf();
  std::string s = oss.str();
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
      static_cast<unsigned char>(s[2]) == 0xBF) {
    s.erase(0, 3);
  }
  std::size_t i = 0;
  skip_ws(s, i);
  if (!expect(s, i, '{')) return false;

  rem::AppConfig cfg{};
  std::vector<rem::Reminder> reminders;

  while (true) {
    skip_ws(s, i);
    if (i < s.size() && s[i] == '}') {
      ++i;
      if (i < s.size()) skip_ws(s, i);
      if (i != s.size()) return false;
      normalize_config(cfg);
      {
        std::lock_guard<std::mutex> lock(st.mu);
        st.reminders = std::move(reminders);
        st.config = cfg;
      }
      return true;
    }
    std::string key;
    if (!parse_string(s, i, key)) return false;
    if (!expect(s, i, ':')) return false;
    if (key == "reminders") {
      if (!expect(s, i, '[')) return false;
      while (true) {
        skip_ws(s, i);
        if (i < s.size() && s[i] == ']') {
          ++i;
          break;
        }
        rem::Reminder r;
        if (!parse_reminder_object(s, i, r)) return false;
        reminders.push_back(std::move(r));
        skip_ws(s, i);
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        if (expect(s, i, ']')) break;
        return false;
      }
    } else if (key == "t1_minutes") {
      if (!parse_int(s, i, cfg.t1_minutes)) return false;
    } else if (key == "t2_minutes") {
      if (!parse_int(s, i, cfg.t2_minutes)) return false;
    } else if (key == "bell_on_popup") {
      if (!parse_bool(s, i, cfg.bell_on_popup)) return false;
    } else {
      if (!skip_json_value(s, i)) return false;
    }
    skip_ws(s, i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      continue;
    }
    if (expect(s, i, '}')) {
      skip_ws(s, i);
      if (i != s.size()) return false;
      normalize_config(cfg);
      {
        std::lock_guard<std::mutex> lock(st.mu);
        st.reminders = std::move(reminders);
        st.config = cfg;
      }
      return true;
    }
    return false;
  }
}

inline bool save_reminders(const std::string& path, const rem::AppState& st) {
  using detail::json_escape;
  rem::AppConfig cfg{};
  {
    std::lock_guard<std::mutex> lock(st.mu);
    cfg = st.config;
  }
  normalize_config(cfg);
  std::ostringstream oss;
  oss << "{\n";
  oss << "  \"t1_minutes\": " << cfg.t1_minutes << ",\n";
  oss << "  \"t2_minutes\": " << cfg.t2_minutes << ",\n";
  oss << "  \"bell_on_popup\": " << (cfg.bell_on_popup ? "true" : "false") << ",\n";
  oss << "  \"reminders\": [\n";
  {
    std::lock_guard<std::mutex> lock(st.mu);
    for (std::size_t k = 0; k < st.reminders.size(); ++k) {
      const rem::Reminder& r = st.reminders[k];
      oss << "    {\n";
      oss << "      \"id\": " << r.id << ",\n";
      oss << "      \"title\": \"" << json_escape(r.title) << "\",\n";
      oss << "      \"description\": \"" << json_escape(r.description) << "\",\n";
      oss << "      \"due\": \"" << json_escape(r.due) << "\",\n";
      oss << "      \"done\": " << (r.done ? "true" : "false") << "\n";
      oss << "    }";
      if (k + 1 < st.reminders.size()) oss << ",";
      oss << "\n";
    }
  }
  oss << "  ]\n}\n";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << oss.str();
  return true;
}

}  // namespace json_store
