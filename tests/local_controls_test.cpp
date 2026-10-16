#include "../esp32_code/LocalControls.h"
#include <cassert>
#include <iostream>

struct Rig {
  LocalControls controls;
  ControlInputs input;
  ControlOutputs output;
  uint32_t now = 0;
  bool relay = false, fan = false, buzzer = false;

  void tick(uint32_t ms = 5, bool startRemote = false) {
    now += ms;
    output = controls.update(now, relay, fan, buzzer, input, startRemote);
  }
  void wait(uint32_t ms) {
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 5) tick();
  }
  void press(bool& button) {
    button = true;
    tick();
    wait(45);
    button = false;
    tick();
    wait(45);
  }
};

void testTemperatureReminder() {
  LocalControls controls;
  ControlInputs hot;
  hot.sensorReady = true;
  hot.temperatureDanger = true;
  const uint32_t started = 12345;
  unsigned bursts = 0, beeps = 0;
  bool wasEnabled = false, wasOn = false;
  // Three complete bursts: immediately, at 5 min and at 10 min.
  for (uint32_t elapsed = 0; elapsed <= 603005; elapsed += 5) {
    const auto out = controls.update(started + elapsed, false, false, false, hot);
    const uint32_t phase = elapsed % 300000;
    const bool expectedBurst = phase < 3000;
    const bool expectedOn = expectedBurst && phase % 1000 < 250;
    assert(out.buzzerEnabled == expectedBurst);
    assert(out.buzzerOn == expectedOn);
    assert(out.fanSpeed == 255 && !out.ventilationOn);
    assert(out.temperatureDanger);
    if (out.buzzerEnabled && !wasEnabled) ++bursts;
    if (out.buzzerOn && !wasOn) ++beeps;
    wasEnabled = out.buzzerEnabled;
    wasOn = out.buzzerOn;
  }
  assert(bursts == 3 && beeps == 9);

  // Recovery interrupts a beep immediately; a fresh crossing sounds again.
  Rig recovery;
  recovery.input.sensorReady = true;
  recovery.input.temperatureDanger = true;
  recovery.tick();
  assert(recovery.output.buzzerOn);
  recovery.input.temperatureDanger = false;
  recovery.tick();
  assert(!recovery.output.buzzerOn && !recovery.output.buzzerEnabled);
  assert(recovery.output.fanSpeed == 170);
  recovery.input.temperatureDanger = true;
  recovery.tick();
  assert(recovery.output.buzzerOn);
  recovery.wait(3000);
  assert(!recovery.output.buzzerEnabled);
  recovery.input.temperatureDanger = false;
  recovery.tick();
  recovery.wait(300000);
  assert(!recovery.output.buzzerEnabled);
  recovery.input.temperatureDanger = true;
  recovery.tick();
  assert(recovery.output.buzzerOn);

  // Run through uint32_t timer wrap with the same burst and reminder timings.
  LocalControls wrap;
  const uint32_t nearWrap = UINT32_MAX - 1000;
  for (uint32_t elapsed = 0; elapsed <= 303005; elapsed += 5) {
    const auto out = wrap.update(uint32_t(nearWrap + elapsed), false, false, false, hot);
    const uint32_t phase = elapsed % 300000;
    assert(out.buzzerEnabled == (phase < 3000));
    assert(out.buzzerOn == (phase < 3000 && phase % 1000 < 250));
  }
}

void testTemperatureWithOtherBuzzerSources() {
  // Compare the old alarm sources with/without heat through its silent period
  // and its next reminder. Heat must never suppress their existing waveforms.
  for (unsigned source = 0; source < 4; ++source) {
    LocalControls baseline, withHeat;
    ControlInputs normal, hot;
    for (uint32_t elapsed = 0; elapsed <= 303005; elapsed += 5) {
      normal.gasDanger = source == 0 && elapsed >= 1000 && elapsed < 301000;
      normal.humidityDanger = source == 1 && elapsed >= 1000 && elapsed < 301000;
      hot = normal;
      hot.temperatureDanger = true;
      // Toggle manual buzzer on/off while heat is otherwise silent.
      const bool manualDown = source == 2 &&
          ((elapsed >= 5000 && elapsed < 5100) || (elapsed >= 15000 && elapsed < 15100));
      const bool remoteStart = source == 3 && (elapsed == 5000 || elapsed == 299000);
      const auto base = baseline.update(elapsed, false, false, manualDown, normal, remoteStart);
      const auto combined = withHeat.update(elapsed, false, false, manualDown, hot, remoteStart);
      const uint32_t phase = elapsed % 300000;
      const bool heatBurst = phase < 3000;
      const bool heatOn = heatBurst && phase % 1000 < 250;
      assert(combined.buzzerEnabled == (base.buzzerEnabled || heatBurst));
      assert(combined.buzzerOn == (base.buzzerOn || heatOn));
      assert(combined.ventilationOn == base.ventilationOn);
      assert(combined.localBuzzerEnabled == base.localBuzzerEnabled);
      assert(combined.remoteBuzzerActive == base.remoteBuzzerActive);
      assert(combined.buzzerPresses == base.buzzerPresses);
      assert(combined.fanSpeed == 255);
    }
  }
}

int main() {
  testTemperatureReminder();
  testTemperatureWithOtherBuzzerSources();
  // Fan-only actions must never enable relay/buzzer in a normal environment.
  Rig fan;
  fan.input.sensorReady = true;
  fan.tick();
  assert(fan.output.fanSpeed == 170);
  fan.press(fan.fan);
  assert(fan.output.fanSpeed == 255 && fan.output.fanPresses == 1);
  assert(!fan.output.ventilationOn && !fan.output.buzzerEnabled);
  assert(fan.output.relayPresses == 0 && fan.output.buzzerPresses == 0);
  fan.press(fan.fan);
  assert(fan.output.fanSpeed == 170 && fan.output.fanPresses == 2);
  fan.input.tooCold = true;
  fan.tick();
  assert(fan.output.fanSpeed == 0);
  fan.press(fan.fan);
  assert(fan.output.fanSpeed == 255 && !fan.output.buzzerEnabled);

  Rig relay;
  relay.press(relay.relay);
  assert(relay.output.ventilationOn && relay.output.fanSpeed == 0);
  assert(!relay.output.buzzerEnabled && !relay.output.localFanMaxForced);
  relay.press(relay.relay);
  assert(!relay.output.ventilationOn && relay.output.relayPresses == 2);
  relay.input.webRelayForced = true;
  relay.tick();
  relay.press(relay.relay);
  relay.press(relay.relay);
  assert(relay.output.ventilationOn && !relay.output.localRelayForced);

  Rig buzzer;
  buzzer.press(buzzer.buzzer);
  assert(buzzer.output.buzzerEnabled && buzzer.output.buzzerOn);
  assert(buzzer.output.fanSpeed == 0 && !buzzer.output.ventilationOn);
  buzzer.wait(250);
  assert(buzzer.output.buzzerEnabled && !buzzer.output.buzzerOn);
  buzzer.press(buzzer.buzzer);
  assert(!buzzer.output.buzzerEnabled && !buzzer.output.buzzerOn);

  // Electrical spikes, bouncing contacts, long holds, stable release, repeat.
  Rig noise;
  for (int i = 0; i < 20; ++i) {
    noise.fan = !noise.fan;
    noise.tick(5);
  }
  assert(noise.output.fanPresses == 0);
  noise.fan = true;
  noise.tick();
  noise.wait(45);
  noise.wait(2000);
  assert(noise.output.fanPresses == 1);
  noise.fan = false;
  noise.tick(5);
  noise.fan = true;
  noise.tick(5);
  noise.wait(100);
  assert(noise.output.fanPresses == 1);
  noise.fan = false;
  noise.tick();
  noise.wait(45);
  noise.press(noise.fan);
  assert(noise.output.fanPresses == 2 && !noise.output.localFanMaxForced);

  // All combinations of local requests, web relay and sensor alarms.
  for (unsigned mask = 0; mask < 128; ++mask) {
    Rig r;
    r.input.sensorReady = true;
    if (mask & 1) r.press(r.relay);
    if (mask & 2) r.press(r.fan);
    if (mask & 4) r.press(r.buzzer);
    r.input.webRelayForced = mask & 8;
    r.input.temperatureDanger = mask & 16;
    r.input.gasDanger = mask & 32;
    r.input.humidityDanger = mask & 64;
    r.tick();
    const bool danger = mask & (16 | 32 | 64);
    assert(r.output.fanSpeed == ((mask & 2) || danger ? 255 : 170));
    assert(r.output.ventilationOn == bool(mask & (1 | 8 | 32 | 64)));
    assert(r.output.buzzerEnabled == bool((mask & 4) || danger));
  }

  // Automatic alarms remain active when the manual buzzer is toggled off.
  Rig alarm;
  alarm.input.gasDanger = true;
  alarm.tick();
  alarm.press(alarm.buzzer);
  alarm.press(alarm.buzzer);
  assert(!alarm.output.localBuzzerEnabled && alarm.output.buzzerEnabled);
  assert(alarm.output.fanSpeed == 255 && alarm.output.ventilationOn);
  alarm.input.gasDanger = false;
  alarm.tick();
  assert(!alarm.output.buzzerEnabled && !alarm.output.ventilationOn);

  // Remote pattern is nonblocking: press all three buttons while it runs.
  Rig remote;
  remote.tick(5, true);
  const uint32_t started = remote.now;
  remote.relay = remote.fan = remote.buzzer = true;
  remote.tick();
  remote.wait(45);
  assert(remote.output.localRelayForced && remote.output.localFanMaxForced);
  assert(remote.output.localBuzzerEnabled && remote.output.remoteBuzzerActive);
  assert(remote.output.relayPresses == 1 && remote.output.fanPresses == 1);
  remote.relay = remote.fan = remote.buzzer = false;
  remote.tick();
  remote.wait(45);
  remote.press(remote.relay);
  remote.press(remote.fan);
  remote.press(remote.buzzer);
  assert(!remote.output.ventilationOn && remote.output.fanSpeed == 0);
  assert(!remote.output.localBuzzerEnabled && remote.output.remoteBuzzerActive);
  while (uint32_t(remote.now - started) < 9000) remote.tick();
  assert(!remote.output.remoteBuzzerActive && !remote.output.buzzerOn);

  // Verify every 5 ms of the 9-second remote waveform.
  LocalControls pattern;
  ControlInputs clear;
  for (uint32_t elapsed = 0; elapsed <= 9000; elapsed += 5) {
    const auto out = pattern.update(elapsed, false, false, false, clear, elapsed == 0);
    const bool expected = elapsed < 9000 && elapsed % 1800 < 1000
                          && elapsed % 1800 % 250 < 150;
    assert(out.buzzerOn == expected);
    assert(out.remoteBuzzerActive == (elapsed < 9000));
  }

  // millis() wrap must not break debounce or the remote timer.
  Rig wrap;
  wrap.now = UINT32_MAX - 20;
  wrap.tick(5, true);
  wrap.press(wrap.fan);
  assert(wrap.output.fanSpeed == 255 && wrap.output.fanPresses == 1);
  wrap.wait(9000);
  assert(!wrap.output.remoteBuzzerActive);

  std::cout << "PASS: button isolation, debounce, holds, repeat presses, 128 priority combinations, "
               "remote timing/concurrent buttons, timer wrap, three-beep temperature bursts, "
               "five-minute reminders/recovery, independent gas/humidity/manual/web buzzer\n";
}
