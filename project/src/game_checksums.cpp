// game_checksums.cpp - accept the script files changed by scripts/build_game_overlay.py.
//
// The Harmonix engine keeps a SHA-1 of every script it loads from the disc archive in
// a table in the executable's .data section (gFileChecksums in the RB3 decomp): 580
// entries of { char* path; u8 sha1[20]; } at 0x829F4C48..0x829F82A8, e.g.
// "./config/gen/main_menu.dtb". A file whose hash does not match makes the game show
// "Disc Read Error" (XamShowDirtyDiscErrorUI) at boot.
//
// build_game_overlay.py writes checksums.txt at the root of the overlay ("<path>
// <sha1 hex>" per modified file). Right after the XEX is mapped, and before the game
// runs, ApplyGameChecksums replaces the matching table entries in guest memory.

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include <rex/logging.h>

namespace {

constexpr uint32_t kTableStart = 0x829F4C48;
constexpr uint32_t kTableEnd = 0x829F82A8;
constexpr uint32_t kEntrySize = 24;  // char* path + 20-byte SHA-1

bool ParseSha1(const std::string& hex, std::array<uint8_t, 20>& out) {
  if (hex.size() != 40) {
    return false;
  }
  for (size_t i = 0; i < 20; ++i) {
    unsigned v = 0;
    if (std::sscanf(hex.c_str() + 2 * i, "%2x", &v) != 1) {
      return false;
    }
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

}  // namespace

void ApplyGameChecksums(uint8_t* base, const std::filesystem::path& game_data_root) {
  const auto list = game_data_root / "checksums.txt";
  std::ifstream in(list);
  if (!in) {
    return;  // stock game data: nothing to replace
  }
  std::unordered_map<std::string, std::array<uint8_t, 20>> wanted;
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string path, hex;
    std::array<uint8_t, 20> sha1;
    if (!(ss >> path >> hex) || !ParseSha1(hex, sha1)) {
      REXLOG_WARN("checksums: bad line in {}: '{}'", list.string(), line);
      continue;
    }
    wanted[path] = sha1;
  }

  size_t replaced = 0;
  for (uint32_t entry = kTableStart; entry < kTableEnd; entry += kEntrySize) {
    uint32_t path_ptr = std::byteswap(*reinterpret_cast<const uint32_t*>(base + entry));
    if (path_ptr < 0x82000000u || path_ptr >= 0x82B90000u) {
      REXLOG_ERROR("checksums: unexpected table layout at {:08X}; not patched", entry);
      return;
    }
    const char* path = reinterpret_cast<const char*>(base + path_ptr);
    auto it = wanted.find(path);
    if (it == wanted.end()) {
      continue;
    }
    std::memcpy(base + entry + 4, it->second.data(), it->second.size());
    wanted.erase(it);
    ++replaced;
  }
  for (const auto& [path, sha1] : wanted) {
    (void)sha1;
    REXLOG_WARN("checksums: '{}' is not in the game's checksum table", path);
  }
  REXLOG_INFO("checksums: accepted {} modified game script(s)", replaced);
}
