#pragma once
// Editable identity. This is a display label, NOT an authorization credential.
constexpr char REMOTE_NAME[] = "Josh - Truck";
constexpr unsigned long SCREEN_IDLE_MS = 60000;
// USB-powered bench version: no deep sleep or battery measurement yet.
constexpr int ACTIVATE_PIN = 32;
constexpr int STATUS_PIN = 33;
