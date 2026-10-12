/**
 * @file TL_Offline.h
 * @brief Keeps readings while there is no connection and sends them later
 *
 * When offline storage is switched on, a reading that cannot be sent (WiFi or
 * mobile data is down, or the MQTT server cannot be reached) is kept in memory
 * instead of being dropped. Once the connection is back the stored readings
 * are sent in the order they were taken, each with the time it was taken, and
 * only then do new readings go out again.
 *
 *   iot.offlineStorage(true);    // keep readings while offline
 *   iot.offlineStorage(false);   // default: readings taken offline are dropped
 *
 * Readings are held in RAM. They survive any length of connection loss, but
 * not a restart or a power cut. When the store is full the oldest reading is
 * dropped to make room for the newest.
 *
 * This file is shared by the WiFi and GSM transports; sketches use the
 * offlineStorage() and offlineCount() methods of the main class instead.
 */

#ifndef TL_OFFLINE_H
#define TL_OFFLINE_H

#include <Arduino.h>

/** Switch the store on or off and set how many readings it may hold.
 *  Switching it off discards whatever is waiting. */
void tlOfflineEnable(bool enable, uint16_t maxRecords);

bool tlOfflineEnabled();

/** Number of readings waiting to be sent. */
uint16_t tlOfflineCount();

/** Discard every waiting reading. */
void tlOfflineClear();

/**
 * Keep one reading.
 * @param payload    the JSON that would have been sent, including its "t"
 * @param timeValid  true when "t" is a real clock time; false when the clock
 *                   was not set yet, in which case "t" is worked out again
 *                   from the reading's age when it is finally sent
 */
void tlOfflineStore(const char* widgetType, const char* pin, const char* payload, bool timeValid);

/** True when the oldest reading was taken before the clock was set. */
bool tlOfflineFrontNeedsClock();

/**
 * Copy out the oldest reading without removing it.
 * @param nowEpoch  current Unix time, or 0 when the clock is still not set
 * @return false when nothing is waiting or a buffer is too small
 */
bool tlOfflinePeek(char* widgetType, size_t widgetTypeSize,
                   char* pin, size_t pinSize,
                   char* payload, size_t payloadSize,
                   unsigned long nowEpoch);

/** Remove the oldest reading (after it was sent). */
void tlOfflinePop();

#endif // TL_OFFLINE_H
