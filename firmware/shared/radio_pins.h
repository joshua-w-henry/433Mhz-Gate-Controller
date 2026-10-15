#pragma once
// Shared bench radio pinout; both ESP32 modules wired identically.
constexpr int RF_CS_PIN = 27;
constexpr int RF_GDO0_PIN = 26; // CC1101 packet IRQ
constexpr int RF_GDO2_PIN = 25; // CC1101 second digital output
constexpr int RF_SCK_PIN = 18;
constexpr int RF_MOSI_PIN = 23;
constexpr int RF_MISO_PIN = 19;
constexpr float RF_FREQUENCY_MHZ = 433.92f;
