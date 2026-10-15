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

int main() {
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
               "remote timing/concurrent buttons, timer wrap\n";
}
