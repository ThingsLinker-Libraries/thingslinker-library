/**
 * @file TL_Offline.cpp
 * @brief Keeps readings while there is no connection and sends them later
 */

#include "TL_Offline.h"
#include "TL_Config.h"
#include <ArduinoJson.h>
#include <deque>

namespace {

struct Record {
  char     widgetType[20];
  char     pin[8];
  uint32_t takenAt;     // millis() when the reading was taken
  bool     timeValid;   // "t" in the payload is a real clock time
  String   payload;
};

std::deque<Record> _records;
bool     _enabled    = false;
uint16_t _maxRecords = TL_OFFLINE_DEFAULT_RECORDS;
uint32_t _dropped    = 0;   // readings lost because the store was full

void dropOldest() {
  if (_records.empty()) return;
  _records.pop_front();
  // Say so once when it starts and then every 50, not on every reading
  if (_dropped % 50 == 0) TL_LOG("[Offline] Store full — oldest readings are being dropped");
  _dropped++;
}

bool memoryIsLow() {
#ifdef ESP32
  return ESP.getFreeHeap() < TL_OFFLINE_MIN_FREE_HEAP;
#else
  return false;
#endif
}

}  // namespace

void tlOfflineEnable(bool enable, uint16_t maxRecords) {
  _enabled    = enable;
  _maxRecords = maxRecords > 0 ? maxRecords : 1;
  if (!enable) tlOfflineClear();
  while (_records.size() > _maxRecords) dropOldest();
}

bool tlOfflineEnabled() { return _enabled; }

uint16_t tlOfflineCount() { return (uint16_t)_records.size(); }

void tlOfflineClear() {
  _records.clear();
  _records.shrink_to_fit();
  _dropped = 0;
}

void tlOfflineStore(const char* widgetType, const char* pin, const char* payload, bool timeValid) {
  if (!_enabled || !widgetType || !pin || !payload) return;

  // Make room: the newest readings are the ones worth keeping
  while (!_records.empty() && (_records.size() >= _maxRecords || memoryIsLow())) dropOldest();
  if (memoryIsLow()) return;   // nothing left to free; the sketch needs the memory more

  Record record;
  strncpy(record.widgetType, widgetType, sizeof(record.widgetType) - 1);
  record.widgetType[sizeof(record.widgetType) - 1] = '\0';
  strncpy(record.pin, pin, sizeof(record.pin) - 1);
  record.pin[sizeof(record.pin) - 1] = '\0';
  record.takenAt   = millis();
  record.timeValid = timeValid;
  record.payload   = payload;
  _records.push_back(record);
}

bool tlOfflineFrontNeedsClock() {
  return !_records.empty() && !_records.front().timeValid;
}

bool tlOfflinePeek(char* widgetType, size_t widgetTypeSize,
                   char* pin, size_t pinSize,
                   char* payload, size_t payloadSize,
                   unsigned long nowEpoch) {
  if (_records.empty() || !widgetType || !pin || !payload) return false;
  const Record& record = _records.front();

  if (strlen(record.widgetType) >= widgetTypeSize || strlen(record.pin) >= pinSize) return false;
  strcpy(widgetType, record.widgetType);
  strcpy(pin, record.pin);

  // A reading taken before the clock was set carries no usable time. Now that
  // the clock is known, its time is the current time minus the reading's age.
  if (!record.timeValid && nowEpoch > 0) {
    JsonDocument doc;
    if (deserializeJson(doc, record.payload.c_str()) == DeserializationError::Ok) {
      unsigned long ageSeconds = (uint32_t)(millis() - record.takenAt) / 1000UL;
      doc["t"] = nowEpoch > ageSeconds ? nowEpoch - ageSeconds : nowEpoch;
      if (measureJson(doc) + 1 <= payloadSize) {
        serializeJson(doc, payload, payloadSize);
        return true;
      }
    }
  }

  if (record.payload.length() >= payloadSize) return false;
  strcpy(payload, record.payload.c_str());
  return true;
}

void tlOfflinePop() {
  if (_records.empty()) return;
  _records.pop_front();
  if (_records.empty()) {
    _records.shrink_to_fit();
    _dropped = 0;
  }
}
