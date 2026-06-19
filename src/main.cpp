#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- HW (igual proyecto Synth) ----------------
constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
constexpr int I2S_BCK_PIN = 26;
constexpr int I2S_WS_PIN = 25;
constexpr int I2S_DO_PIN = 22;

constexpr int MUX_SIG_PIN = 35;
constexpr int MUX_S0_PIN = 16;
constexpr int MUX_S1_PIN = 17;
constexpr int MUX_S2_PIN = 19;
constexpr int MUX_S3_PIN = 18;

constexpr int BTN_CHANGE_WAVE_PIN = 5;
constexpr int BTN_TOGGLE_LFO_PIN = 4;
constexpr int BTN_SELECT_LFO_PIN = 13;
constexpr int BTN_SIREN_WAVE_PIN = 27;

constexpr int OLED_SDA_PIN = 21;
constexpr int OLED_SCL_PIN = 23;
constexpr int OLED_ADDR = 0x3C;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;

// ---------------- Audio ----------------
constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 256;
constexpr int MUX_CHANNELS = 12;

constexpr float OUTPUT_GAIN = 0.10f;
constexpr float SIREN_CENTER_HZ = 420.0f;
constexpr float SIREN_SWEEP_HZ = 210.0f;
constexpr float SIREN_RATE_HZ = 0.85f;

constexpr int DELAY_BUFFER_SIZE = 16384;
constexpr int REVERB_BUF_1 = 1400;
constexpr int REVERB_BUF_2 = 1900;

constexpr uint32_t DEBOUNCE_MS = 40;

enum WaveShape {
  WAVE_SINE = 0,
  WAVE_SQUARE = 1,
  WAVE_TRIANGLE = 2,
  WAVE_SAW = 3,
  WAVE_COUNT = 4
};

enum SirenWave {
  SIREN_ORIGINAL = 0,
  SIREN_SINE = 1,
  SIREN_SQUARE = 2,
  SIREN_TRIANGLE = 3,
  SIREN_SAW = 4,
  SIREN_WAVE_COUNT = 5
};

enum PotChannel {
  POT_PITCH_DEPTH = 0,
  POT_PITCH_RATE = 1,
  POT_VOLUME_RATE = 2,
  POT_VOLUME_DEPTH = 3,
  POT_FILTER_BASE = 4,
  POT_FILTER_RATE = 5,
  POT_FILTER_DEPTH = 6,
  POT_REVERB_MIX = 7,
  POT_REVERB_DECAY = 8,
  POT_DELAY_TIME = 9,
  POT_DELAY_FEEDBACK = 10,
  POT_DELAY_MIX = 11
};

enum LfoSlot {
  LFO_SLOT_PITCH = 0,
  LFO_SLOT_VOLUME = 1,
  LFO_SLOT_FILTER = 2,
  LFO_SLOT_REVERB = 3,
  LFO_SLOT_DELAY = 4,
  LFO_SLOT_COUNT = 5
};

enum DisplayPage {
  PAGE_SIREN = 0,
  PAGE_LFO_PITCH = 1,
  PAGE_LFO_VOLUME = 2,
  PAGE_LFO_FILTER = 3,
  PAGE_LFO_REVERB = 4,
  PAGE_LFO_DELAY = 5,
  PAGE_COUNT = 6
};

struct DebouncedButton {
  uint8_t pin;
  bool lastReading;
  bool stableState;
  uint32_t lastChangeMs;
};

struct LfoState {
  float phase;
  WaveShape shape;
  float rateHz;
  float depth;
  bool enabled;
};

struct PotDef {
  float minValue;
  float maxValue;
};

const PotDef potDefs[MUX_CHANNELS] = {
  {0.05f, 12.0f},
  {0.0f, 520.0f},
  {0.05f, 12.0f},
  {0.0f, 1.0f},
  {-1.0f, 1.0f},
  {0.05f, 12.0f},
  {0.0f, 1.0f},
  {0.0f, 0.8f},
  {0.10f, 0.95f},
  {20.0f, 420.0f},
  {0.0f, 0.90f},
  {0.0f, 0.80f}
};

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledReady = false;

DebouncedButton btnSelectLfo;
DebouncedButton btnChangeWave;
DebouncedButton btnSirenWave;
DebouncedButton btnToggleLfo;

LfoState pitchLfo = {0.0f, WAVE_SINE, 2.0f, 160.0f, true};
LfoState volumeLfo = {0.0f, WAVE_SINE, 3.0f, 0.35f, true};
LfoState filterLfo = {0.0f, WAVE_SINE, 0.80f, 0.45f, true};
LfoState reverbLfo = {0.0f, WAVE_SINE, 0.60f, 0.40f, true};
LfoState delayLfo = {0.0f, WAVE_SINE, 0.70f, 0.40f, true};

SirenWave sirenWave = SIREN_ORIGINAL;
LfoSlot selectedLfo = LFO_SLOT_PITCH;
DisplayPage currentPage = PAGE_SIREN;

float muxRawSmooth[MUX_CHANNELS] = {
  2048.0f, 2048.0f, 2048.0f, 2048.0f,
  2048.0f, 2048.0f, 2048.0f, 2048.0f,
  2048.0f, 2048.0f, 2048.0f, 2048.0f};
float potMapped[MUX_CHANNELS] = {0.0f};

float carrierPhase = 0.0f;
float sirenPhase = 0.0f;

float lpfState = 0.0f;
float hpfPrevInput = 0.0f;
float hpfPrevOutput = 0.0f;

float reverbBuffer1[REVERB_BUF_1] = {0.0f};
float reverbBuffer2[REVERB_BUF_2] = {0.0f};
int reverbIndex1 = 0;
int reverbIndex2 = 0;

float delayBuffer[DELAY_BUFFER_SIZE] = {0.0f};
int delayWriteIndex = 0;

float clampf(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

float mapLinear(float normalized, float minValue, float maxValue) {
  return minValue + normalized * (maxValue - minValue);
}

const char* waveName(WaveShape shape) {
  switch (shape) {
    case WAVE_SINE:
      return "SIN";
    case WAVE_SQUARE:
      return "SQR";
    case WAVE_TRIANGLE:
      return "TRI";
    case WAVE_SAW:
      return "SAW";
    default:
      return "?";
  }
}

const char* sirenWaveName(SirenWave shape) {
  switch (shape) {
    case SIREN_ORIGINAL:
      return "ORIG";
    case SIREN_SINE:
      return "SIN";
    case SIREN_SQUARE:
      return "SQR";
    case SIREN_TRIANGLE:
      return "TRI";
    case SIREN_SAW:
      return "SAW";
    default:
      return "?";
  }
}

const char* onOffText(bool enabled) {
  return enabled ? "ON" : "OFF";
}

const char* lfoSlotName(LfoSlot slot) {
  switch (slot) {
    case LFO_SLOT_PITCH:
      return "Pitch";
    case LFO_SLOT_VOLUME:
      return "Volume";
    case LFO_SLOT_FILTER:
      return "Filter";
    case LFO_SLOT_REVERB:
      return "Reverb";
    case LFO_SLOT_DELAY:
      return "Delay";
    default:
      return "?";
  }
}

LfoState& selectedLfoRef() {
  switch (selectedLfo) {
    case LFO_SLOT_PITCH:
      return pitchLfo;
    case LFO_SLOT_VOLUME:
      return volumeLfo;
    case LFO_SLOT_FILTER:
      return filterLfo;
    case LFO_SLOT_REVERB:
      return reverbLfo;
    default:
      return delayLfo;
  }
}

void syncLfoFromPage() {
  switch (currentPage) {
    case PAGE_LFO_PITCH:
      selectedLfo = LFO_SLOT_PITCH;
      break;
    case PAGE_LFO_VOLUME:
      selectedLfo = LFO_SLOT_VOLUME;
      break;
    case PAGE_LFO_FILTER:
      selectedLfo = LFO_SLOT_FILTER;
      break;
    case PAGE_LFO_REVERB:
      selectedLfo = LFO_SLOT_REVERB;
      break;
    case PAGE_LFO_DELAY:
      selectedLfo = LFO_SLOT_DELAY;
      break;
    default:
      break;
  }
}

void initButton(DebouncedButton& btn, uint8_t pin) {
  btn.pin = pin;
  btn.lastReading = HIGH;
  btn.stableState = HIGH;
  btn.lastChangeMs = 0;
  pinMode(pin, INPUT_PULLUP);
}

bool updateButtonPressed(DebouncedButton& btn) {
  bool reading = digitalRead(btn.pin);

  if (reading != btn.lastReading) {
    btn.lastReading = reading;
    btn.lastChangeMs = millis();
  }

  if ((millis() - btn.lastChangeMs) > DEBOUNCE_MS) {
    if (reading != btn.stableState) {
      btn.stableState = reading;
      if (btn.stableState == LOW) {
        return true;
      }
    }
  }

  return false;
}

void selectMuxChannel(uint8_t channel) {
  digitalWrite(MUX_S0_PIN, channel & 0x01);
  digitalWrite(MUX_S1_PIN, (channel >> 1) & 0x01);
  digitalWrite(MUX_S2_PIN, (channel >> 2) & 0x01);
  digitalWrite(MUX_S3_PIN, (channel >> 3) & 0x01);
}

int readMuxRaw(uint8_t channel) {
  selectMuxChannel(channel);
  delayMicroseconds(4);
  return analogRead(MUX_SIG_PIN);
}

float evalWaveByPhase(float phase, WaveShape shape) {
  float t = phase / TWO_PI;

  switch (shape) {
    case WAVE_SINE:
      return sinf(phase);
    case WAVE_SQUARE:
      return (sinf(phase) >= 0.0f) ? 1.0f : -1.0f;
    case WAVE_TRIANGLE:
      return 2.0f * fabsf(2.0f * t - 1.0f) - 1.0f;
    case WAVE_SAW:
      return 2.0f * t - 1.0f;
    default:
      return 0.0f;
  }
}

float evalSirenCarrier(float phase, SirenWave shape) {
  switch (shape) {
    case SIREN_ORIGINAL: {
      float s1 = sinf(phase);
      float s2 = 0.30f * sinf(phase * 2.0f);
      return clampf(s1 + s2, -1.0f, 1.0f);
    }
    case SIREN_SINE:
      return sinf(phase);
    case SIREN_SQUARE:
      return (sinf(phase) >= 0.0f) ? 1.0f : -1.0f;
    case SIREN_TRIANGLE: {
      float t = phase / TWO_PI;
      return 2.0f * fabsf(2.0f * t - 1.0f) - 1.0f;
    }
    case SIREN_SAW: {
      float t = phase / TWO_PI;
      return 2.0f * t - 1.0f;
    }
    default:
      return 0.0f;
  }
}

float nextLfoRaw(LfoState& lfo) {
  float value = evalWaveByPhase(lfo.phase, lfo.shape);
  lfo.phase += TWO_PI * lfo.rateHz / SAMPLE_RATE;
  if (lfo.phase >= TWO_PI) {
    lfo.phase -= TWO_PI;
  }
  return value;
}

float processFilter(float input, float cutoffHz, float morph) {
  float dt = 1.0f / SAMPLE_RATE;
  float rc = 1.0f / (TWO_PI * cutoffHz);

  float alphaLp = dt / (rc + dt);
  lpfState += alphaLp * (input - lpfState);

  float alphaHp = rc / (rc + dt);
  float hp = alphaHp * (hpfPrevOutput + input - hpfPrevInput);
  hpfPrevInput = input;
  hpfPrevOutput = hp;

  float mix = clampf((morph + 1.0f) * 0.5f, 0.0f, 1.0f);
  return (1.0f - mix) * lpfState + mix * hp;
}

float processReverb(float input, float mix, float decay) {
  float y1 = reverbBuffer1[reverbIndex1];
  float y2 = reverbBuffer2[reverbIndex2];

  reverbBuffer1[reverbIndex1] = input + y1 * decay;
  reverbBuffer2[reverbIndex2] = input + y2 * (decay * 0.92f);

  reverbIndex1 = (reverbIndex1 + 1) % REVERB_BUF_1;
  reverbIndex2 = (reverbIndex2 + 1) % REVERB_BUF_2;

  float wet = 0.5f * (y1 + y2);
  return input * (1.0f - mix) + wet * mix;
}

float processDelay(float input, float delayMs, float feedback, float mix) {
  int delaySamples = static_cast<int>(delayMs * SAMPLE_RATE / 1000.0f);
  if (delaySamples < 1) {
    delaySamples = 1;
  }
  if (delaySamples >= DELAY_BUFFER_SIZE) {
    delaySamples = DELAY_BUFFER_SIZE - 1;
  }

  int readIndex = delayWriteIndex - delaySamples;
  if (readIndex < 0) {
    readIndex += DELAY_BUFFER_SIZE;
  }

  float delayed = delayBuffer[readIndex];
  delayBuffer[delayWriteIndex] = input + delayed * feedback;

  delayWriteIndex++;
  if (delayWriteIndex >= DELAY_BUFFER_SIZE) {
    delayWriteIndex = 0;
  }

  return input * (1.0f - mix) + delayed * mix;
}

void updateControls() {
  for (int ch = 0; ch < MUX_CHANNELS; ch++) {
    int raw = readMuxRaw(ch);
    muxRawSmooth[ch] += 0.18f * (raw - muxRawSmooth[ch]);
    float normalized = clampf(muxRawSmooth[ch] / 4095.0f, 0.0f, 1.0f);
    potMapped[ch] = mapLinear(normalized, potDefs[ch].minValue, potDefs[ch].maxValue);
  }

  pitchLfo.rateHz = potMapped[POT_PITCH_RATE];
  pitchLfo.depth = potMapped[POT_PITCH_DEPTH];

  volumeLfo.rateHz = potMapped[POT_VOLUME_RATE];
  volumeLfo.depth = potMapped[POT_VOLUME_DEPTH];

  filterLfo.rateHz = potMapped[POT_FILTER_RATE];
  filterLfo.depth = potMapped[POT_FILTER_DEPTH];

  reverbLfo.rateHz = potMapped[POT_FILTER_RATE];
  reverbLfo.depth = potMapped[POT_FILTER_DEPTH];

  delayLfo.rateHz = potMapped[POT_FILTER_RATE];
  delayLfo.depth = potMapped[POT_FILTER_DEPTH];

  if (updateButtonPressed(btnSelectLfo)) {
    currentPage = static_cast<DisplayPage>((currentPage + 1) % PAGE_COUNT);
    syncLfoFromPage();
  }

  if (currentPage != PAGE_SIREN && updateButtonPressed(btnChangeWave)) {
    LfoState& lfo = selectedLfoRef();
    lfo.shape = static_cast<WaveShape>((lfo.shape + 1) % WAVE_COUNT);
  }

  if (updateButtonPressed(btnSirenWave)) {
    sirenWave = static_cast<SirenWave>((sirenWave + 1) % SIREN_WAVE_COUNT);
  }

  if (currentPage != PAGE_SIREN && updateButtonPressed(btnToggleLfo)) {
    LfoState& lfo = selectedLfoRef();
    lfo.enabled = !lfo.enabled;
  }
}

void drawDisplay() {
  if (!oledReady) {
    return;
  }

  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (currentPage == PAGE_SIREN) {
    display.setCursor(0, 0);
    display.print("SECCION SIRENA");

    display.setCursor(0, 8);
    display.print("Wave: ");
    display.print(sirenWaveName(sirenWave));

    // Deja una linea en blanco despues de Wave para separar la guia.
    display.setCursor(0, 26);
    display.print("Rojo: wave sirena");

    display.setCursor(0, 36);
    display.print("Blanco: sel LFO");

    display.setCursor(0, 46);
    display.print("Verde: ON/OFF LFO");

    display.setCursor(0, 56);
    display.print("Negro: wave LFO");
  } else {
    LfoState& activeLfo = selectedLfoRef();

    display.setCursor(0, 0);
    display.print("LFO ");
    display.print(lfoSlotName(selectedLfo));

    display.setCursor(0, 12);
    display.print("State: ");
    display.print(onOffText(activeLfo.enabled));

    display.setCursor(0, 22);
    display.print("Wave: ");
    display.print(waveName(activeLfo.shape));

    if (selectedLfo == LFO_SLOT_PITCH) {
      display.setCursor(0, 34);
      display.print("Rate: ");
      display.print(potMapped[POT_PITCH_RATE], 2);
      display.setCursor(0, 46);
      display.print("Depth: ");
      display.print(potMapped[POT_PITCH_DEPTH], 0);
    } else if (selectedLfo == LFO_SLOT_VOLUME) {
      display.setCursor(0, 34);
      display.print("Rate: ");
      display.print(potMapped[POT_VOLUME_RATE], 2);
      display.setCursor(0, 46);
      display.print("Depth: ");
      display.print(potMapped[POT_VOLUME_DEPTH], 2);
    } else if (selectedLfo == LFO_SLOT_FILTER) {
      display.setCursor(0, 34);
      display.print("Base: ");
      display.print(potMapped[POT_FILTER_BASE], 2);
      display.setCursor(0, 44);
      display.print("Rate: ");
      display.print(potMapped[POT_FILTER_RATE], 2);
      display.setCursor(0, 54);
      display.print("Depth: ");
      display.print(potMapped[POT_FILTER_DEPTH], 2);
    } else if (selectedLfo == LFO_SLOT_REVERB) {
      display.setCursor(0, 34);
      display.print("Mix: ");
      display.print(potMapped[POT_REVERB_MIX], 2);
      display.setCursor(0, 46);
      display.print("Decay: ");
      display.print(potMapped[POT_REVERB_DECAY], 2);
    } else {
      display.setCursor(0, 34);
      display.print("Time: ");
      display.print(potMapped[POT_DELAY_TIME], 0);
      display.setCursor(0, 44);
      display.print("Fdbk: ");
      display.print(potMapped[POT_DELAY_FEEDBACK], 2);
      display.setCursor(0, 54);
      display.print("Mix: ");
      display.print(potMapped[POT_DELAY_MIX], 2);
    }
  }

  display.display();
}

void setupI2S() {
  i2s_config_t i2s_config = {};
  i2s_config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_config.sample_rate = SAMPLE_RATE;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 8;
  i2s_config.dma_buf_len = BUFFER_SIZE;
  i2s_config.use_apll = false;
  i2s_config.tx_desc_auto_clear = true;
  i2s_config.fixed_mclk = 0;

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num = I2S_BCK_PIN;
  pin_config.ws_io_num = I2S_WS_PIN;
  pin_config.data_out_num = I2S_DO_PIN;
  pin_config.data_in_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(12);
  analogSetPinAttenuation(MUX_SIG_PIN, ADC_11db);

  pinMode(MUX_S0_PIN, OUTPUT);
  pinMode(MUX_S1_PIN, OUTPUT);
  pinMode(MUX_S2_PIN, OUTPUT);
  pinMode(MUX_S3_PIN, OUTPUT);

  initButton(btnSelectLfo, BTN_SELECT_LFO_PIN);
  initButton(btnChangeWave, BTN_CHANGE_WAVE_PIN);
  initButton(btnSirenWave, BTN_SIREN_WAVE_PIN);
  initButton(btnToggleLfo, BTN_TOGGLE_LFO_PIN);

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledReady) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Siren test boot...");
    display.display();
  }

  setupI2S();
  Serial.println("Siren test ready (Synth HW map)");
}

void loop() {
  updateControls();

  int16_t audio_buffer[BUFFER_SIZE * 2];
  const float sirenInc = TWO_PI * SIREN_RATE_HZ / SAMPLE_RATE;

  for (int i = 0; i < BUFFER_SIZE; ++i) {
    float sweep = sinf(sirenPhase);
    float freq = SIREN_CENTER_HZ + SIREN_SWEEP_HZ * sweep;

    float pitchRaw = nextLfoRaw(pitchLfo);
    if (pitchLfo.enabled) {
      freq += pitchRaw * pitchLfo.depth;
    }
    freq = clampf(freq, 60.0f, 2800.0f);

    float amp = OUTPUT_GAIN;
    float volumeRaw = nextLfoRaw(volumeLfo);
    if (volumeLfo.enabled) {
      float trem = (1.0f - volumeLfo.depth) + volumeLfo.depth * ((volumeRaw + 1.0f) * 0.5f);
      amp *= clampf(trem, 0.0f, 1.2f);
    }

    float carrier = evalSirenCarrier(carrierPhase, sirenWave);
    float sample = carrier * amp;

    float filterRaw = nextLfoRaw(filterLfo);
    float filterBase = potMapped[POT_FILTER_BASE];
    float filterMorph = filterBase;
    if (filterLfo.enabled) {
      filterMorph += filterRaw * filterLfo.depth;
    }
    filterMorph = clampf(filterMorph, -1.0f, 1.0f);

    float cutoff = 280.0f + fabsf(filterMorph) * 6500.0f;
    sample = processFilter(sample, cutoff, filterMorph);

    float reverbMix = potMapped[POT_REVERB_MIX];
    float reverbDecay = potMapped[POT_REVERB_DECAY];
    if (reverbLfo.enabled) {
      float revRaw = nextLfoRaw(reverbLfo);
      reverbMix = clampf(reverbMix + revRaw * 0.35f * reverbLfo.depth, 0.0f, 0.85f);
      reverbDecay = clampf(reverbDecay + revRaw * 0.20f * reverbLfo.depth, 0.05f, 0.98f);
    }
    sample = processReverb(sample, reverbMix, reverbDecay);

    float delayTime = potMapped[POT_DELAY_TIME];
    float delayFeedback = potMapped[POT_DELAY_FEEDBACK];
    float delayMix = potMapped[POT_DELAY_MIX];
    if (delayLfo.enabled) {
      float dlyRaw = nextLfoRaw(delayLfo);
      delayTime = clampf(delayTime + dlyRaw * 100.0f * delayLfo.depth, 10.0f, 450.0f);
      delayFeedback = clampf(delayFeedback + dlyRaw * 0.25f * delayLfo.depth, 0.0f, 0.95f);
      delayMix = clampf(delayMix + dlyRaw * 0.30f * delayLfo.depth, 0.0f, 0.90f);
    }
    sample = processDelay(sample, delayTime, delayFeedback, delayMix);

    sample = clampf(sample, -1.0f, 1.0f);
    int16_t s = static_cast<int16_t>(clampf(sample, -1.0f, 1.0f) * 32767.0f);

    audio_buffer[i * 2] = s;
    audio_buffer[i * 2 + 1] = s;

    carrierPhase += TWO_PI * freq / SAMPLE_RATE;
    if (carrierPhase >= TWO_PI) {
      carrierPhase -= TWO_PI;
    }

    sirenPhase += sirenInc;
    if (sirenPhase >= TWO_PI) {
      sirenPhase -= TWO_PI;
    }
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_PORT, audio_buffer, sizeof(audio_buffer), &bytesWritten, portMAX_DELAY);

  static uint32_t lastDisplayMs = 0;
  if (millis() - lastDisplayMs > 90) {
    drawDisplay();
    lastDisplayMs = millis();
  }
}
