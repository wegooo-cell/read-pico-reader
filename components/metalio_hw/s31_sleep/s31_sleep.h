// SPDX-License-Identifier: MIT
// Metalio reference: CloudZao/Metalio-E-INK4-Plus@ad8edf4150a688737c0d7c00cae4413079b262ef; see licenses/Metalio-MIT.txt.
#pragma once

#include <cstdint>
#include <esp_err.h>

// ESP32-S31 SDK sleep compatibility and retained diagnostics.
// These entry points run with caches enabled; cache-off SDK wrappers stay private.
namespace s31_sleep {
enum class Phase : uint32_t {
    Admission = 1,
    HardwareParked = 2,
    WakeArmed = 3,
    BeforeSleep = 4,
    AfterSleep = 5,
    KeysRestored = 6,
    BeforeSdRestore = 7,
    BeforeBusRestore = 8,
    Restored = 9,
};

esp_err_t Init();
void CaptureReset();
void ReportReset();
void BeginCycle();
void Checkpoint(Phase phase, unsigned applied);
esp_err_t LightSleep(unsigned applied, bool *power_key_confirmed);
}  // namespace s31_sleep
