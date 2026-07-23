#pragma once

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "hound/document.hpp"
#include "hound/fuzzy_index.hpp"

namespace hound {

namespace detail {

inline void write_u32(std::ostream& out, std::uint32_t v) {
  out.put(static_cast<char>(v & 0xff));
  out.put(static_cast<char>((v >> 8) & 0xff));
  out.put(static_cast<char>((v >> 16) & 0xff));
  out.put(static_cast<char>((v >> 24) & 0xff));
}

inline std::uint32_t read_u32(std::istream& in) {
  unsigned char b0 = static_cast<unsigned char>(in.get());
  unsigned char b1 = static_cast<unsigned char>(in.get());
  unsigned char b2 = static_cast<unsigned char>(in.get());
  unsigned char b3 = static_cast<unsigned char>(in.get());
  if (!in) {
    throw std::runtime_error("snapshot: truncated integer");
  }
  return static_cast<std::uint32_t>(b0) | (static_cast<std::uint32_t>(b1) << 8) |
         (static_cast<std::uint32_t>(b2) << 16) | (static_cast<std::uint32_t>(b3) << 24);
}

inline void write_u64(std::ostream& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.put(static_cast<char>((v >> (8 * i)) & 0xff));
  }
}

inline std::uint64_t read_u64(std::istream& in) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    unsigned char b = static_cast<unsigned char>(in.get());
    if (!in) {
      throw std::runtime_error("snapshot: truncated u64");
    }
    v |= static_cast<std::uint64_t>(b) << (8 * i);
  }
  return v;
}

inline void write_string(std::ostream& out, const std::string& s) {
  if (s.size() > 0xffffffffu) {
    throw std::runtime_error("snapshot: string too long");
  }
  write_u32(out, static_cast<std::uint32_t>(s.size()));
  out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

inline std::string read_string(std::istream& in) {
  const std::uint32_t n = read_u32(in);
  std::string s(n, '\0');
  if (n > 0) {
    in.read(s.data(), static_cast<std::streamsize>(n));
    if (!in) {
      throw std::runtime_error("snapshot: truncated string");
    }
  }
  return s;
}

inline void write_f64(std::ostream& out, double v) {
  static_assert(sizeof(double) == 8, "unexpected double size");
  const auto* bytes = reinterpret_cast<const unsigned char*>(&v);
  for (int i = 0; i < 8; ++i) {
    out.put(static_cast<char>(bytes[i]));
  }
}

inline double read_f64(std::istream& in) {
  unsigned char bytes[8];
  for (int i = 0; i < 8; ++i) {
    bytes[i] = static_cast<unsigned char>(in.get());
    if (!in) {
      throw std::runtime_error("snapshot: truncated double");
    }
  }
  double v = 0.0;
  auto* dest = reinterpret_cast<unsigned char*>(&v);
  for (int i = 0; i < 8; ++i) {
    dest[i] = bytes[i];
  }
  return v;
}

}  // namespace detail

inline constexpr std::uint32_t kSnapshotMagic = 0x484e4453;  // 'HNDS'
inline constexpr std::uint32_t kSnapshotVersion = 1;

inline void save_snapshot(const FuzzyIndex& index, const std::string& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("snapshot: cannot open for write: " + path);
  }
  detail::write_u32(out, kSnapshotMagic);
  detail::write_u32(out, kSnapshotVersion);
  detail::write_u64(out, static_cast<std::uint64_t>(index.size()));
  for (const auto& [id, doc] : index.documents()) {
    detail::write_string(out, doc.id);
    detail::write_string(out, doc.text);
    detail::write_f64(out, doc.external_score);
  }
  if (!out) {
    throw std::runtime_error("snapshot: write failed");
  }
}

inline void load_snapshot(FuzzyIndex& index, const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("snapshot: cannot open for read: " + path);
  }
  const auto magic = detail::read_u32(in);
  if (magic != kSnapshotMagic) {
    throw std::runtime_error("snapshot: bad magic");
  }
  const auto version = detail::read_u32(in);
  if (version != kSnapshotVersion) {
    throw std::runtime_error("snapshot: unsupported version");
  }
  const auto count = detail::read_u64(in);
  index.clear();
  for (std::uint64_t i = 0; i < count; ++i) {
    Document doc;
    doc.id = detail::read_string(in);
    doc.text = detail::read_string(in);
    doc.external_score = detail::read_f64(in);
    index.upsert(std::move(doc));
  }
}

}  // namespace hound
