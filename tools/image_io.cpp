#include "image_io.hpp"

#include <cctype>
#include <cstdio>
#include <vector>

namespace nota_tools {
namespace {

// PGM 헤더의 토큰 하나를 읽는다. 공백을 건너뛰고 '#' 주석 줄을 무시한다.
bool ReadToken(std::FILE* f, std::string* token) {
  token->clear();
  int c = 0;
  // 공백·주석 건너뛰기
  for (;;) {
    c = std::fgetc(f);
    if (c == EOF) return false;
    if (c == '#') {
      while (c != '\n' && c != EOF) c = std::fgetc(f);
      continue;
    }
    if (!std::isspace(c)) break;
  }
  // 토큰 수집
  while (c != EOF && !std::isspace(c)) {
    token->push_back(static_cast<char>(c));
    c = std::fgetc(f);
  }
  return !token->empty();
}

}  // namespace

nota::GrayImage ReadPgm(const std::string& path, std::string* error) {
  const auto fail = [&](const std::string& msg) {
    if (error) *error = msg;
    return nota::GrayImage();
  };

  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return fail("cannot open: " + path);

  std::string magic;
  if (!ReadToken(f, &magic) || magic != "P5") {
    std::fclose(f);
    return fail("not a binary PGM (expected P5): " + path);
  }

  std::string w_tok;
  std::string h_tok;
  std::string max_tok;
  if (!ReadToken(f, &w_tok) || !ReadToken(f, &h_tok) || !ReadToken(f, &max_tok)) {
    std::fclose(f);
    return fail("truncated PGM header: " + path);
  }

  const long w = std::strtol(w_tok.c_str(), nullptr, 10);
  const long h = std::strtol(h_tok.c_str(), nullptr, 10);
  const long maxval = std::strtol(max_tok.c_str(), nullptr, 10);
  if (w <= 0 || h <= 0) {
    std::fclose(f);
    return fail("bad PGM dimensions: " + path);
  }
  if (maxval != 255) {
    std::fclose(f);
    return fail("only 8-bit PGM is supported (maxval=" + max_tok + ")");
  }

  // 헤더의 마지막 공백 한 글자를 소비한다. ReadToken이 이미 소비했다.
  nota::GrayImage img(static_cast<int>(w), static_cast<int>(h));
  const std::size_t n = img.pixels.size();
  const std::size_t got = std::fread(img.pixels.data(), 1, n, f);
  std::fclose(f);

  if (got != n) {
    return fail("truncated PGM data: expected " + std::to_string(n) + " bytes, got " +
                std::to_string(got));
  }
  if (error) error->clear();
  return img;
}

bool WritePpm(const std::string& path, const RgbImage& image, std::string* error) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    if (error) *error = "cannot write: " + path;
    return false;
  }
  std::fprintf(f, "P6\n%d %d\n255\n", image.width, image.height);
  const std::size_t n = image.rgb.size();
  const bool ok = std::fwrite(image.rgb.data(), 1, n, f) == n;
  std::fclose(f);
  if (!ok && error) *error = "short write: " + path;
  return ok;
}

}  // namespace nota_tools
