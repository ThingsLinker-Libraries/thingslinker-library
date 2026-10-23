/**
 * @file TL_Echo.h
 * @brief Tells the device's own messages apart from commands sent to it
 *
 * A device listens on the same topics it publishes on (a Terminal or Button
 * pin is both written by the device and sent to it by the app), and MQTT
 * delivers a client's own messages back to it. Without a check the device
 * would take its own output for a command from the app.
 *
 * Every data message the library publishes is noted here; an incoming message
 * on the same topic with the same payload within TL_ECHO_WINDOW_MS is the
 * device's own and is ignored. Payloads carry a timestamp, so a real command
 * from the app never matches one.
 *
 * Shared by the WiFi and GSM transports.
 */
#ifndef TL_ECHO_H
#define TL_ECHO_H

#define TL_ECHO_WINDOW_MS 5000UL

/** Note a data message this device has just published. */
void tlEchoNote(const char* topic, const char* payload);

/** True (once) if this incoming message is one the device published itself. */
bool tlEchoIsOwn(const char* topic, const char* payload);

#endif
