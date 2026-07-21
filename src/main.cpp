#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <stdio.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Arquitectura del firmware:
// 1) Lee controles por CD4067 -> ADC.
// 2) Genera y procesa la sirena en software.
// 3) Envia PCM estereo por I2S al PCM5102A.
// 4) Muestra estado resumido en OLED.

// ---------------- Hardware ----------------
// Pines I2S desde ESP32 hacia el modulo DAC PCM5102A.
constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
constexpr int I2S_BCK_PIN = 26;
constexpr int I2S_WS_PIN = 25;
constexpr int I2S_DO_PIN = 22;

// Lineas de control/direccion del CD4067 y pin de entrada ADC.
constexpr int MUX_SIG_PIN = 35;
constexpr int MUX_S0_PIN = 16;
constexpr int MUX_S1_PIN = 17;
constexpr int MUX_S2_PIN = 19;
constexpr int MUX_S3_PIN = 18;

// Botones de usuario (activos en bajo con INPUT_PULLUP).
constexpr int BTN_SIREN_WAVE_PIN = 27;
constexpr int BTN_PAGE_PIN = 13;

// Conexion de la pantalla OLED.
constexpr int OLED_SDA_PIN = 21;
constexpr int OLED_SCL_PIN = 23;
constexpr int OLED_ADDR = 0x3C;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;

// ---------------- Audio ----------------
// Parametros del flujo PCM.
constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 256;

// Parametros de movimiento de la sirena.
constexpr float SIREN_SWEEP_HZ = 210.0f;
constexpr float PITCH_LFO_RATE_HZ = 0.55f;
constexpr float PITCH_LFO_DEPTH_HZ = 70.0f;
constexpr float OUTPUT_GAIN_MAX = 0.14f;

// Tamano de buffers internos para reverb/delay simples.
constexpr int REVERB_BUF_1 = 1400;
constexpr int REVERB_BUF_2 = 1900;
constexpr int DELAY_BUFFER_SIZE = 16384;

constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t CONTROL_UPDATE_MS = 10;

// Modo de diagnostico temporal para aislar fuentes de ruido.
// En false vuelve a controles normales leidos desde MUX.
constexpr bool DIAG_FIXED_CONTROLS = false;
constexpr float DIAG_FIXED_LFO_RATE_HZ = 1.0f;
constexpr float DIAG_FIXED_LFO_DEPTH_HZ = 0.0f;
constexpr float DIAG_FIXED_MASTER_GAIN = 0.60f;
constexpr float DIAG_FIXED_FILTER_MORPH = 0.0f;
constexpr float DIAG_FIXED_REVERB_MIX = 0.0f;
constexpr float DIAG_FIXED_DELAY_MIX = 0.0f;
constexpr bool DIAG_DISABLE_OLED = false;
constexpr bool DIAG_DISABLE_OLED_REFRESH = false;
constexpr bool DIAG_USE_TONETEST_CORE = false;

// Reduce rafagas I2C de OLED para minimizar acople digital audible.
constexpr uint32_t OLED_REFRESH_MIN_MS = 350;
constexpr uint32_t OLED_REFRESH_FORCE_MS = 2000;

// Endurecimiento de lectura MUX para reducir arrastre entre canales.
constexpr float POT_SMOOTH_ALPHA = 0.10f;
constexpr uint16_t MUX_SETTLE_US = 300;
constexpr uint16_t MUX_INTER_SAMPLE_US = 40;
constexpr uint8_t MUX_DUMMY_READS = 2;
constexpr uint8_t MUX_AVG_SAMPLES = 4;
constexpr float PARAM_SMOOTH_ALPHA = 0.0015f;
constexpr uint32_t BUTTON_HOLD_MS = 650;
constexpr float MOD_LFO_RATE_HZ = 0.70f;

// Mapa logico de los seis canales activos del MUX.
enum PotChannel {
  POT_MASTER_GAIN = 0,  // I0
  POT_SIREN_RATE = 1,   // I1
  POT_CENTER_PITCH = 2, // I2
  POT_FILTER_MORPH = 3, // I3
  POT_REVERB_MIX = 4,   // I4
  POT_DELAY_MIX = 5     // I5
};

constexpr int POT_CHANNELS = 6;

// Formas de oscilador disponibles para la portadora de sirena.
enum SirenWave {
  SIREN_ORIGINAL = 0,
  SIREN_SINE = 1,
  SIREN_SQUARE = 2,
  SIREN_TRIANGLE = 3,
  SIREN_SAW = 4,
  SIREN_WAVE_COUNT = 5
};

// Paginas de UI asociadas 1:1 a parametros I0..I5.
enum ParamPage {
  PAGE_GAIN = 0,
  PAGE_SIREN_RATE = 1,
  PAGE_CENTER_PITCH = 2,
  PAGE_FILTER = 3,
  PAGE_REVERB = 4,
  PAGE_DELAY = 5,
  PAGE_COUNT = 6
};

struct DebouncedButton {
  uint8_t pin;
  bool lastReading;
  bool stableState;
  uint32_t lastChangeMs;
};

// Mapeo entre valor ADC normalizado [0..1] y rango de cada control.
struct PotDef {
  float minValue;
  float maxValue;
};

const PotDef potDefs[POT_CHANNELS] = {
  {0.0f, 1.0f},    // I0: Master Gain (0..100%)
  {12.0f, 300.0f}, // I1: Velocidad de sirena (BPM)
  {110.0f, 880.0f},// I2: Pitch base (Hz)
  {-1.0f, 1.0f},   // I3: Filter morph (LP..HP)
  {0.0f, 1.0f},    // I4: Reverb mix
  {0.0f, 1.0f}     // I5: Delay mix
};

const float potDeadband[POT_CHANNELS] = {
  0.006f,
  0.8f,
  1.2f,
  0.01f,
  0.01f,
  0.01f};

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledReady = false;

DebouncedButton btnSirenWave;
DebouncedButton btnPage;

SirenWave sirenWave = SIREN_ORIGINAL;
uint8_t displayPage = PAGE_GAIN;

// Configuracion de variacion independiente por parametro/pagina.
SirenWave paramModWave[POT_CHANNELS] = {
  SIREN_SINE, SIREN_SINE, SIREN_SINE,
  SIREN_SINE, SIREN_SINE, SIREN_SINE};
bool paramModSelected[POT_CHANNELS] = {
  false, false, false,
  false, false, false};

// Estados de pulsacion para diferenciar toque corto y largo.
bool btnWavePressActive = false;
bool btnWaveLongHandled = false;
uint32_t btnWavePressStartMs = 0;
bool btnModePressActive = false;
bool btnModeLongHandled = false;
uint32_t btnModePressStartMs = 0;

// Estado ADC por canal: valor crudo suavizado y valor de control mapeado.
float muxRawSmooth[POT_CHANNELS] = {
  2048.0f, 2048.0f, 2048.0f,
  2048.0f, 2048.0f, 2048.0f};
float potMapped[POT_CHANNELS] = {0.0f};

// Fases de osciladores: portadora, barrido de sirena y LFO de pitch.
float carrierPhase = 0.0f;
float sirenPhase = 0.0f;
float lfoPhase = 0.0f;
float modPhase = 0.0f;
float toneTestSweepPhase = 0.0f;

// Estado historico del filtro.
float lpfState = 0.0f;
float hpfPrevInput = 0.0f;
float hpfPrevOutput = 0.0f;

// Buffers de efectos y cursores de escritura.
float reverbBuffer1[REVERB_BUF_1] = {0.0f};
float reverbBuffer2[REVERB_BUF_2] = {0.0f};
int reverbIndex1 = 0;
int reverbIndex2 = 0;

float delayBuffer[DELAY_BUFFER_SIZE] = {0.0f};
int delayWriteIndex = 0;

// Objetivos suavizados de parametros usados dentro del loop de audio.
float smoothedMasterGain = 0.0f;
float smoothedSirenRateBpm = 90.0f;
float smoothedCenterPitchHz = 440.0f;
float smoothedFilterMorph = 0.0f;
float smoothedReverbMix = 0.0f;
float smoothedDelayMix = 0.0f;

// Indice de canal en round-robin para escaneo de MUX.
uint8_t muxScanChannel = 0;

// Helper generico para limitar floats.
float clampf(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

// Helper de interpolacion lineal para mapear controles normalizados a rangos reales.
float mapLinear(float normalized, float minValue, float maxValue) {
  return minValue + normalized * (maxValue - minValue);
}

float bpmToHz(float bpm) {
  return bpm / 60.0f;
}

void drawCenteredText(int y, const char* text) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int x = (OLED_WIDTH - static_cast<int>(w)) / 2;
  if (x < 0) {
    x = 0;
  }
  display.setCursor(x, y);
  display.print(text);
}

// Dibuja un icono de forma de onda en la OLED sin depender de caracteres.
void drawWaveIcon(int x, int y, SirenWave shape) {
  const int w = 26;
  const int h = 8;
  const int yMid = y + (h / 2);
  const int yTop = y;
  const int yBot = y + h;

  switch (shape) {
    case SIREN_ORIGINAL:
      // Se usa una O mayuscula para diferenciar claramente el modo original.
      display.setCursor(x + 8, yTop);
      display.print("O");
      break;
    case SIREN_SINE:
      display.drawLine(x, yMid, x + 4, yTop + 1, SSD1306_WHITE);
      display.drawLine(x + 4, yTop + 1, x + 8, yMid, SSD1306_WHITE);
      display.drawLine(x + 8, yMid, x + 12, yBot - 1, SSD1306_WHITE);
      display.drawLine(x + 12, yBot - 1, x + 16, yMid, SSD1306_WHITE);
      display.drawLine(x + 16, yMid, x + 20, yTop + 1, SSD1306_WHITE);
      display.drawLine(x + 20, yTop + 1, x + 24, yMid, SSD1306_WHITE);
      break;
    case SIREN_SQUARE:
      display.drawLine(x, yBot - 1, x + 6, yBot - 1, SSD1306_WHITE);
      display.drawLine(x + 6, yBot - 1, x + 6, yTop + 1, SSD1306_WHITE);
      display.drawLine(x + 6, yTop + 1, x + 14, yTop + 1, SSD1306_WHITE);
      display.drawLine(x + 14, yTop + 1, x + 14, yBot - 1, SSD1306_WHITE);
      display.drawLine(x + 14, yBot - 1, x + 22, yBot - 1, SSD1306_WHITE);
      break;
    case SIREN_TRIANGLE:
      display.drawLine(x, yBot - 1, x + 6, yTop + 1, SSD1306_WHITE);
      display.drawLine(x + 6, yTop + 1, x + 12, yBot - 1, SSD1306_WHITE);
      display.drawLine(x + 12, yBot - 1, x + 18, yTop + 1, SSD1306_WHITE);
      display.drawLine(x + 18, yTop + 1, x + 24, yBot - 1, SSD1306_WHITE);
      break;
    case SIREN_SAW:
      display.drawLine(x, yBot - 1, x + 8, yTop + 1, SSD1306_WHITE);
      display.drawLine(x + 8, yTop + 1, x + 8, yBot - 1, SSD1306_WHITE);
      display.drawLine(x + 8, yBot - 1, x + 16, yTop + 1, SSD1306_WHITE);
      display.drawLine(x + 16, yTop + 1, x + 16, yBot - 1, SSD1306_WHITE);
      display.drawLine(x + 16, yBot - 1, x + 24, yTop + 1, SSD1306_WHITE);
      break;
    default:
      display.drawRect(x, yTop + 1, w - 2, h - 1, SSD1306_WHITE);
      break;
  }
}

const char* pageName(uint8_t page) {
  switch (page) {
    case PAGE_GAIN:
      return "I0 GAIN";
    case PAGE_SIREN_RATE:
      return "I1 VELOCIDAD";
    case PAGE_CENTER_PITCH:
      return "I2 PITCH BASE";
    case PAGE_FILTER:
      return "I3 FILTER";
    case PAGE_REVERB:
      return "I4 REVERB";
    case PAGE_DELAY:
      return "I5 DELAY";
    default:
      return "PARAM";
  }
}

const char* pageShortCode(uint8_t page) {
  switch (page) {
    case PAGE_GAIN:
      return "I0";
    case PAGE_SIREN_RATE:
      return "I1";
    case PAGE_CENTER_PITCH:
      return "I2";
    case PAGE_FILTER:
      return "I3";
    case PAGE_REVERB:
      return "I4";
    case PAGE_DELAY:
      return "I5";
    default:
      return "?";
  }
}

void formatNoteName(float frequencyHz, char* out, size_t outSize) {
  static const char* kNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

  float clamped = clampf(frequencyHz, 20.0f, 5000.0f);
  int midi = static_cast<int>(roundf(69.0f + 12.0f * log2f(clamped / 440.0f)));
  int noteIndex = ((midi % 12) + 12) % 12;
  int octave = (midi / 12) - 1;
  snprintf(out, outSize, "%s%d", kNames[noteIndex], octave);
}

void drawPageFooter(uint8_t page) {
  char line[22];
  snprintf(line, sizeof(line), "P:%s W:%c", pageShortCode(page), paramModSelected[page] ? '*' : '-');
  drawCenteredText(44, line);
  drawWaveIcon((OLED_WIDTH - 26) / 2, 54, paramModWave[page]);
}

// Inicializa un boton en modo pull-up y resetea estado de debounce.
void initButton(DebouncedButton& btn, uint8_t pin) {
  btn.pin = pin;
  btn.lastReading = HIGH;
  btn.stableState = HIGH;
  btn.lastChangeMs = 0;
  pinMode(pin, INPUT_PULLUP);
}

// Helper de debounce que devuelve true una vez por pulsacion valida.
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

// Selecciona canal activo del CD4067 por el bus de direccion de 4 bits.
void selectMuxChannel(uint8_t channel) {
  digitalWrite(MUX_S0_PIN, channel & 0x01);
  digitalWrite(MUX_S1_PIN, (channel >> 1) & 0x01);
  digitalWrite(MUX_S2_PIN, (channel >> 2) & 0x01);
  digitalWrite(MUX_S3_PIN, (channel >> 3) & 0x01);
}

// Lee un canal del MUX con tiempo de asentamiento, lectura dummy y promediado.
int readMuxRaw(uint8_t channel) {
  selectMuxChannel(channel);
  delayMicroseconds(MUX_SETTLE_US);

  for (uint8_t i = 0; i < MUX_DUMMY_READS; i++) {
    analogRead(MUX_SIG_PIN);
    delayMicroseconds(MUX_INTER_SAMPLE_US);
  }

  uint32_t sum = 0;
  for (uint8_t i = 0; i < MUX_AVG_SAMPLES; i++) {
    sum += static_cast<uint32_t>(analogRead(MUX_SIG_PIN));
    if (i + 1 < MUX_AVG_SAMPLES) {
      delayMicroseconds(MUX_INTER_SAMPLE_US);
    }
  }

  return static_cast<int>(sum / MUX_AVG_SAMPLES);
}

// Captura valores iniciales de controles al arranque para evitar saltos bruscos.
void initPotStateFromHardware() {
  for (int ch = 0; ch < POT_CHANNELS; ch++) {
    int raw = readMuxRaw(ch);
    muxRawSmooth[ch] = static_cast<float>(raw);

    float normalized = clampf(muxRawSmooth[ch] / 4095.0f, 0.0f, 1.0f);
    potMapped[ch] = mapLinear(normalized, potDefs[ch].minValue, potDefs[ch].maxValue);
  }

  smoothedMasterGain = potMapped[POT_MASTER_GAIN];
  smoothedSirenRateBpm = potMapped[POT_SIREN_RATE];
  smoothedCenterPitchHz = potMapped[POT_CENTER_PITCH];
  smoothedFilterMorph = potMapped[POT_FILTER_MORPH];
  smoothedReverbMix = potMapped[POT_REVERB_MIX];
  smoothedDelayMix = potMapped[POT_DELAY_MIX];
}

// Actualiza un canal de control por ciclo para reducir crosstalk del MUX.
void updateControls() {
  int ch = static_cast<int>(muxScanChannel);
  float previousMapped = potMapped[ch];
  int raw = readMuxRaw(ch);
  muxRawSmooth[ch] += POT_SMOOTH_ALPHA * (raw - muxRawSmooth[ch]);

  float normalized = clampf(muxRawSmooth[ch] / 4095.0f, 0.0f, 1.0f);
  float mapped = mapLinear(normalized, potDefs[ch].minValue, potDefs[ch].maxValue);
  if (fabsf(mapped - previousMapped) < potDeadband[ch]) {
    mapped = previousMapped;
  }
  potMapped[ch] = mapped;

  muxScanChannel = static_cast<uint8_t>((muxScanChannel + 1) % POT_CHANNELS);

  // Actualiza estados de debounce para ambos botones.
  updateButtonPressed(btnSirenWave);
  updateButtonPressed(btnPage);

  uint32_t nowMs = millis();

  // GPIO27:
  // - Toque: cambia forma de onda base.
  // - Mantener: recorre pagina/parametro en foco.
  if (btnSirenWave.stableState == LOW) {
    if (!btnWavePressActive) {
      btnWavePressActive = true;
      btnWaveLongHandled = false;
      btnWavePressStartMs = nowMs;
    } else if (!btnWaveLongHandled && (nowMs - btnWavePressStartMs >= BUTTON_HOLD_MS)) {
      displayPage = static_cast<uint8_t>((displayPage + 1) % PAGE_COUNT);
      btnWaveLongHandled = true;
      Serial.print("Pagina activa: ");
      Serial.println(pageName(displayPage));
    }
  } else if (btnWavePressActive) {
    if (!btnWaveLongHandled) {
      sirenWave = static_cast<SirenWave>((sirenWave + 1) % SIREN_WAVE_COUNT);
    }
    btnWavePressActive = false;
    btnWaveLongHandled = false;
  }

  // GPIO13:
  // - Toque: recorre onda de variacion del parametro/pagina activa.
  // - Mantener: selecciona/deselecciona esa variacion.
  if (btnPage.stableState == LOW) {
    if (!btnModePressActive) {
      btnModePressActive = true;
      btnModeLongHandled = false;
      btnModePressStartMs = nowMs;
    } else if (!btnModeLongHandled && (nowMs - btnModePressStartMs >= BUTTON_HOLD_MS)) {
      paramModSelected[displayPage] = !paramModSelected[displayPage];
      btnModeLongHandled = true;
      Serial.print("Mod ");
      Serial.print(pageName(displayPage));
      Serial.print(": ");
      Serial.println(paramModSelected[displayPage] ? "ON" : "OFF");
    }
  } else if (btnModePressActive) {
    if (!btnModeLongHandled) {
      paramModWave[displayPage] = static_cast<SirenWave>((paramModWave[displayPage] + 1) % SIREN_WAVE_COUNT);
    }
    btnModePressActive = false;
    btnModeLongHandled = false;
  }
}

// Fuerza valores fijos conocidos en modo diagnostico.
void applyFixedDiagnosticControls() {
  potMapped[POT_MASTER_GAIN] = DIAG_FIXED_MASTER_GAIN;
  potMapped[POT_SIREN_RATE] = DIAG_FIXED_LFO_RATE_HZ * 60.0f;
  potMapped[POT_CENTER_PITCH] = 440.0f + DIAG_FIXED_LFO_DEPTH_HZ;
  potMapped[POT_FILTER_MORPH] = DIAG_FIXED_FILTER_MORPH;
  potMapped[POT_REVERB_MIX] = DIAG_FIXED_REVERB_MIX;
  potMapped[POT_DELAY_MIX] = DIAG_FIXED_DELAY_MIX;

  smoothedMasterGain = DIAG_FIXED_MASTER_GAIN;
  smoothedSirenRateBpm = DIAG_FIXED_LFO_RATE_HZ * 60.0f;
  smoothedCenterPitchHz = 440.0f + DIAG_FIXED_LFO_DEPTH_HZ;
  smoothedFilterMorph = DIAG_FIXED_FILTER_MORPH;
  smoothedReverbMix = DIAG_FIXED_REVERB_MIX;
  smoothedDelayMix = DIAG_FIXED_DELAY_MIX;
}

// Genera una muestra de forma de onda para una fase dada.
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

// Filtro LP/HP con morph: morph<0 favorece LP, morph>0 favorece HP.
float processFilter(float input, float cutoffHz, float morph) {
  float dt = 1.0f / SAMPLE_RATE;
  float rc = 1.0f / (TWO_PI * cutoffHz);

  float alphaLp = dt / (rc + dt);
  lpfState += alphaLp * (input - lpfState);

  float alphaHp = rc / (rc + dt);
  float hp = alphaHp * (hpfPrevOutput + input - hpfPrevInput);
  hpfPrevInput = input;
  hpfPrevOutput = hp;

  float amount = clampf(fabsf(morph), 0.0f, 1.0f);
  float selected = (morph < 0.0f) ? lpfState : hp;
  return (1.0f - amount) * input + amount * selected;
}

// Reverb liviana de dos taps con realimentacion.
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

// Delay simple con tiempo fijo y realimentacion.
float processDelay(float input, float mix, float delayMs, float feedback) {
  int delaySamples = static_cast<int>(delayMs * SAMPLE_RATE / 1000.0f);
  delaySamples = static_cast<int>(clampf(static_cast<float>(delaySamples), 1.0f, static_cast<float>(DELAY_BUFFER_SIZE - 1)));

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

// Decide cuando refrescar OLED para limitar actividad I2C y acople de ruido.
bool displayNeedsUpdate(uint32_t nowMs) {
  static bool initialized = false;
  static uint32_t lastDisplayMs = 0;
  static uint32_t lastForcedMs = 0;
  static SirenWave lastWave = SIREN_ORIGINAL;
  static uint8_t lastPage = PAGE_GAIN;
  static float lastPot[POT_CHANNELS] = {0.0f};
  static SirenWave lastModWave[POT_CHANNELS] = {
    SIREN_SINE, SIREN_SINE, SIREN_SINE,
    SIREN_SINE, SIREN_SINE, SIREN_SINE};
  static bool lastModSelected[POT_CHANNELS] = {
    false, false, false,
    false, false, false};

  if (!initialized) {
    initialized = true;
    lastWave = sirenWave;
    lastPage = displayPage;
    for (int ch = 0; ch < POT_CHANNELS; ch++) {
      lastPot[ch] = potMapped[ch];
      lastModWave[ch] = paramModWave[ch];
      lastModSelected[ch] = paramModSelected[ch];
    }
    lastDisplayMs = nowMs;
    lastForcedMs = nowMs;
    return true;
  }

  bool changed = (sirenWave != lastWave) || (displayPage != lastPage);
  for (int ch = 0; ch < POT_CHANNELS; ch++) {
    if (fabsf(potMapped[ch] - lastPot[ch]) > (potDeadband[ch] * 2.0f)) {
      changed = true;
      break;
    }
    if (paramModWave[ch] != lastModWave[ch] || paramModSelected[ch] != lastModSelected[ch]) {
      changed = true;
      break;
    }
  }

  bool dueByDelta = (nowMs - lastDisplayMs >= OLED_REFRESH_MIN_MS) && changed;
  bool dueByForce = (nowMs - lastForcedMs >= OLED_REFRESH_FORCE_MS);
  if (!(dueByDelta || dueByForce)) {
    return false;
  }

  lastWave = sirenWave;
  lastPage = displayPage;
  for (int ch = 0; ch < POT_CHANNELS; ch++) {
    lastPot[ch] = potMapped[ch];
    lastModWave[ch] = paramModWave[ch];
    lastModSelected[ch] = paramModSelected[ch];
  }
  lastDisplayMs = nowMs;
  if (dueByForce) {
    lastForcedMs = nowMs;
  }

  return true;
}

// Dibuja la pagina OLED seleccionada con el estado actual de controles.
void drawDisplay() {
  if (!oledReady) {
    return;
  }

  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  char line[32];

  if (displayPage == PAGE_GAIN) {
    drawCenteredText(0, "SIRENA");
    drawCenteredText(10, "WAVE BASE");
    drawWaveIcon((OLED_WIDTH - 26) / 2, 20, sirenWave);
    snprintf(line, sizeof(line), "GAIN %d%%", static_cast<int>(potMapped[POT_MASTER_GAIN] * 100.0f));
    drawCenteredText(34, line);
  } else if (displayPage == PAGE_SIREN_RATE) {
    drawCenteredText(0, "I1 VELOCIDAD");
    snprintf(line, sizeof(line), "%d BPM", static_cast<int>(potMapped[POT_SIREN_RATE] + 0.5f));
    drawCenteredText(18, line);
    snprintf(line, sizeof(line), "%.2f HZ", bpmToHz(potMapped[POT_SIREN_RATE]));
    drawCenteredText(30, line);
  } else if (displayPage == PAGE_CENTER_PITCH) {
    char note[8];
    formatNoteName(potMapped[POT_CENTER_PITCH], note, sizeof(note));
    drawCenteredText(0, "I2 PITCH BASE");
    snprintf(line, sizeof(line), "NOTA %s", note);
    drawCenteredText(18, line);
    snprintf(line, sizeof(line), "%.1f HZ", potMapped[POT_CENTER_PITCH]);
    drawCenteredText(30, line);
  } else if (displayPage == PAGE_FILTER) {
    drawCenteredText(0, "I3 FILTER");
    snprintf(line, sizeof(line), "MORPH %.2f", potMapped[POT_FILTER_MORPH]);
    drawCenteredText(24, line);
  } else if (displayPage == PAGE_REVERB) {
    drawCenteredText(0, "I4 REVERB");
    snprintf(line, sizeof(line), "MIX %d%%", static_cast<int>(potMapped[POT_REVERB_MIX] * 100.0f));
    drawCenteredText(24, line);
  } else {
    drawCenteredText(0, "I5 DELAY");
    snprintf(line, sizeof(line), "MIX %d%%", static_cast<int>(potMapped[POT_DELAY_MIX] * 100.0f));
    drawCenteredText(24, line);
  }

  drawPageFooter(displayPage);

  display.display();
}

// Configura el periferico I2S del ESP32 y asigna pines de audio.
void setupI2S() {
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = SAMPLE_RATE;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = BUFFER_SIZE;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = true;
  i2sConfig.fixed_mclk = 0;

  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = I2S_BCK_PIN;
  pinConfig.ws_io_num = I2S_WS_PIN;
  pinConfig.data_out_num = I2S_DO_PIN;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_PORT, &i2sConfig, 0, nullptr);
  i2s_set_pin(I2S_PORT, &pinConfig);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

// Inicializacion de hardware y subsistemas.
void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(12);
  analogSetPinAttenuation(MUX_SIG_PIN, ADC_11db);

  pinMode(MUX_S0_PIN, OUTPUT);
  pinMode(MUX_S1_PIN, OUTPUT);
  pinMode(MUX_S2_PIN, OUTPUT);
  pinMode(MUX_S3_PIN, OUTPUT);

  initButton(btnSirenWave, BTN_SIREN_WAVE_PIN);
  initButton(btnPage, BTN_PAGE_PIN);

  if (DIAG_FIXED_CONTROLS) {
    applyFixedDiagnosticControls();
  } else {
    initPotStateFromHardware();
  }

  if (!DIAG_DISABLE_OLED) {
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    if (oledReady) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("Synth simple boot");
      display.display();
    }
  }

  setupI2S();
  Serial.println("Synth simple mode ready");
}

// Loop principal: controles -> sintesis por bloques -> salida I2S -> OLED opcional.
void loop() {
  static uint32_t lastControlMs = 0;
  uint32_t now = millis();
  if (!DIAG_FIXED_CONTROLS && (now - lastControlMs >= CONTROL_UPDATE_MS)) {
    updateControls();
    lastControlMs = now;
  }

  if (DIAG_FIXED_CONTROLS) {
    potMapped[POT_MASTER_GAIN] = DIAG_FIXED_MASTER_GAIN;
    potMapped[POT_SIREN_RATE] = DIAG_FIXED_LFO_RATE_HZ * 60.0f;
    potMapped[POT_CENTER_PITCH] = 440.0f + DIAG_FIXED_LFO_DEPTH_HZ;
    potMapped[POT_FILTER_MORPH] = DIAG_FIXED_FILTER_MORPH;
    potMapped[POT_REVERB_MIX] = DIAG_FIXED_REVERB_MIX;
    potMapped[POT_DELAY_MIX] = DIAG_FIXED_DELAY_MIX;
  }

  float targetMasterGain = potMapped[POT_MASTER_GAIN];
  float targetSirenRateBpm = potMapped[POT_SIREN_RATE];
  float targetCenterPitchHz = potMapped[POT_CENTER_PITCH];
  float targetFilterMorph = potMapped[POT_FILTER_MORPH];
  float targetReverbMix = potMapped[POT_REVERB_MIX];
  float targetDelayMix = potMapped[POT_DELAY_MIX];

  for (int ch = 0; ch < POT_CHANNELS; ch++) {
    if (!paramModSelected[ch]) {
      continue;
    }

    float modSignal = evalSirenCarrier(modPhase, paramModWave[ch]);
    switch (ch) {
      case POT_MASTER_GAIN:
        targetMasterGain = clampf(targetMasterGain + modSignal * 0.20f, potDefs[POT_MASTER_GAIN].minValue, potDefs[POT_MASTER_GAIN].maxValue);
        break;
      case POT_SIREN_RATE:
        targetSirenRateBpm = clampf(targetSirenRateBpm + modSignal * 35.0f, potDefs[POT_SIREN_RATE].minValue, potDefs[POT_SIREN_RATE].maxValue);
        break;
      case POT_CENTER_PITCH:
        targetCenterPitchHz = clampf(targetCenterPitchHz + modSignal * 120.0f, potDefs[POT_CENTER_PITCH].minValue, potDefs[POT_CENTER_PITCH].maxValue);
        break;
      case POT_FILTER_MORPH:
        targetFilterMorph = clampf(targetFilterMorph + modSignal * 0.25f, potDefs[POT_FILTER_MORPH].minValue, potDefs[POT_FILTER_MORPH].maxValue);
        break;
      case POT_REVERB_MIX:
        targetReverbMix = clampf(targetReverbMix + modSignal * 0.22f, potDefs[POT_REVERB_MIX].minValue, potDefs[POT_REVERB_MIX].maxValue);
        break;
      case POT_DELAY_MIX:
        targetDelayMix = clampf(targetDelayMix + modSignal * 0.22f, potDefs[POT_DELAY_MIX].minValue, potDefs[POT_DELAY_MIX].maxValue);
        break;
      default:
        break;
    }
  }

  // Buffer estereo intercalado (L,R,L,R...).
  int16_t audioBuffer[BUFFER_SIZE * 2];

  for (int i = 0; i < BUFFER_SIZE; ++i) {
    float sample = 0.0f;

    if (DIAG_USE_TONETEST_CORE) {
      // Nucleo minimo alternativo usado solo para diagnostico A/B.
      const float sweep = 0.5f * (sinf(toneTestSweepPhase) + 1.0f);
      const float frequency = 420.0f + (960.0f - 420.0f) * sweep;

      const float fundamental = sinf(carrierPhase);
      const float harmonic = 0.22f * sinf(carrierPhase * 2.0f);
      sample = (fundamental + harmonic) * (0.18f * DIAG_FIXED_MASTER_GAIN);

      carrierPhase += TWO_PI * frequency / SAMPLE_RATE;
      if (carrierPhase >= TWO_PI) {
        carrierPhase -= TWO_PI;
      }

      toneTestSweepPhase += TWO_PI * 0.75f / SAMPLE_RATE;
      if (toneTestSweepPhase >= TWO_PI) {
        toneTestSweepPhase -= TWO_PI;
      }
    } else {
      // Ruta normal de audio de Synth.
      smoothedMasterGain += (targetMasterGain - smoothedMasterGain) * PARAM_SMOOTH_ALPHA;
      smoothedSirenRateBpm += (targetSirenRateBpm - smoothedSirenRateBpm) * PARAM_SMOOTH_ALPHA;
      smoothedCenterPitchHz += (targetCenterPitchHz - smoothedCenterPitchHz) * PARAM_SMOOTH_ALPHA;
      smoothedFilterMorph += (targetFilterMorph - smoothedFilterMorph) * PARAM_SMOOTH_ALPHA;
      smoothedReverbMix += (targetReverbMix - smoothedReverbMix) * PARAM_SMOOTH_ALPHA;
      smoothedDelayMix += (targetDelayMix - smoothedDelayMix) * PARAM_SMOOTH_ALPHA;

      float sweep = sinf(sirenPhase);
      float lfo = sinf(lfoPhase);

      float freq = smoothedCenterPitchHz + SIREN_SWEEP_HZ * sweep + lfo * PITCH_LFO_DEPTH_HZ;
      freq = clampf(freq, 60.0f, 2800.0f);

      // Portadora + ganancia.
      sample = evalSirenCarrier(carrierPhase, sirenWave);
      sample *= OUTPUT_GAIN_MAX * smoothedMasterGain;

      // Filtro y efectos temporales.
      float cutoff = 280.0f + fabsf(smoothedFilterMorph) * 6500.0f;
      sample = processFilter(sample, cutoff, smoothedFilterMorph);

      sample = processReverb(sample, smoothedReverbMix, 0.78f);

      sample = processDelay(sample, smoothedDelayMix, 180.0f, 0.32f);

      carrierPhase += TWO_PI * freq / SAMPLE_RATE;
      if (carrierPhase >= TWO_PI) {
        carrierPhase -= TWO_PI;
      }

      float sirenInc = TWO_PI * bpmToHz(smoothedSirenRateBpm) / SAMPLE_RATE;
      sirenPhase += sirenInc;
      if (sirenPhase >= TWO_PI) {
        sirenPhase -= TWO_PI;
      }

      lfoPhase += TWO_PI * PITCH_LFO_RATE_HZ / SAMPLE_RATE;
      if (lfoPhase >= TWO_PI) {
        lfoPhase -= TWO_PI;
      }

      modPhase += TWO_PI * MOD_LFO_RATE_HZ / SAMPLE_RATE;
      if (modPhase >= TWO_PI) {
        modPhase -= TWO_PI;
      }
    }

    // Convierte float [-1..1] a PCM de 16 bits con signo.
    sample = clampf(sample, -1.0f, 1.0f);
    int16_t s = static_cast<int16_t>(sample * 32767.0f);

    audioBuffer[i * 2] = s;
    audioBuffer[i * 2 + 1] = s;

  }

  // Envia un bloque de audio al DMA de I2S.
  size_t bytesWritten = 0;
  i2s_write(I2S_PORT, audioBuffer, sizeof(audioBuffer), &bytesWritten, portMAX_DELAY);

  if (!DIAG_DISABLE_OLED && !DIAG_DISABLE_OLED_REFRESH && displayNeedsUpdate(millis())) {
    drawDisplay();
  }
}
