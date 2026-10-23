#include "TL_Echo.h"
#include <Arduino.h>

namespace {

const int kSlots = 8;

struct Sent {
  uint32_t      hash;
  unsigned long at;
};

Sent _sent[kSlots] = {};
int  _next = 0;

// FNV-1a over "topic\npayload": a fingerprint is enough, nothing is stored
uint32_t fingerprint(const char* topic, const char* payload) {
  uint32_t h = 2166136261u;
  for (const char* p = topic; p && *p; p++)   { h ^= (uint8_t)*p; h *= 16777619u; }
  h ^= '\n';                                    h *= 16777619u;
  for (const char* p = payload; p && *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  return h ? h : 1;   // 0 marks an empty slot
}

}  // namespace

void tlEchoNote(const char* topic, const char* payload) {
  _sent[_next].hash = fingerprint(topic, payload);
  _sent[_next].at   = millis();
  _next = (_next + 1) % kSlots;
}

bool tlEchoIsOwn(const char* topic, const char* payload) {
  uint32_t h = fingerprint(topic, payload);
  unsigned long now = millis();
  for (int i = 0; i < kSlots; i++) {
    if (_sent[i].hash == h && now - _sent[i].at <= TL_ECHO_WINDOW_MS) {
      _sent[i].hash = 0;   // each message comes back once
      return true;
    }
  }
  return false;
}
