#pragma once
// Thin RAII wrapper over the vendored SQLite (third_party/sqlite). The mod only READS databases shipped under
// assets/ (rooms.db); all writes happen in the Python tools. Close every Db before the DLL unloads (hot
// reload): dllmain calls db::shutdown() after the last owner is gone.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace gd::db {

class Db {
 public:
  Db() = default;
  ~Db();
  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;
  bool open_readonly(const std::string& path);   // false + error() on failure (missing file included)
  void close();
  bool ok() const { return h_ != nullptr; }
  sqlite3* raw() const { return h_; }
  std::string error() const;

 private:
  sqlite3* h_ = nullptr;
};

class Stmt {
 public:
  Stmt(Db& db, const char* sql);
  ~Stmt();
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
  bool ok() const { return s_ != nullptr; }
  Stmt& bind(int index, std::string_view text);
  Stmt& bind(int index, long long value);
  Stmt& bind(int index, double value);
  bool step();                       // true = a row is available; false = done or error
  void reset();
  bool is_null(int col) const;
  long long int64(int col) const;
  double real(int col) const;
  std::string text(int col) const;
  std::vector<uint8_t> blob(int col) const;

 private:
  sqlite3_stmt* s_ = nullptr;
};

void shutdown();                      // sqlite3_shutdown(); after every Db is closed
std::string version();                // library version string (dev)

}  // namespace gd::db
