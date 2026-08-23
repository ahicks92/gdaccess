#include "db.h"
#include "log.h"
#include <sqlite3.h>

namespace gd::db {

Db::~Db() { close(); }

bool Db::open_readonly(const std::string& path) {
  close();
  int rc = sqlite3_open_v2(path.c_str(), &h_, SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    log::writef("db: open '{}' failed: {}", path, h_ ? sqlite3_errmsg(h_) : sqlite3_errstr(rc));
    close();
    return false;
  }
  return true;
}

void Db::close() {
  if (h_) { sqlite3_close_v2(h_); h_ = nullptr; }
}

std::string Db::error() const { return h_ ? sqlite3_errmsg(h_) : "not open"; }

Stmt::Stmt(Db& db, const char* sql) {
  if (!db.ok()) return;
  if (sqlite3_prepare_v2(db.raw(), sql, -1, &s_, nullptr) != SQLITE_OK) {
    log::writef("db: prepare failed: {} -- {}", db.error(), sql);
    s_ = nullptr;
  }
}
Stmt::~Stmt() { if (s_) sqlite3_finalize(s_); }
Stmt& Stmt::bind(int index, std::string_view text) { if (s_) sqlite3_bind_text(s_, index, text.data(), (int)text.size(), SQLITE_TRANSIENT); return *this; }
Stmt& Stmt::bind(int index, long long value) { if (s_) sqlite3_bind_int64(s_, index, value); return *this; }
Stmt& Stmt::bind(int index, double value) { if (s_) sqlite3_bind_double(s_, index, value); return *this; }
bool Stmt::step() {
  if (!s_) return false;
  int rc = sqlite3_step(s_);
  if (rc == SQLITE_ROW) return true;
  if (rc != SQLITE_DONE) log::writef("db: step failed: {}", sqlite3_errstr(rc));
  return false;
}
void Stmt::reset() { if (s_) { sqlite3_reset(s_); sqlite3_clear_bindings(s_); } }
bool Stmt::is_null(int col) const { return !s_ || sqlite3_column_type(s_, col) == SQLITE_NULL; }
long long Stmt::int64(int col) const { return s_ ? sqlite3_column_int64(s_, col) : 0; }
double Stmt::real(int col) const { return s_ ? sqlite3_column_double(s_, col) : 0.0; }
std::string Stmt::text(int col) const {
  if (!s_) return {};
  const unsigned char* t = sqlite3_column_text(s_, col);
  return t ? std::string(reinterpret_cast<const char*>(t), (size_t)sqlite3_column_bytes(s_, col)) : std::string();
}
std::vector<uint8_t> Stmt::blob(int col) const {
  if (!s_) return {};
  const void* p = sqlite3_column_blob(s_, col);
  int n = sqlite3_column_bytes(s_, col);
  return p && n > 0 ? std::vector<uint8_t>((const uint8_t*)p, (const uint8_t*)p + n) : std::vector<uint8_t>();
}

void shutdown() { sqlite3_shutdown(); }
std::string version() { return sqlite3_libversion(); }

}  // namespace gd::db
