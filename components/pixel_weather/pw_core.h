// pw_core.h - portable part of Pixel Weather (no Arduino dependencies, so it can be
// unit-tested on a PC). Renders the 16x16 frames, encodes them as a GIF, and wraps the
// GIF in iDotMatrix upload packets.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include "icons.h"

struct RGB { uint8_t r, g, b; };
struct Frame { RGB px[256]; };

struct Weather {
  float temp; int code; bool isDay; float hi, lo;
  uint8_t rain[16];            // precipitation probability %, next 16 hours
};

// ------------------------------------------------------------ conditions ----
inline int conditionIcon(int code, bool day) {
  if (code == 0 || code == 1) return day ? ICON_SUN : ICON_MOON;
  if (code == 2)              return day ? ICON_PARTLY : ICON_PARTLY_NIGHT;
  if (code == 3)              return ICON_CLOUD;
  if (code == 45 || code == 48) return ICON_FOG;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return ICON_SNOW;
  if (code >= 95)             return ICON_STORM;
  if (code >= 51)             return ICON_RAIN;
  return ICON_CLOUD;
}

// ---------------------------------------------------------------- colours ----
static const RGB DGR = {60, 62, 75}, CYN = {90, 220, 255};

inline RGB tempColour(float t) {
  static const float T[5] = {5, 15, 22, 30, 38};
  static const RGB   C[5] = {{60,120,255}, {40,220,120}, {240,220,40}, {255,130,20}, {255,40,40}};
  if (t <= T[0]) return C[0];
  for (int i = 0; i < 4; i++) {
    if (t <= T[i + 1]) {
      float f = (t - T[i]) / (T[i + 1] - T[i]);
      return { (uint8_t)(C[i].r + (C[i+1].r - C[i].r) * f),
               (uint8_t)(C[i].g + (C[i+1].g - C[i].g) * f),
               (uint8_t)(C[i].b + (C[i+1].b - C[i].b) * f) };
    }
  }
  return C[4];
}

// ------------------------------------------------------------------ fonts ----
struct Glyph { char ch; const char* rows[7]; };
static const Glyph BIG[] = {   // 4x7
  {'0',{".##.","#..#","#..#","#..#","#..#","#..#",".##."}}, {'1',{"..#.",".##.","..#.","..#.","..#.","..#.",".###"}},
  {'2',{".##.","#..#","...#","..#.",".#..","#...","####"}}, {'3',{"###.","...#","...#",".##.","...#","...#","###."}},
  {'4',{"#..#","#..#","#..#","####","...#","...#","...#"}}, {'5',{"####","#...","###.","...#","...#","#..#",".##."}},
  {'6',{".##.","#...","#...","###.","#..#","#..#",".##."}}, {'7',{"####","...#","..#.","..#.",".#..",".#..",".#.."}},
  {'8',{".##.","#..#","#..#",".##.","#..#","#..#",".##."}}, {'9',{".##.","#..#","#..#",".###","...#","...#",".##."}},
  {'-',{"....","....","....","###.","....","....","...."}},
};
static const Glyph SMALL[] = { // 3x5
  {'0',{"###","#.#","#.#","#.#","###"}}, {'1',{".#.","##.",".#.",".#.","###"}}, {'2',{"###","..#","###","#..","###"}},
  {'3',{"###","..#",".##","..#","###"}}, {'4',{"#.#","#.#","###","..#","..#"}}, {'5',{"###","#..","###","..#","###"}},
  {'6',{"###","#..","###","#.#","###"}}, {'7',{"###","..#",".#.",".#.",".#."}}, {'8',{"###","#.#","###","#.#","###"}},
  {'9',{"###","#.#","###","..#","###"}}, {'-',{"...","...","###","...","..."}},
};
static const char* DEGREE[] = {".#.", "#.#", ".#."};

inline void setPx(Frame& f, int x, int y, RGB c) {
  if (x >= 0 && x < 16 && y >= 0 && y < 16) f.px[y * 16 + x] = c;
}
inline int drawRows(Frame& f, int x, int y, const char* const* rows, int n, RGB c) {
  for (int dy = 0; dy < n; dy++)
    for (int dx = 0; rows[dy][dx]; dx++)
      if (rows[dy][dx] == '#') setPx(f, x + dx, y + dy, c);
  return (int)strlen(rows[0]);
}
template <size_t N>
inline int drawText(Frame& f, int x, int y, const std::string& s, const Glyph (&font)[N], int h, RGB c) {
  for (char ch : s)
    for (size_t i = 0; i < N; i++)
      if (font[i].ch == ch) { x += drawRows(f, x, y, font[i].rows, h, c) + 1; break; }
  return x;
}

// Python-compatible helpers: str(round(v))[-2:]
inline std::string lastTwo(long v) { std::string s = std::to_string(v); return s.size() > 2 ? s.substr(s.size() - 2) : s; }
inline long pyRound(float v) { return lrintf(v); }   // round-half-even, like Python

// ---------------------------------------------------------------- screens ----
inline void renderIcon(Frame& f, const Weather& w, int frameNo) {
  const uint8_t* p = ICONS[conditionIcon(w.code, w.isDay)][frameNo % ICON_FRAMES];
  for (int i = 0; i < 256; i++) f.px[i] = {p[i*3], p[i*3+1], p[i*3+2]};
}

inline void renderData(Frame& f, const Weather& w) {
  memset(&f, 0, sizeof f);
  long t = pyRound(w.temp);
  std::string s = t >= 0 ? lastTwo(t) : "-" + std::to_string(-t).substr(std::to_string(-t).size() - 1);
  int width = (int)s.size() * 5 + 3;                       // 4px digits + 1 gap, plus degree sign
  RGB tc = tempColour((float)t);
  int x = drawText(f, (16 - width) / 2, 1, s, BIG, 7, tc);
  drawRows(f, x, 1, DEGREE, 3, tc);
  std::string hi = lastTwo(pyRound(w.hi)), lo = lastTwo(pyRound(w.lo));
  drawText(f, 0, 9, hi, SMALL, 5, tempColour(w.hi));
  drawText(f, 16 - (4 * (int)lo.size() - 1), 9, lo, SMALL, 5, tempColour(w.lo));
  for (int i = 0; i < 16; i++) {                           // rain chance strip
    float k = w.rain[i] / 100.0f;
    setPx(f, i, 15, k == 0 ? DGR : RGB{(uint8_t)(30 + (CYN.r - 30) * k),
                                       (uint8_t)(30 + (CYN.g - 30) * k),
                                       (uint8_t)(30 + (CYN.b - 30) * k)});
  }
}

// Same sequence as the laptop: 8 animated icon frames (350 ms), then the data screen (3.5 s)
inline int buildFrames(const Weather& w, Frame* frames, uint16_t* delaysCs) {
  for (int i = 0; i < ICON_FRAMES * 2; i++) { renderIcon(frames[i], w, i); delaysCs[i] = 35; }
  renderData(frames[ICON_FRAMES * 2], w);
  delaysCs[ICON_FRAMES * 2] = 350;
  return ICON_FRAMES * 2 + 1;
}

// ------------------------------------------------------------ GIF encoder ----
// Uncompressed-LZW GIF: 9-bit literal codes with a clear code every 254 pixels so the
// code table never grows. Tiny, no tables needed, and a 16x16 x 9-frame GIF stays ~3.5 KB.
class BitWriter {
 public:
  std::vector<uint8_t> bytes; uint32_t acc = 0; int n = 0;
  void put(uint16_t code, int bits) { acc |= (uint32_t)code << n; n += bits;
    while (n >= 8) { bytes.push_back(acc & 0xFF); acc >>= 8; n -= 8; } }
  void flush() { if (n > 0) bytes.push_back(acc & 0xFF); acc = 0; n = 0; }
};

inline std::vector<uint8_t> encodeGif(const Frame* frames, const uint16_t* delaysCs, int count) {
  std::vector<RGB> pal;                                    // global palette of every colour used
  std::vector<uint8_t> idx(count * 256);
  for (int f = 0; f < count; f++)
    for (int i = 0; i < 256; i++) {
      const RGB& c = frames[f].px[i]; size_t k = 0;
      while (k < pal.size() && !(pal[k].r == c.r && pal[k].g == c.g && pal[k].b == c.b)) k++;
      if (k == pal.size()) { if (pal.size() < 256) pal.push_back(c); else k = 0; }
      idx[f * 256 + i] = (uint8_t)k;
    }
  std::vector<uint8_t> g;
  auto u16 = [&](uint16_t v) { g.push_back(v & 0xFF); g.push_back(v >> 8); };
  const char* sig = "GIF89a"; g.insert(g.end(), sig, sig + 6);
  u16(16); u16(16); g.push_back(0xF7); g.push_back(0); g.push_back(0);   // 256-entry global table
  for (int k = 0; k < 256; k++) {
    RGB c = k < (int)pal.size() ? pal[k] : RGB{0, 0, 0};
    g.push_back(c.r); g.push_back(c.g); g.push_back(c.b);
  }
  const uint8_t loop[] = {0x21, 0xFF, 0x0B, 'N','E','T','S','C','A','P','E','2','.','0', 0x03, 0x01, 0, 0, 0};
  g.insert(g.end(), loop, loop + sizeof loop);
  for (int f = 0; f < count; f++) {
    g.push_back(0x21); g.push_back(0xF9); g.push_back(4); g.push_back(2 << 2);   // disposal 2
    u16(delaysCs[f]); g.push_back(0); g.push_back(0);
    g.push_back(0x2C); u16(0); u16(0); u16(16); u16(16); g.push_back(0);
    g.push_back(8);                                        // LZW minimum code size
    BitWriter bw; bw.put(256, 9);
    for (int i = 0, run = 0; i < 256; i++, run++) {
      if (run == 254) { bw.put(256, 9); run = 0; }
      bw.put(idx[f * 256 + i], 9);
    }
    bw.put(257, 9); bw.flush();
    for (size_t o = 0; o < bw.bytes.size(); o += 255) {   // 255-byte sub-blocks
      size_t len = bw.bytes.size() - o < 255 ? bw.bytes.size() - o : 255;
      g.push_back((uint8_t)len); g.insert(g.end(), bw.bytes.begin() + o, bw.bytes.begin() + o + len);
    }
    g.push_back(0);
  }
  g.push_back(0x3B);
  return g;
}

// ------------------------------------------------------ iDotMatrix packets ----
inline uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); }
  return ~c;
}

// Splits the GIF into 4 KB chunks, each with the 16-byte upload header (same as idm.py)
inline std::vector<std::vector<uint8_t>> idmGifPackets(const std::vector<uint8_t>& gif) {
  std::vector<std::vector<uint8_t>> out;
  uint8_t h[16] = {255,255,1,0,0, 255,255,255,255, 255,255,255,255, 5,0,13};
  uint32_t len = gif.size(), crc = crc32(gif.data(), gif.size());
  for (int i = 0; i < 4; i++) { h[5 + i] = (len >> (8 * i)) & 0xFF; h[9 + i] = (crc >> (8 * i)) & 0xFF; }
  for (size_t o = 0, n = 0; o < gif.size(); o += 4096, n++) {
    size_t cl = gif.size() - o < 4096 ? gif.size() - o : 4096;
    h[4] = n ? 2 : 0;
    uint16_t pl = (uint16_t)(cl + 16); h[0] = pl & 0xFF; h[1] = pl >> 8;
    std::vector<uint8_t> p(h, h + 16); p.insert(p.end(), gif.begin() + o, gif.begin() + o + cl);
    out.push_back(p);
  }
  return out;
}
