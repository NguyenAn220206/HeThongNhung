#pragma once

#include <stdint.h>

const uint8_t FAN_SPEED_OFF = 0;
const uint8_t FAN_SPEED_SLOW = 90;
const uint8_t FAN_SPEED_NORMAL = 170;
const uint8_t FAN_SPEED_FAST = 255;

// Each button must stay pressed/released for 40 ms. Short electrical spikes
// cannot toggle a button, and a held button produces only one press.
class DebouncedButton {
 public:
  bool pressed(bool down, uint32_t now) {
    if (down != rawDown_) {
      rawDown_ = down;
      changedAt_ = now;
    }
    if (down != stableDown_ && uint32_t(now - changedAt_) >= 40) {
      stableDown_ = down;
      return stableDown_;
    }
    return false;
  }

 private:
  bool rawDown_ = false;
  bool stableDown_ = false;
  uint32_t changedAt_ = 0;
};

struct ControlInputs {
  bool sensorReady = false;
  bool tooCold = false;
  bool temperatureDanger = false;
  bool gasDanger = false;
  bool humidityDanger = false;
  bool webRelayForced = false;
};

struct ControlOutputs {
  uint8_t fanSpeed = FAN_SPEED_OFF;
  bool ventilationOn = false;
  bool buzzerEnabled = false;
  bool buzzerOn = false;
  bool localRelayForced = false;
  bool localFanMaxForced = false;
  bool localBuzzerEnabled = false;
  bool remoteBuzzerActive = false;
  bool webRelayForced = false;
  bool temperatureDanger = false;
  bool gasDanger = false;
  bool humidityDanger = false;
  uint32_t relayPresses = 0;
  uint32_t fanPresses = 0;
  uint32_t buzzerPresses = 0;
};

// No GPIO, HTTP, delays or shared memory here: one task owns this state.
class LocalControls {
 public:
  ControlOutputs update(uint32_t now, bool relayDown, bool fanDown,
                        bool buzzerDown, const ControlInputs& input,
                        bool startRemoteBuzzer = false) {
    if (relayButton_.pressed(relayDown, now)) {
      state_.localRelayForced = !state_.localRelayForced;
      ++state_.relayPresses;
    }
    if (fanButton_.pressed(fanDown, now)) {
      state_.localFanMaxForced = !state_.localFanMaxForced;
      ++state_.fanPresses;
    }
    if (buzzerButton_.pressed(buzzerDown, now)) {
      state_.localBuzzerEnabled = !state_.localBuzzerEnabled;
      ++state_.buzzerPresses;
    }

    state_.webRelayForced = input.webRelayForced;
    state_.temperatureDanger = input.temperatureDanger;
    state_.gasDanger = input.gasDanger;
    state_.humidityDanger = input.humidityDanger;
    const bool danger = input.temperatureDanger || input.gasDanger || input.humidityDanger;
    state_.fanSpeed = state_.localFanMaxForced || danger
                          ? FAN_SPEED_FAST
                          : (input.sensorReady && !input.tooCold ? FAN_SPEED_NORMAL : FAN_SPEED_OFF);
    state_.ventilationOn = state_.localRelayForced || input.webRelayForced
                           || input.gasDanger || input.humidityDanger;

    // Gas, humidity and manual buzzer keep their existing repeating pattern.
    const bool alarm = state_.localBuzzerEnabled || input.gasDanger || input.humidityDanger;
    if (alarm && !alarmWasEnabled_) alarmStartedAt_ = now;
    alarmWasEnabled_ = alarm;
    const bool alarmOn = alarm && uint32_t(now - alarmStartedAt_) % 1000 < 250;

    // Temperature alone: three 250 ms beeps over 3 s, repeated every 5 min
    // from the start of the previous burst while the temperature stays high.
    // A return to normal cancels the burst and rearms the next threshold crossing.
    if (input.temperatureDanger && (!temperatureWasDangerous_
        || uint32_t(now - temperatureBurstStartedAt_) >= 300000)) {
      temperatureBurstStartedAt_ = now;
    }
    temperatureWasDangerous_ = input.temperatureDanger;
    const uint32_t temperatureElapsed = uint32_t(now - temperatureBurstStartedAt_);
    const bool temperatureBurst = input.temperatureDanger && temperatureElapsed < 3000;
    const bool temperatureOn = temperatureBurst && temperatureElapsed % 1000 < 250;

    if (startRemoteBuzzer && !state_.remoteBuzzerActive) {
      state_.remoteBuzzerActive = true;
      remoteStartedAt_ = now;
    }
    const uint32_t remoteElapsed = uint32_t(now - remoteStartedAt_);
    if (state_.remoteBuzzerActive && remoteElapsed >= 9000) {
      state_.remoteBuzzerActive = false;
    }
    // Five cycles: four 150 ms beeps with 100 ms gaps, then 800 ms rest.
    const uint32_t remotePhase = remoteElapsed % 1800;
    const bool remoteOn = state_.remoteBuzzerActive && remotePhase < 1000
                          && remotePhase % 250 < 150;
    state_.buzzerEnabled = alarm || temperatureBurst || state_.remoteBuzzerActive;
    state_.buzzerOn = alarmOn || temperatureOn || remoteOn;
    return state_;
  }

 private:
  DebouncedButton relayButton_;
  DebouncedButton fanButton_;
  DebouncedButton buzzerButton_;
  ControlOutputs state_;
  bool alarmWasEnabled_ = false;
  uint32_t alarmStartedAt_ = 0;
  bool temperatureWasDangerous_ = false;
  uint32_t temperatureBurstStartedAt_ = 0;
  uint32_t remoteStartedAt_ = 0;
};
