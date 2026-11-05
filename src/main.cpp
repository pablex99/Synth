#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

// Prueba de audio: sirena dub estilo patch modular (LFO diente de sierra -> FM de VCO
// cuadrado -> VCA -> delay), sonando siempre. Controles: I4 = frecuencia del LFO,
// I3 = profundidad FM, I2 = frecuencia base del VCO, C0/S0 = tiempo de delay,
// C6/S6 = feedback de delay, I5 = nivel/volumen.
// Sin botones ni OLED. El motor prioriza caracter analogico y baja aspereza digital.

constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
constexpr int I2S_BCK_PIN = 13;
constexpr int I2S_WS_PIN = 27;
constexpr int I2S_DO_PIN = 14;

// Lineas de control/direccion del CD4067 y pin de entrada ADC.
constexpr int MUX_SIG_PIN = 35;
constexpr int MUX_S0_PIN = 16;
constexpr int MUX_S1_PIN = 17;
constexpr int MUX_S2_PIN = 19;
constexpr int MUX_S3_PIN = 18;
constexpr uint8_t MUX_CH_DELAY_TIME = 0;      // C0/S0
constexpr uint8_t MUX_CH_VCO_BASE = 2;        // I2/C2
constexpr uint8_t MUX_CH_FM_DEPTH = 3;        // I3/C3
constexpr uint8_t MUX_CH_LFO_RATE = 4;        // I4/C4
constexpr uint8_t MUX_CH_VCA_LEVEL = 5;       // I5/C5
constexpr uint8_t MUX_CH_DELAY_FEEDBACK = 6;  // C6/S6
constexpr int BTN_RED_PIN = 4;          // Rojo: cambia forma de onda del LFO
constexpr int BTN_WHITE_PIN = 21;       // Blanco: alterna modo AUTO/MANUAL
constexpr int BTN_BLUE_PIN = 22;        // Azul: compuerta manual (mantener)

constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 256;
constexpr uint32_t CONTROL_UPDATE_MS = 10;
constexpr uint32_t DEBOUNCE_MS = 40;

// Filtros de lectura/control para minimizar zipper noise.
constexpr float POT_SMOOTH_ALPHA = 0.08f;
constexpr float PARAM_SMOOTH_ALPHA = 0.0013f;
constexpr float POT_NORMALIZED_DEADBAND = 0.003f;

// Endurecimiento de lectura MUX/ADC para reducir ruido de conversion y arrastre.
constexpr uint16_t MUX_SETTLE_US = 300;
constexpr uint16_t MUX_INTER_SAMPLE_US = 40;
constexpr uint8_t MUX_DUMMY_READS = 2;
constexpr uint8_t MUX_AVG_SAMPLES = 4;

// Rangos ampliados de controles:
// I4: 0.05..50 Hz, I3: -100..300 %, I2: 50..4000 Hz.
constexpr float LFO_RATE_MIN_HZ = 0.05f;
constexpr float LFO_RATE_MAX_HZ = 50.0f;
constexpr float VCO_BASE_MIN_HZ = 50.0f;
constexpr float VCO_BASE_MAX_HZ = 4000.0f;
constexpr float FM_DEPTH_MIN_PERCENT = -100.0f;
constexpr float FM_DEPTH_MAX_PERCENT = 300.0f;
constexpr float VCA_LEVEL_MAX = 0.5f;

// Voz/timbre tipo dub analogo.
constexpr float OUTPUT_GAIN = 0.30f;
constexpr float DUTY_CYCLE = 0.45f;
constexpr float VCO_TONE_ALPHA = 0.18f;
constexpr float OUTPUT_DRIVE = 1.20f;

// Delay dub fijo.
constexpr float DELAY_TIME_MS = 7000.0f;
constexpr float DELAY_TIME_MIN_MS = 0.0f;
constexpr int DELAY_SAMPLE_RATE = 7000;
constexpr int DELAY_BUFFER_SAMPLES = static_cast<int>(DELAY_SAMPLE_RATE * DELAY_TIME_MS / 1000.0f);
constexpr float DELAY_FEEDBACK_MIN = 0.0f;
constexpr float DELAY_FEEDBACK_MAX = 1.0f;
constexpr float DELAY_MIX = 0.30f;
constexpr float DELAY_WET_TRIM = 0.80f;
constexpr float DELAY_FEEDBACK_DARKEN_ALPHA = 0.08f;

// Limpieza de salida.
constexpr float DC_BLOCK_R = 0.995f;
constexpr float OUTPUT_LP_ALPHA = 0.08f;
constexpr float FM_SLEW_ALPHA = 0.012f;
constexpr float GATE_SMOOTH_ALPHA = 0.01f;
constexpr float VCA_POT_MIN_NORM = 0.045f;
constexpr float VCA_POT_MAX_NORM = 0.995f;
constexpr float VCA_AUDIO_TAPER = 1.6f;

float carrierPhase = 0.0f;
float lfoPhase = 0.0f;
float vcoToneState = 0.0f;
float fmHzSmoothed = 440.0f;
float delayFeedbackFiltered = 0.0f;
float dcBlockX1 = 0.0f;
float dcBlockY1 = 0.0f;
float outputLpState = 0.0f;
int16_t delayBuffer[DELAY_BUFFER_SAMPLES] = {};
int delayIndex = 0;
int delayWritePhase = 0;

// Estado de controles (I2, I3, I4, I5): valor crudo suavizado y normalizado [0..1].
float muxRawSmoothDelayTime = 2048.0f;
float muxRawSmoothVcoBase = 2048.0f;
float muxRawSmoothFmDepth = 2048.0f;
float muxRawSmoothLfoRate = 2048.0f;
float muxRawSmoothVcaLevel = 2048.0f;
float muxRawSmoothDelayFeedback = 2048.0f;
float potDelayTimeNorm = 0.5f;
float potVcoBaseNorm = 0.5f;
float potFmDepthNorm = 0.5f;
float potLfoRateNorm = 0.5f;
float potVcaLevel = 0.5f;
float potDelayFeedbackNorm = 0.3f;
float smoothedDelayTimeNorm = 0.5f;
float smoothedVcoBaseNorm = 0.5f;
float smoothedFmDepthNorm = 0.5f;
float smoothedLfoRateNorm = 0.5f;
float smoothedVcaLevel = 0.5f;
float smoothedDelayFeedbackNorm = 0.3f;
uint32_t lastControlMs = 0;
uint8_t muxScanIndex = 0;
bool autoMode = true;
bool manualGateHeld = false;
bool manualGateHeldPrev = false;
float gateLevel = 1.0f;

enum class LfoWaveform : uint8_t {
  SawDown = 0,
  Sine = 1,
  SawUp = 2,
  Square = 3,
  Count = 4
};

LfoWaveform lfoWaveform = LfoWaveform::SawDown;
bool redLastReading = HIGH;
bool redStableState = HIGH;
uint32_t redLastChangeMs = 0;
bool whiteLastReading = HIGH;
bool whiteStableState = HIGH;
uint32_t whiteLastChangeMs = 0;
bool blueLastReading = HIGH;
bool blueStableState = HIGH;
uint32_t blueLastChangeMs = 0;

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
  return minValue + clampf(normalized, 0.0f, 1.0f) * (maxValue - minValue);
}

// Convierte la lectura normalizada del pot de volumen al rango [0..1] util real,
// compensando offset de hardware y aplicando una curva tipo audio-taper.
float mapVcaPotToGain(float potNorm) {
  float remapped = (potNorm - VCA_POT_MIN_NORM) / (VCA_POT_MAX_NORM - VCA_POT_MIN_NORM);
  remapped = clampf(remapped, 0.0f, 1.0f);
  return powf(remapped, VCA_AUDIO_TAPER);
}

float computeLfoValue(float phaseNorm, float phaseRad, LfoWaveform waveform) {
  switch (waveform) {
    case LfoWaveform::Sine:
      return sinf(phaseRad);
    case LfoWaveform::SawUp:
      return -1.0f + 2.0f * phaseNorm;
    case LfoWaveform::Square:
      return (phaseNorm < 0.5f) ? 1.0f : -1.0f;
    case LfoWaveform::SawDown:
    default:
      return 1.0f - 2.0f * phaseNorm;
  }
}

// PolyBLEP: reduce aliasing del pulso para un sonido menos digital.
float polyBlep(float phase, float dt) {
  if (phase < dt) {
    float t = phase / dt;
    return t + t - t * t - 1.0f;
  }
  if (phase > 1.0f - dt) {
    float t = (phase - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

void selectMuxChannel(uint8_t channel) {
  digitalWrite(MUX_S0_PIN, channel & 0x01);
  digitalWrite(MUX_S1_PIN, (channel >> 1) & 0x01);
  digitalWrite(MUX_S2_PIN, (channel >> 2) & 0x01);
  digitalWrite(MUX_S3_PIN, (channel >> 3) & 0x01);
}

int readMuxRaw(uint8_t channel) {
  selectMuxChannel(channel);
  delayMicroseconds(MUX_SETTLE_US);

  for (uint8_t i = 0; i < MUX_DUMMY_READS; ++i) {
    analogRead(MUX_SIG_PIN);
    delayMicroseconds(MUX_INTER_SAMPLE_US);
  }

  uint32_t sum = 0;
  for (uint8_t i = 0; i < MUX_AVG_SAMPLES; ++i) {
    sum += static_cast<uint32_t>(analogRead(MUX_SIG_PIN));
    if (i + 1 < MUX_AVG_SAMPLES) {
      delayMicroseconds(MUX_INTER_SAMPLE_US);
    }
  }
  return static_cast<int>(sum / MUX_AVG_SAMPLES);
}

float readSmoothedNormalized(uint8_t channel, float& rawSmooth) {
  float previousNormalized = clampf(rawSmooth / 4095.0f, 0.0f, 1.0f);
  rawSmooth += POT_SMOOTH_ALPHA * (readMuxRaw(channel) - rawSmooth);
  float normalized = clampf(rawSmooth / 4095.0f, 0.0f, 1.0f);
  if (fabsf(normalized - previousNormalized) < POT_NORMALIZED_DEADBAND) {
    normalized = previousNormalized;
    rawSmooth = normalized * 4095.0f;
  }
  return normalized;
}

void setupI2S() {
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = SAMPLE_RATE;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = 16;
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

void setup() {
  Serial.begin(115200);
  delay(150);

  analogReadResolution(12);
  analogSetPinAttenuation(MUX_SIG_PIN, ADC_11db);
  pinMode(MUX_S0_PIN, OUTPUT);
  pinMode(MUX_S1_PIN, OUTPUT);
  pinMode(MUX_S2_PIN, OUTPUT);
  pinMode(MUX_S3_PIN, OUTPUT);
  pinMode(BTN_RED_PIN, INPUT_PULLUP);
  pinMode(BTN_WHITE_PIN, INPUT_PULLUP);
  pinMode(BTN_BLUE_PIN, INPUT_PULLUP);

  muxRawSmoothDelayTime = static_cast<float>(readMuxRaw(MUX_CH_DELAY_TIME));
  muxRawSmoothVcoBase = static_cast<float>(readMuxRaw(MUX_CH_VCO_BASE));
  muxRawSmoothFmDepth = static_cast<float>(readMuxRaw(MUX_CH_FM_DEPTH));
  muxRawSmoothLfoRate = static_cast<float>(readMuxRaw(MUX_CH_LFO_RATE));
  muxRawSmoothVcaLevel = static_cast<float>(readMuxRaw(MUX_CH_VCA_LEVEL));
  muxRawSmoothDelayFeedback = static_cast<float>(readMuxRaw(MUX_CH_DELAY_FEEDBACK));

  potDelayTimeNorm = clampf(muxRawSmoothDelayTime / 4095.0f, 0.0f, 1.0f);
  potVcoBaseNorm = clampf(muxRawSmoothVcoBase / 4095.0f, 0.0f, 1.0f);
  potFmDepthNorm = clampf(muxRawSmoothFmDepth / 4095.0f, 0.0f, 1.0f);
  potLfoRateNorm = clampf(muxRawSmoothLfoRate / 4095.0f, 0.0f, 1.0f);
  potVcaLevel = clampf(muxRawSmoothVcaLevel / 4095.0f, 0.0f, 1.0f);
  potDelayFeedbackNorm = clampf(muxRawSmoothDelayFeedback / 4095.0f, 0.0f, 1.0f);

  smoothedDelayTimeNorm = potDelayTimeNorm;
  smoothedVcoBaseNorm = potVcoBaseNorm;
  smoothedFmDepthNorm = potFmDepthNorm;
  smoothedLfoRateNorm = potLfoRateNorm;
  smoothedVcaLevel = potVcaLevel;
  smoothedDelayFeedbackNorm = potDelayFeedbackNorm;
  fmHzSmoothed = mapLinear(smoothedVcoBaseNorm, VCO_BASE_MIN_HZ, VCO_BASE_MAX_HZ);

  setupI2S();
  Serial.println("Synth_tone_test: VCV FM siren (I4=LFO Hz, I3=FM %, I2=VCO Hz, C0/S0=delay time, C6/S6=delay feedback, I5=volume, red=waveform, white=auto/manual, blue=manual gate)");
}

void loop() {
  int16_t audioBuffer[BUFFER_SIZE * 2];
  static const float outputDriveNorm = tanhf(OUTPUT_DRIVE);

  uint32_t now = millis();

  bool redReading = digitalRead(BTN_RED_PIN);
  if (redReading != redLastReading) {
    redLastReading = redReading;
    redLastChangeMs = now;
  }
  if ((now - redLastChangeMs) > DEBOUNCE_MS && redReading != redStableState) {
    redStableState = redReading;
    if (redStableState == LOW) {
      lfoWaveform = static_cast<LfoWaveform>((static_cast<uint8_t>(lfoWaveform) + 1) % static_cast<uint8_t>(LfoWaveform::Count));
    }
  }

  bool whiteReading = digitalRead(BTN_WHITE_PIN);
  if (whiteReading != whiteLastReading) {
    whiteLastReading = whiteReading;
    whiteLastChangeMs = now;
  }
  if ((now - whiteLastChangeMs) > DEBOUNCE_MS && whiteReading != whiteStableState) {
    whiteStableState = whiteReading;
    if (whiteStableState == LOW) {
      autoMode = !autoMode;
    }
  }

  bool blueReading = digitalRead(BTN_BLUE_PIN);
  if (blueReading != blueLastReading) {
    blueLastReading = blueReading;
    blueLastChangeMs = now;
  }
  if ((now - blueLastChangeMs) > DEBOUNCE_MS && blueReading != blueStableState) {
    blueStableState = blueReading;
  }
  manualGateHeld = (blueStableState == LOW);

  // En modo manual, al presionar el trigger reiniciamos fase para que cada disparo
  // arranque desde el punto inicial predecible de cada forma de onda.
  bool manualTriggerPressed = (!autoMode && manualGateHeld && !manualGateHeldPrev);
  if (manualTriggerPressed) {
    lfoPhase = 0.0f;
    carrierPhase = 0.0f;
    vcoToneState = 0.0f;
    outputLpState = 0.0f;
    dcBlockX1 = 0.0f;
    dcBlockY1 = 0.0f;
  }
  manualGateHeldPrev = manualGateHeld;

  if (now - lastControlMs >= CONTROL_UPDATE_MS) {
    // Round-robin: 1 canal por tick para reducir actividad ADC instantanea y ruido.
    switch (muxScanIndex) {
      case 0:
        potDelayTimeNorm = readSmoothedNormalized(MUX_CH_DELAY_TIME, muxRawSmoothDelayTime);
        break;
      case 1:
        potVcoBaseNorm = readSmoothedNormalized(MUX_CH_VCO_BASE, muxRawSmoothVcoBase);
        break;
      case 2:
        potFmDepthNorm = readSmoothedNormalized(MUX_CH_FM_DEPTH, muxRawSmoothFmDepth);
        break;
      case 3:
        potLfoRateNorm = readSmoothedNormalized(MUX_CH_LFO_RATE, muxRawSmoothLfoRate);
        break;
      case 4:
        potVcaLevel = readSmoothedNormalized(MUX_CH_VCA_LEVEL, muxRawSmoothVcaLevel);
        break;
      case 5:
      default:
        potDelayFeedbackNorm = readSmoothedNormalized(MUX_CH_DELAY_FEEDBACK, muxRawSmoothDelayFeedback);
        break;
    }
    muxScanIndex = (muxScanIndex + 1) % 6;
    lastControlMs = now;
  }

  float targetDelayTimeNorm = potDelayTimeNorm;
  float targetVcoBaseNorm = potVcoBaseNorm;
  float targetFmDepthNorm = potFmDepthNorm;
  float targetLfoRateNorm = potLfoRateNorm;
  float targetVcaLevel = mapVcaPotToGain(potVcaLevel) * VCA_LEVEL_MAX;
  float targetDelayFeedbackNorm = potDelayFeedbackNorm;
  float targetGateLevel = (autoMode || manualGateHeld) ? 1.0f : 0.0f;

  for (int i = 0; i < BUFFER_SIZE; ++i) {
    smoothedDelayTimeNorm += (targetDelayTimeNorm - smoothedDelayTimeNorm) * PARAM_SMOOTH_ALPHA;
    smoothedVcoBaseNorm += (targetVcoBaseNorm - smoothedVcoBaseNorm) * PARAM_SMOOTH_ALPHA;
    smoothedFmDepthNorm += (targetFmDepthNorm - smoothedFmDepthNorm) * PARAM_SMOOTH_ALPHA;
    smoothedLfoRateNorm += (targetLfoRateNorm - smoothedLfoRateNorm) * PARAM_SMOOTH_ALPHA;
    smoothedVcaLevel += (targetVcaLevel - smoothedVcaLevel) * PARAM_SMOOTH_ALPHA;
    smoothedDelayFeedbackNorm += (targetDelayFeedbackNorm - smoothedDelayFeedbackNorm) * PARAM_SMOOTH_ALPHA;
    gateLevel += (targetGateLevel - gateLevel) * GATE_SMOOTH_ALPHA;

    float currentVcoBaseHz = mapLinear(smoothedVcoBaseNorm, VCO_BASE_MIN_HZ, VCO_BASE_MAX_HZ);
    float currentLfoRateHz = mapLinear(smoothedLfoRateNorm, LFO_RATE_MIN_HZ, LFO_RATE_MAX_HZ);
    float currentFmDepthPercent = mapLinear(smoothedFmDepthNorm, FM_DEPTH_MIN_PERCENT, FM_DEPTH_MAX_PERCENT) * 0.01f;

    // LFO configurable (sierra descendente, seno o sierra ascendente) hacia FM del VCO.
    float lfoPhaseNorm = lfoPhase / TWO_PI;
    float lfoValue = computeLfoValue(lfoPhaseNorm, lfoPhase, lfoWaveform);
    float maxFmDepthHz = fminf(currentVcoBaseHz - VCO_BASE_MIN_HZ, VCO_BASE_MAX_HZ - currentVcoBaseHz);
    maxFmDepthHz = clampf(maxFmDepthHz, 0.0f, VCO_BASE_MAX_HZ - VCO_BASE_MIN_HZ);
    float currentFmDepthHz = maxFmDepthHz * currentFmDepthPercent;
    float frequencyTarget = clampf(currentVcoBaseHz + lfoValue * currentFmDepthHz, VCO_BASE_MIN_HZ, VCO_BASE_MAX_HZ);
    fmHzSmoothed += (frequencyTarget - fmHzSmoothed) * FM_SLEW_ALPHA;
    float frequency = clampf(fmHzSmoothed, VCO_BASE_MIN_HZ, VCO_BASE_MAX_HZ);

    // Pulso con PolyBLEP + rolloff suave para un tono menos digital.
    float dutyPhase = carrierPhase / TWO_PI;
    float dt = frequency / SAMPLE_RATE;
    float rawSquare = (dutyPhase < DUTY_CYCLE) ? 1.0f : -1.0f;
    rawSquare += polyBlep(dutyPhase, dt);
    float fallPhase = dutyPhase - DUTY_CYCLE;
    if (fallPhase < 0.0f) {
      fallPhase += 1.0f;
    }
    rawSquare -= polyBlep(fallPhase, dt);
    vcoToneState += (rawSquare - vcoToneState) * VCO_TONE_ALPHA;
    float drySample = vcoToneState * OUTPUT_GAIN * gateLevel;

    // Delay dub con tiempo variable por C0/S0 (0..7000 ms) y feedback por C6/S6.
    float currentDelayMs = DELAY_TIME_MIN_MS + smoothedDelayTimeNorm * (DELAY_TIME_MS - DELAY_TIME_MIN_MS);
    float currentDelayFeedback = mapLinear(smoothedDelayFeedbackNorm, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX);
    int currentDelaySamples = static_cast<int>(clampf(currentDelayMs * DELAY_SAMPLE_RATE / 1000.0f, 0.0f, static_cast<float>(DELAY_BUFFER_SAMPLES - 1)));
    float delayedSample = 0.0f;
    if (currentDelaySamples > 0) {
      int readIndex = delayIndex - currentDelaySamples;
      if (readIndex < 0) {
        readIndex += DELAY_BUFFER_SAMPLES;
      }
      delayedSample = static_cast<float>(delayBuffer[readIndex]) / 32767.0f;
    }
    float wetSample = delayedSample * DELAY_WET_TRIM;
    float sample = drySample * (1.0f - DELAY_MIX) + wetSample * DELAY_MIX;
    float feedbackInput = drySample + delayedSample * currentDelayFeedback;
    delayFeedbackFiltered += (feedbackInput - delayFeedbackFiltered) * DELAY_FEEDBACK_DARKEN_ALPHA;

    // Escritura del delay a tasa reducida para habilitar 5 s de retardo dentro de la RAM disponible.
    delayWritePhase += DELAY_SAMPLE_RATE;
    if (delayWritePhase >= SAMPLE_RATE) {
      delayWritePhase -= SAMPLE_RATE;
      delayBuffer[delayIndex] = static_cast<int16_t>(clampf(delayFeedbackFiltered, -1.0f, 1.0f) * 32767.0f);
      delayIndex = (delayIndex + 1) % DELAY_BUFFER_SAMPLES;
    }

    // Saturacion suave + limpieza final.
    sample = tanhf(sample * OUTPUT_DRIVE) / outputDriveNorm;
    float dcBlocked = sample - dcBlockX1 + DC_BLOCK_R * dcBlockY1;
    dcBlockX1 = sample;
    dcBlockY1 = dcBlocked;
    outputLpState += (dcBlocked - outputLpState) * OUTPUT_LP_ALPHA;
    sample = outputLpState * smoothedVcaLevel;
    sample = clampf(sample, -1.0f, 1.0f);

    int16_t pcm = static_cast<int16_t>(sample * 32767.0f);
    audioBuffer[i * 2] = pcm;
    audioBuffer[i * 2 + 1] = pcm;

    carrierPhase += TWO_PI * frequency / SAMPLE_RATE;
    if (carrierPhase >= TWO_PI) {
      carrierPhase -= TWO_PI;
    }

    lfoPhase += TWO_PI * currentLfoRateHz / SAMPLE_RATE;
    if (lfoPhase >= TWO_PI) {
      lfoPhase -= TWO_PI;
    }
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_PORT, audioBuffer, sizeof(audioBuffer), &bytesWritten, portMAX_DELAY);
}
