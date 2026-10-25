/**
 * RGBEffects.h — drives a NeoPixel strip from the ThingsLinker RGB widget.
 *
 *  - Colours are gamma-corrected, so the LEDs match the colour shown in
 *    the app and portal (without it, dark and mixed colours look washed
 *    out and too green).
 *  - Brightness uses strip.setBrightness(), which keeps the colour's hue.
 *  - Plays the effect picked in the widget's Effect list. The effects are
 *    non-blocking: call effects.loop() on every pass of loop().
 *
 * Usage:
 *   RGBEffects effects(strip);
 *   effects.set(r, g, b, on, count, pattern, brightnessPercent);  // on every change
 *   effects.loop();                                               // in loop()
 *
 * Effect names are matched without case and without a trailing " Effect",
 * so "Fire Effect" and "fire" are the same. An unknown name, "Default" or
 * "Solid" shows a steady colour.
 */
#pragma once
#include <Adafruit_NeoPixel.h>

class RGBEffects {
public:
  explicit RGBEffects(Adafruit_NeoPixel& strip) : _s(strip) {}

  void set(uint8_t r, uint8_t g, uint8_t b, bool on, uint16_t count,
           const char* pattern, float brightnessPercent) {
    _r = r; _g = g; _b = b; _on = on;
    _count = count;
    _fx = parse(pattern);
    if (brightnessPercent < 0)   brightnessPercent = 0;
    if (brightnessPercent > 100) brightnessPercent = 100;
    uint8_t level = (uint8_t)(brightnessPercent * 255.0f / 100.0f + 0.5f);
    if (brightnessPercent > 0 && level == 0) level = 1;
    _s.setBrightness(level);
    _start = millis();
    memset(_lv, 0, sizeof(_lv));
    _dirty = true;
  }

  void loop() {
    unsigned long now = millis();
    bool animated = _on && _fx != SOLID;
    if (!_dirty && (!animated || now - _lastFrame < FRAME_MS)) return;
    _lastFrame = now;
    _dirty = false;
    draw(now - _start);
    _s.show();
  }

private:
  enum Fx {
    SOLID, BLINK, PULSE, RAINBOW_CYCLE, RAINBOW_WAVE, CHASE, FADE, STROBE,
    FIRE, TWINKLE, SPARKLE, WAVE, POLICE, LIGHTNING, NORTHERN, FIREWORKS,
    BEAT, FLICKER, FIRESTORM
  };
  static const unsigned long FRAME_MS = 20;  // ~50 frames a second
  static const uint16_t MAX_PIXELS = 300;    // the RGB widget's highest count

  Adafruit_NeoPixel& _s;
  uint8_t  _r = 255, _g = 255, _b = 255;
  bool     _on = false, _dirty = true;
  uint16_t _count = 0;
  Fx       _fx = SOLID;
  unsigned long _start = 0, _lastFrame = 0;
  uint8_t  _lv[MAX_PIXELS];  // per-pixel level for Twinkle, Fire, Fireworks
  uint16_t _hue[MAX_PIXELS]; // per-pixel hue for Fireworks

  static Fx parse(const char* name) {
    if (!name) return SOLID;
    String n(name);
    n.trim();
    n.toLowerCase();
    if (n.endsWith(" effect")) n.remove(n.length() - 7);
    if (n == "blink" || n == "blinking")                 return BLINK;
    if (n == "pulse")                                    return PULSE;
    if (n == "rainbow" || n == "rainbow cycle")          return RAINBOW_CYCLE;
    if (n == "rainbow wave")                             return RAINBOW_WAVE;
    if (n == "chase")                                    return CHASE;
    if (n == "fade")                                     return FADE;
    if (n == "strobe")                                   return STROBE;
    if (n == "fire")                                     return FIRE;
    if (n == "twinkle")                                  return TWINKLE;
    if (n == "sparkle")                                  return SPARKLE;
    if (n == "wave")                                     return WAVE;
    if (n == "police" || n == "police lights")           return POLICE;
    if (n == "lightning")                                return LIGHTNING;
    if (n == "northern lights")                          return NORTHERN;
    if (n == "fireworks")                                return FIREWORKS;
    if (n == "pulse to beat")                            return BEAT;
    if (n == "flicker")                                  return FLICKER;
    if (n == "firestorm")                                return FIRESTORM;
    return SOLID;  // "Default", "Solid" and unknown names
  }

  uint16_t pixels() const {
    uint16_t n = _s.numPixels();
    if (n > MAX_PIXELS) n = MAX_PIXELS;
    return (_count > 0 && _count < n) ? _count : n;
  }

  // Sets one pixel, with gamma correction. level 0–255 scales the colour.
  void px(uint16_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t level = 255) {
    r = (uint16_t)r * level / 255;
    g = (uint16_t)g * level / 255;
    b = (uint16_t)b * level / 255;
    _s.setPixelColor(i, _s.gamma32(_s.Color(r, g, b)));
  }
  void pxHue(uint16_t i, uint16_t hue, uint8_t level = 255) {
    _s.setPixelColor(i, _s.gamma32(_s.ColorHSV(hue, 255, level)));
  }
  void fill(uint8_t level) {
    for (uint16_t i = 0; i < pixels(); i++) px(i, _r, _g, _b, level);
  }
  // 0–255 sine of a position in a period
  static uint8_t wave8(unsigned long t, unsigned long period) {
    float a = (float)(t % period) / period * 2.0f * PI;
    return (uint8_t)((sinf(a) + 1.0f) * 127.5f);
  }

  void draw(unsigned long t) {
    _s.clear();
    if (!_on) return;
    uint16_t n = pixels();

    switch (_fx) {
      case SOLID:
        fill(255);
        break;

      case BLINK:
        if ((t / 500) % 2 == 0) fill(255);
        break;

      case PULSE:
        fill(20 + wave8(t, 1500) * 235 / 255);
        break;

      case FADE:
        fill(wave8(t, 4000));
        break;

      case BEAT: {  // two quick beats, then a pause
        unsigned long p = t % 1200;
        uint8_t level = 15;
        if (p < 120)                 level = 255 - p * 2;
        else if (p >= 250 && p < 370) level = 255 - (p - 250) * 2;
        fill(level);
        break;
      }

      case STROBE:
        if (t % 120 < 30) fill(255);
        break;

      case RAINBOW_CYCLE:  // the whole strip changes colour together
        for (uint16_t i = 0; i < n; i++) pxHue(i, (uint16_t)(t * 13));
        break;

      case RAINBOW_WAVE:   // a rainbow that runs along the strip
        for (uint16_t i = 0; i < n; i++) pxHue(i, (uint16_t)(t * 20 + i * 65536UL / n));
        break;

      case CHASE: {
        uint16_t head = (t / 80) % n;
        for (uint8_t k = 0; k < 4 && k < n; k++)
          px((head + n - k) % n, _r, _g, _b, 255 >> (k * 2));
        break;
      }

      case WAVE:
        for (uint16_t i = 0; i < n; i++)
          px(i, _r, _g, _b, 20 + wave8(t + i * 2000UL / n, 2000) * 235 / 255);
        break;

      case POLICE: {  // halves flash red and blue in turn
        unsigned long p = t % 800;
        bool flash = (p % 200) < 70;
        bool redSide = p < 400;
        for (uint16_t i = 0; i < n; i++) {
          bool left = i < (n + 1) / 2;
          if (flash && left == redSide) px(i, left ? 255 : 0, 0, left ? 0 : 255);
        }
        break;
      }

      case TWINKLE:  // pixels of your colour fade in and out at random
        for (uint16_t i = 0; i < n; i++) {
          if (_lv[i] > 8) _lv[i] -= 8; else _lv[i] = 0;
          if (_lv[i] == 0 && random(100) < 3) _lv[i] = 255;
          px(i, _r, _g, _b, _lv[i]);
        }
        break;

      case SPARKLE:  // a dim base of your colour with white sparks
        for (uint16_t i = 0; i < n; i++) {
          if (random(100) < 4) px(i, 255, 255, 255);
          else                 px(i, _r, _g, _b, 60);
        }
        break;

      case FLICKER:  // candle-like
        for (uint16_t i = 0; i < n; i++) px(i, _r, _g, _b, random(120, 256));
        break;

      case FIRE:
      case FIRESTORM: {  // flames in red, orange and yellow
        uint8_t cool = _fx == FIRESTORM ? 40 : 18;
        uint8_t spark = _fx == FIRESTORM ? 40 : 15;
        for (uint16_t i = 0; i < n; i++) {
          _lv[i] = _lv[i] > cool ? _lv[i] - random(cool) : random(40);
          if (random(100) < spark) _lv[i] = random(180, 256);
          uint8_t h = _lv[i];
          px(i, 255, h > 128 ? (h - 128) * 2 : h / 3, h > 220 ? (h - 220) * 4 : 0, h);
        }
        break;
      }

      case LIGHTNING: {  // dark, then a burst of white flashes
        unsigned long p = t % 3000;
        bool flash = (p < 60) || (p > 140 && p < 180) || (p > 260 && p < 330);
        if (flash) fillWhite(n);
        break;
      }

      case NORTHERN:  // slow green, teal and purple waves
        for (uint16_t i = 0; i < n; i++) {
          uint16_t hue = 21845 + (uint16_t)(wave8(t / 3 + i * 600UL / n, 1000) * 120); // green → purple
          pxHue(i, hue, 60 + wave8(t + i * 3000UL / n, 3000) * 195 / 255);
        }
        break;

      case FIREWORKS:  // bursts of random colours that fade
        for (uint16_t i = 0; i < n; i++) {
          if (_lv[i] > 6) _lv[i] -= 6; else _lv[i] = 0;
          if (_lv[i] == 0 && random(1000) < 8) {
            _lv[i] = 255;
            _hue[i] = random(65536);
          }
          if (_lv[i]) pxHue(i, _hue[i], _lv[i]);
        }
        break;
    }
  }

  void fillWhite(uint16_t n) {
    for (uint16_t i = 0; i < n; i++) px(i, 255, 255, 255);
  }
};
