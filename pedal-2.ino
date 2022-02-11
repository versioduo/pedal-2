// © Kay Sievers <kay@versioduo.com>, 2020-2022
// SPDX-License-Identifier: Apache-2.0

#include <V2Device.h>
#include <V2LED.h>
#include <V2MIDI.h>
#include <V2Potentiometer.h>

V2DEVICE_METADATA("com.versioduo.pedal-2", 12, "versioduo:samd:itsybitsy");

enum {
  PIN_PEDAL         = A0,
  PIN_POTENTIOMETER = A1,
  PIN_SWITCH        = A2,
};

static V2Base::Analog::ADC ADC(V2Base::Analog::ADC::getID(PIN_PEDAL));

static class Device : public V2Device {
public:
  Device() : V2Device() {
    metadata.vendor      = "Versio Duo";
    metadata.product     = "pedal-2";
    metadata.description = "Expression Pedal + Potentiometer";
    metadata.home        = "https://versioduo.com/#pedal-2";

    system.download  = "https://versioduo.com/download";
    system.configure = "https://versioduo.com/configure";

    // https://github.com/versioduo/arduino-board-package/blob/main/boards.txt
    usb.pid            = 0xe931;
    usb.ports.standard = 0;

    configuration = {.size{sizeof(config)}, .data{&config}};
  }

  // Config, written to EEPROM
  struct {
    uint8_t channel{};
    struct {
      uint8_t controller{V2MIDI::CC::FootController};
      uint8_t from{};
      uint8_t to{127};
    } pedal;

    struct {
      uint8_t controller{V2MIDI::CC::Controller9};
      uint8_t from{};
      uint8_t to{127};
    } poti;
  } config;

private:
  const struct V2Potentiometer::Config _config { .n_steps{128}, .min{0.05}, .max{0.95}, .alpha{0.2}, .lag{0.007}, };
  V2Potentiometer _pedal{&_config};
  bool _reverse{};
  V2Potentiometer _poti{&_config};
  uint8_t _step_pedal{};
  uint8_t _step_poti{};
  unsigned long _measure_usec{};
  unsigned long _events_usec{};
  V2MIDI::Packet _midi{};

  void handleReset() override {
    _pedal.reset();
    _poti.reset();
    _step_pedal   = 0;
    _reverse      = false;
    _step_poti    = 0;
    _measure_usec = 0;
    _events_usec  = micros();
    _midi         = {};
  }

  void allNotesOff() {
    sendEvents(true);
  }

  void handleLoop() override {
    if ((unsigned long)(micros() - _measure_usec) > 5 * 1000) {
      _pedal.measure(ADC.readChannel(V2Base::Analog::ADC::getChannel(PIN_PEDAL)));
      _poti.measure(1.f - ADC.readChannel(V2Base::Analog::ADC::getChannel(PIN_POTENTIOMETER)));
      _measure_usec = micros();
    }

    if ((unsigned long)(micros() - _events_usec) > 20 * 1000) {
      const bool on = !digitalRead(PIN_SWITCH);
      if (on != _reverse)
        _reverse = on;

      sendEvents();
      _events_usec = micros();
    }
  }

  bool handleSend(V2MIDI::Packet *midi) override {
    return usb.midi.send(midi);
  }

  void sendEvents(bool force = false) {
    {
      const float range    = (int8_t)config.pedal.to - (int8_t)config.pedal.from;
      const float fraction = _reverse ? (1.f - _pedal.getFraction()) : _pedal.getFraction();
      const uint8_t value  = config.pedal.from + (range * fraction);

      if (force || _step_pedal != value) {
        _step_pedal = value;
        send(_midi.setControlChange(config.channel, config.pedal.controller, value));
      }
    }

    {
      const float range   = (int8_t)config.poti.to - (int8_t)config.poti.from;
      const uint8_t value = config.poti.from + (range * _poti.getFraction());

      if (force || _step_poti != value) {
        _step_poti = value;
        send(_midi.setControlChange(config.channel, config.poti.controller, value));
      }
    }
  }

  void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value) override {
    switch (controller) {
      case V2MIDI::CC::AllSoundOff:
      case V2MIDI::CC::AllNotesOff:
        allNotesOff();
        break;
    }
  }

  void handleSystemReset() override {
    reset();
  }

  void exportSettings(JsonArray json) override {
    {
      JsonObject setting = json.createNestedObject();
      setting["type"]    = "number";
      setting["title"]   = "MIDI";
      setting["label"]   = "Channel";
      setting["min"]     = 1;
      setting["max"]     = 16;
      setting["input"]   = "select";
      setting["path"]    = "midi/channel";
    }

    {
      JsonObject setting = json.createNestedObject();
      setting["type"]    = "controller";
      setting["title"]   = "Pedal";
      setting["path"]    = "pedal/controller";
    }
    {
      JsonObject setting = json.createNestedObject();
      setting["type"]    = "number";
      setting["label"]   = "From";
      setting["path"]    = "pedal/from";
    }
    {
      JsonObject setting = json.createNestedObject();
      setting["type"]    = "number";
      setting["label"]   = "To";
      setting["path"]    = "pedal/to";
    }

    {
      JsonObject setting = json.createNestedObject();
      setting["type"]    = "controller";
      setting["title"]   = "Potentiometer";
      setting["path"]    = "potentiometer/controller";
    }

    {
      JsonObject setting = json.createNestedObject();
      setting["type"]    = "number";
      setting["label"]   = "From";
      setting["path"]    = "potentiometer/from";
    }
    {
      JsonObject setting = json.createNestedObject();
      setting["type"]    = "number";
      setting["label"]   = "To";
      setting["path"]    = "potentiometer/to";
    }
  }

  void importConfiguration(JsonObject json) override {
    JsonObject json_midi = json["midi"];
    if (json_midi) {
      if (!json_midi["channel"].isNull()) {
        uint8_t channel = json_midi["channel"];

        if (channel < 1)
          config.channel = 0;
        else if (channel > 16)
          config.channel = 15;
        else
          config.channel = channel - 1;
      }
    }

    JsonObject json_pedal = json["pedal"];
    if (json_pedal) {
      if (!json_pedal["controller"].isNull()) {
        uint8_t controller = json_pedal["controller"];
        if (controller > 127)
          config.pedal.controller = 127;

        else
          config.pedal.controller = controller;
      }

      if (!json_pedal["from"].isNull()) {
        uint8_t value = json_pedal["from"];
        if (value > 127)
          config.pedal.from = 127;

        else
          config.pedal.from = value;
      }

      if (!json_pedal["to"].isNull()) {
        uint8_t value = json_pedal["to"];
        if (value > 127)
          config.pedal.to = 127;

        else
          config.pedal.to = value;
      }
    }

    JsonObject json_poti = json["potentiometer"];
    if (json_poti) {
      if (!json_poti["controller"].isNull()) {
        uint8_t controller = json_poti["controller"];
        if (controller > 127)
          config.poti.controller = 127;

        else
          config.poti.controller = controller;
      }

      if (!json_poti["from"].isNull()) {
        uint8_t value = json_poti["from"];
        if (value > 127)
          config.poti.from = 127;

        else
          config.poti.from = value;
      }

      if (!json_poti["to"].isNull()) {
        uint8_t value = json_poti["to"];
        if (value > 127)
          config.poti.to = 127;

        else
          config.poti.to = value;
      }
    }
  }

  void exportConfiguration(JsonObject json) override {
    {
      json["#midi"]         = "The MIDI settings";
      JsonObject json_midi  = json.createNestedObject("midi");
      json_midi["#channel"] = "The channel to send notes and control values to";
      json_midi["channel"]  = config.channel + 1;
    }

    {
      JsonObject json_pedal     = json.createNestedObject("pedal");
      json_pedal["#controller"] = "The MIDI The MIDI controller number and value range";
      json_pedal["controller"]  = config.pedal.controller;
      json_pedal["from"]        = config.pedal.from;
      json_pedal["to"]          = config.pedal.to;
    }

    {
      JsonObject json_poti     = json.createNestedObject("potentiometer");
      json_poti["#controller"] = "The MIDI The MIDI controller number and value range";
      json_poti["controller"]  = config.poti.controller;
      json_poti["from"]        = config.poti.from;
      json_poti["to"]          = config.poti.to;
    }
  }

  void exportOutput(JsonObject json) override {
    json["channel"] = config.channel;

    JsonArray json_controllers = json.createNestedArray("controllers");
    {
      JsonObject json_controller = json_controllers.createNestedObject();
      json_controller["name"]    = "Pedal";
      json_controller["number"]  = config.pedal.controller;
      json_controller["value"]   = _step_pedal;
    }
    {
      JsonObject json_controller = json_controllers.createNestedObject();
      json_controller["name"]    = "Potentiometer";
      json_controller["number"]  = config.poti.controller;
      json_controller["value"]   = _step_poti;
    }
  }
} Device;

// Dispatch MIDI packets
static class MIDI {
public:
  void loop() {
    if (!Device.usb.midi.receive(&_midi))
      return;

    if (_midi.getPort() != 0)
      return;

    Device.dispatch(&Device.usb.midi, &_midi);
  }

private:
  V2MIDI::Packet _midi{};
} MIDI;

void setup() {
  Serial.begin(9600);

  pinMode(PIN_SWITCH, INPUT_PULLUP);

  ADC.begin();
  ADC.addChannel(V2Base::Analog::ADC::getChannel(PIN_PEDAL));
  ADC.addChannel(V2Base::Analog::ADC::getChannel(PIN_POTENTIOMETER));

  Device.begin();
  Device.reset();
}

void loop() {
  MIDI.loop();
  Device.loop();

  if (Device.idle())
    Device.sleep();
}
