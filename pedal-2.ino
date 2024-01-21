// © Kay Sievers <kay@versioduo.com>, 2020-2022
// SPDX-License-Identifier: Apache-2.0

#include <V2Device.h>
#include <V2LED.h>
#include <V2MIDI.h>
#include <V2Potentiometer.h>

V2DEVICE_METADATA("com.versioduo.pedal-2", 13, "versioduo:samd:itsybitsy");

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
    metadata.product     = "V2 pedal-2";
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
  const struct V2Potentiometer::Config _config { .nSteps{128}, .min{0.05}, .max{0.95}, .alpha{0.2}, .lag{0.007}, };
  V2Potentiometer _pedal{&_config};
  bool _reverse{};
  V2Potentiometer _poti{&_config};
  uint8_t _stepPedal{};
  uint8_t _stepPoti{};
  uint32_t _measureUsec{};
  uint32_t _eventsUsec{};
  V2MIDI::Packet _midi{};

  void handleReset() override {
    _pedal.reset();
    _poti.reset();
    _stepPedal  = 0;
    _reverse     = false;
    _stepPoti   = 0;
    _measureUsec = 0;
    _eventsUsec  = V2Base::getUsec();
    _midi        = {};
  }

  void allNotesOff() {
    sendEvents(true);
  }

  void handleLoop() override {
    if (V2Base::getUsecSince(_measureUsec) > 5 * 1000) {
      _pedal.measure(ADC.readChannel(V2Base::Analog::ADC::getChannel(PIN_PEDAL)));
      _poti.measure(1.f - ADC.readChannel(V2Base::Analog::ADC::getChannel(PIN_POTENTIOMETER)));
      _measureUsec = V2Base::getUsec();
    }

    if (V2Base::getUsecSince(_eventsUsec) > 20 * 1000) {
      const bool on = !digitalRead(PIN_SWITCH);
      if (on != _reverse)
        _reverse = on;

      sendEvents();
      _eventsUsec = V2Base::getUsec();
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

      if (force || _stepPedal != value) {
        _stepPedal = value;
        send(_midi.setControlChange(config.channel, config.pedal.controller, value));
      }
    }

    {
      const float range   = (int8_t)config.poti.to - (int8_t)config.poti.from;
      const uint8_t value = config.poti.from + (range * _poti.getFraction());

      if (force || _stepPoti != value) {
        _stepPoti = value;
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
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "number";
      setting["title"]   = "MIDI";
      setting["label"]   = "Channel";
      setting["min"]     = 1;
      setting["max"]     = 16;
      setting["input"]   = "select";
      setting["path"]    = "midi/channel";
    }

    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "controller";
      setting["title"]   = "Pedal";
      setting["path"]    = "pedal/controller";
    }
    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "number";
      setting["label"]   = "From";
      setting["path"]    = "pedal/from";
    }
    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "number";
      setting["label"]   = "To";
      setting["path"]    = "pedal/to";
    }

    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "controller";
      setting["title"]   = "Potentiometer";
      setting["path"]    = "potentiometer/controller";
    }

    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "number";
      setting["label"]   = "From";
      setting["path"]    = "potentiometer/from";
    }
    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "number";
      setting["label"]   = "To";
      setting["path"]    = "potentiometer/to";
    }
  }

  void importConfiguration(JsonObject json) override {
    JsonObject jsonMidi = json["midi"];
    if (jsonMidi) {
      if (!jsonMidi["channel"].isNull()) {
        uint8_t channel = jsonMidi["channel"];

        if (channel < 1)
          config.channel = 0;
        else if (channel > 16)
          config.channel = 15;
        else
          config.channel = channel - 1;
      }
    }

    JsonObject jsonPedal = json["pedal"];
    if (jsonPedal) {
      if (!jsonPedal["controller"].isNull()) {
        uint8_t controller = jsonPedal["controller"];
        if (controller > 127)
          config.pedal.controller = 127;

        else
          config.pedal.controller = controller;
      }

      if (!jsonPedal["from"].isNull()) {
        uint8_t value = jsonPedal["from"];
        if (value > 127)
          config.pedal.from = 127;

        else
          config.pedal.from = value;
      }

      if (!jsonPedal["to"].isNull()) {
        uint8_t value = jsonPedal["to"];
        if (value > 127)
          config.pedal.to = 127;

        else
          config.pedal.to = value;
      }
    }

    JsonObject jsonPoti = json["potentiometer"];
    if (jsonPoti) {
      if (!jsonPoti["controller"].isNull()) {
        uint8_t controller = jsonPoti["controller"];
        if (controller > 127)
          config.poti.controller = 127;

        else
          config.poti.controller = controller;
      }

      if (!jsonPoti["from"].isNull()) {
        uint8_t value = jsonPoti["from"];
        if (value > 127)
          config.poti.from = 127;

        else
          config.poti.from = value;
      }

      if (!jsonPoti["to"].isNull()) {
        uint8_t value = jsonPoti["to"];
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
      JsonObject jsonMidi  = json["midi"].to<JsonObject>();
      jsonMidi["#channel"] = "The channel to send notes and control values to";
      jsonMidi["channel"]  = config.channel + 1;
    }

    {
      JsonObject jsonPedal     = json["pedal"].to<JsonObject>();
      jsonPedal["#controller"] = "The MIDI The MIDI controller number and value range";
      jsonPedal["controller"]  = config.pedal.controller;
      jsonPedal["from"]        = config.pedal.from;
      jsonPedal["to"]          = config.pedal.to;
    }

    {
      JsonObject jsonPoti     = json["potentiometer"].to<JsonObject>();
      jsonPoti["#controller"] = "The MIDI The MIDI controller number and value range";
      jsonPoti["controller"]  = config.poti.controller;
      jsonPoti["from"]        = config.poti.from;
      jsonPoti["to"]          = config.poti.to;
    }
  }

  void exportOutput(JsonObject json) override {
    json["channel"] = config.channel;

    JsonArray jsonControllers = json["controllers"].to<JsonArray>();
    {
      JsonObject jsonController = jsonControllers.add<JsonObject>();
      jsonController["name"]    = "Pedal";
      jsonController["number"]  = config.pedal.controller;
      jsonController["value"]   = _stepPedal;
    }
    {
      JsonObject jsonController = jsonControllers.add<JsonObject>();
      jsonController["name"]    = "Potentiometer";
      jsonController["number"]  = config.poti.controller;
      jsonController["value"]   = _stepPoti;
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
