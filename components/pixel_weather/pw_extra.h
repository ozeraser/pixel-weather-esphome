// pw_extra.h - screens added for the ESPHome version (UV, lightning warning, scrolling message)
// and the Home Assistant condition mapping. Builds on pw_core.h, which stays unchanged.
#pragma once
#include <ctype.h>
#include "pw_core.h"

// Home Assistant weather condition -> the Open-Meteo style code that conditionIcon() understands
inline int haConditionCode(const std::string& c) {
  if (c == "sunny" || c == "clear-night") return 0;
  if (c == "partlycloudy") return 2;
  if (c == "fog") return 45;
  if (c == "rainy" || c == "pouring" || c == "hail") return 61;
  if (c == "snowy" || c == "snowy-rainy") return 71;
  if (c == "lightning" || c == "lightning-rainy") return 95;
  return 3;   // cloudy, windy, windy-variant, exceptional, unknown
}

// 3x5 font for letters, digits and a little punctuation (also used for the scrolling message)
static const Glyph TINY[] = {
  {'A',{".#.","#.#","###","#.#","#.#"}}, {'B',{"##.","#.#","##.","#.#","##."}}, {'C',{".##","#..","#..","#..",".##"}},
  {'D',{"##.","#.#","#.#","#.#","##."}}, {'E',{"###","#..","##.","#..","###"}}, {'F',{"###","#..","##.","#..","#.."}},
  {'G',{".##","#..","#.#","#.#",".##"}}, {'H',{"#.#","#.#","###","#.#","#.#"}}, {'I',{"###",".#.",".#.",".#.","###"}},
  {'J',{"..#","..#","..#","#.#",".#."}}, {'K',{"#.#","#.#","##.","#.#","#.#"}}, {'L',{"#..","#..","#..","#..","###"}},
  {'M',{"#.#","###","###","#.#","#.#"}}, {'N',{"##.","#.#","#.#","#.#","#.#"}}, {'O',{".#.","#.#","#.#","#.#",".#."}},
  {'P',{"##.","#.#","##.","#..","#.."}}, {'Q',{".#.","#.#","#.#","##.",".##"}}, {'R',{"##.","#.#","##.","#.#","#.#"}},
  {'S',{".##","#..",".#.","..#","##."}}, {'T',{"###",".#.",".#.",".#.",".#."}}, {'U',{"#.#","#.#","#.#","#.#","###"}},
  {'V',{"#.#","#.#","#.#","#.#",".#."}}, {'W',{"#.#","#.#","###","###","#.#"}}, {'X',{"#.#","#.#",".#.","#.#","#.#"}},
  {'Y',{"#.#","#.#",".#.",".#.",".#."}}, {'Z',{"###","..#",".#.","#..","###"}},
  {'0',{"###","#.#","#.#","#.#","###"}}, {'1',{".#.","##.",".#.",".#.","###"}}, {'2',{"###","..#","###","#..","###"}},
  {'3',{"###","..#",".##","..#","###"}}, {'4',{"#.#","#.#","###","..#","..#"}}, {'5',{"###","#..","###","..#","###"}},
  {'6',{"###","#..","###","#.#","###"}}, {'7',{"###","..#",".#.",".#.",".#."}}, {'8',{"###","#.#","###","#.#","###"}},
  {'9',{"###","#.#","###","..#","###"}},
  {' ',{"..","..","..","..",".."}},      {'-',{"...","...","###","...","..."}}, {'.',{".",".",".",".","#"}},
  {'!',{"#","#","#",".","#"}},           {'?',{"##.","..#",".#.","...",".#."}}, {':',{".","#",".","#","."}},
  {'\'',{"#","#",".",".","."}},          {',',{"..","..","..",".#","#."}},     {'/',{"..#","..#",".#.","#..","#.."}},
  {'+',{"...",".#.","###",".#.","..."}}, {'%',{"#.#","..#",".#.","#..","#.#"}}, {'#',{"#.#","###","#.#","###","#.#"}},
};

inline int textWidth(const std::string& s) {
  int w = 0;
  for (char ch : s)
    for (const Glyph& g : TINY)
      if (g.ch == ch) { w += (int)strlen(g.rows[0]) + 1; break; }
  return w > 0 ? w - 1 : 0;
}

inline std::string upper(std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; }

// --------------------------------------------------------------------- UV ----
inline RGB uvColour(int uv) {        // standard UV index colours
  if (uv <= 2)  return {40, 220, 120};
  if (uv <= 5)  return {240, 220, 40};
  if (uv <= 7)  return {255, 130, 20};
  if (uv <= 10) return {255, 40, 40};
  return {180, 60, 255};
}

// "UV" on top, the index in the big font below
inline void renderUv(Frame& f, int uv) {
  memset(&f, 0, sizeof f);
  if (uv > 99) uv = 99;
  RGB c = uvColour(uv);
  drawText(f, (16 - textWidth("UV")) / 2, 0, std::string("UV"), TINY, 5, c);
  std::string s = std::to_string(uv);
  drawText(f, (16 - ((int)s.size() * 5 - 1)) / 2, 7, s, BIG, 7, c);
}

// -------------------------------------------------------------- lightning ----
// Distance in the big font, "KM" underneath, all in warning yellow
inline void renderStrikeDistance(Frame& f, int km) {
  memset(&f, 0, sizeof f);
  if (km < 0) km = 0;
  if (km > 99) km = 99;
  const RGB y = {255, 220, 0};
  std::string s = std::to_string(km);
  drawText(f, (16 - ((int)s.size() * 5 - 1)) / 2, 1, s, BIG, 7, y);
  drawText(f, (16 - textWidth("KM")) / 2, 10, std::string("KM"), TINY, 5, y);
}

// Storm animation, then the distance flashing three times
inline int buildLightningFrames(int km, std::vector<Frame>& frames, std::vector<uint16_t>& delaysCs) {
  Weather storm{};
  storm.code = 95; storm.isDay = true;
  frames.clear(); delaysCs.clear();
  for (int i = 0; i < ICON_FRAMES * 2; i++) {
    frames.emplace_back(); renderIcon(frames.back(), storm, i); delaysCs.push_back(25);
  }
  Frame dist; renderStrikeDistance(dist, km);
  Frame blank; memset(&blank, 0, sizeof blank);
  for (int k = 0; k < 3; k++) {
    frames.push_back(dist);  delaysCs.push_back(60);
    frames.push_back(blank); delaysCs.push_back(15);
  }
  return (int)frames.size();
}

// --------------------------------------------------------------- message ----
// Scrolls the text right-to-left across the middle rows, 2 px per frame
inline int buildMessageFrames(const std::string& text, std::vector<Frame>& frames, std::vector<uint16_t>& delaysCs) {
  const RGB colour = {255, 220, 160};
  std::string s = upper(text);
  int w = textWidth(s);
  frames.clear(); delaysCs.clear();
  for (int off = 16; off > -w; off -= 2) {
    frames.emplace_back();
    Frame& f = frames.back();
    memset(&f, 0, sizeof f);
    drawText(f, off, 5, s, TINY, 5, colour);
    delaysCs.push_back(10);
  }
  if (frames.empty()) { frames.emplace_back(); memset(&frames.back(), 0, sizeof(Frame)); delaysCs.push_back(100); }
  return (int)frames.size();
}

// Normal weather loop from pw_core plus the UV screen when UV is 3 or more
inline int buildWeatherFrames(const Weather& w, int uv, std::vector<Frame>& frames, std::vector<uint16_t>& delaysCs) {
  frames.assign(ICON_FRAMES * 2 + 2, Frame{});
  delaysCs.assign(ICON_FRAMES * 2 + 2, 0);
  int n = buildFrames(w, frames.data(), delaysCs.data());
  if (uv >= 3) { renderUv(frames[n], uv); delaysCs[n] = 250; n++; }
  frames.resize(n); delaysCs.resize(n);
  return n;
}

// ------------------------------------------------------- alarm (buzzer) ----
// The panel has no plain "beep" command, but its alarm clock can sound the buzzer.
// Layout matches the official app 2.1.7 (TimerAgreement.sendData): duration is 16-bit LE in bytes 8-9,
// content 1 = GIF / 2 = image / 3 = text, buzzer 1 = on.

// Set the panel's clock: 0B 00 01 80 YY MM DD WD HH MI SS (weekday 1 = Monday .. 7 = Sunday)
inline std::vector<uint8_t> idmTimeSync(int year, int month, int day, int weekday, int hour, int minute, int second) {
  return {11, 0, 1, 128, (uint8_t)(year % 100), (uint8_t)month, (uint8_t)day, (uint8_t)weekday,
          (uint8_t)hour, (uint8_t)minute, (uint8_t)second};
}

// One-off alarm in `slot` at hour:minute that shows `gif` and sounds the buzzer for `seconds`.
// Each packet repeats the 24-byte header; size and CRC describe the whole GIF.
inline std::vector<std::vector<uint8_t>> idmAlarmPackets(uint8_t slot, int hour, int minute, int seconds,
                                                         const std::vector<uint8_t>& gif) {
  std::vector<std::vector<uint8_t>> out;
  uint8_t h[24] = {0};
  h[2] = 0x00; h[3] = 0x80;
  h[4] = slot;
  h[5] = 0x01;                     // enabled, no weekdays = one-off
  h[6] = (uint8_t)hour; h[7] = (uint8_t)minute;
  h[8] = (uint8_t)(seconds < 1 ? 1 : (seconds > 255 ? 255 : seconds));
  h[10] = 0x01;                    // content: GIF
  h[11] = 0x01;                    // buzzer on
  uint32_t len = gif.size(), crc = crc32(gif.data(), gif.size());
  for (int i = 0; i < 4; i++) { h[13 + i] = (len >> (8 * i)) & 0xFF; h[17 + i] = (crc >> (8 * i)) & 0xFF; }
  for (size_t o = 0, n = 0; o < gif.size(); o += 4096, n++) {
    size_t cl = gif.size() - o < 4096 ? gif.size() - o : 4096;
    h[12] = n ? 2 : 0;             // 0 on the first packet, 2 on the rest
    h[23] = (uint8_t)(slot + 20);  // media id: the official app uses slot + 20
    uint16_t pl = (uint16_t)(cl + 24); h[0] = pl & 0xFF; h[1] = pl >> 8;
    std::vector<uint8_t> p(h, h + 24); p.insert(p.end(), gif.begin() + o, gif.begin() + o + cl);
    out.push_back(p);
  }
  return out;
}
