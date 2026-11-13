#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <SD.h>
#include <SPI.h>

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
constexpr uint8_t MUX_CH_ENV_RELEASE = 1;     // I1/C1
constexpr uint8_t MUX_CH_VCO_BASE = 2;        // I2/C2
constexpr uint8_t MUX_CH_FM_DEPTH = 3;        // I3/C3
constexpr uint8_t MUX_CH_LFO_RATE = 4;        // I4/C4
constexpr uint8_t MUX_CH_VCA_LEVEL = 5;       // I5/C5
constexpr uint8_t MUX_CH_DELAY_FEEDBACK = 6;  // C6/S6
constexpr int BTN_RED_PIN = 4;          // Rojo: cambia forma de onda del LFO
constexpr int BTN_WHITE_PIN = 21;       // Blanco: alterna modo AUTO/MANUAL
constexpr int BTN_BLUE_PIN = 22;        // Azul: compuerta manual (mantener)
constexpr int BTN_GREEN_PIN = 23;       // Verde: dispara reproduccion WAV desde SD

constexpr int SD_CS_PIN = 33;
constexpr int SD_SCK_PIN = 25;
constexpr int SD_MOSI_PIN = 26;
constexpr int SD_MISO_PIN = 34;
constexpr uint32_t SD_SPI_HZ = 4000000;

constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 512;
constexpr uint32_t CONTROL_UPDATE_MS = 20;
constexpr uint32_t DEBOUNCE_MS = 60;
constexpr uint32_t BLUE_LONG_PRESS_MS = 2000;
constexpr uint16_t SAMPLE_SD_BLOCK_BYTES = 1536;
constexpr uint16_t SAMPLE_RING_CAPACITY = 1024;
constexpr uint16_t SAMPLE_RING_LOW_WATER = 512;
constexpr uint16_t SAMPLE_PREROLL_SAMPLES = 256;
constexpr float SAMPLE_MIX_GAIN = 0.5f;
constexpr float SAMPLE_FADE_MS = 10.0f;
constexpr float SAMPLE_FADE_STEP = 1.0f / ((SAMPLE_FADE_MS * SAMPLE_RATE / 1000.0f) > 1.0f ? (SAMPLE_FADE_MS * SAMPLE_RATE / 1000.0f) : 1.0f);
constexpr float SAMPLE_OUTPUT_SMOOTH_ALPHA = 0.65f;
constexpr float SAMPLE_UNDERRUN_DECAY = 0.98f;
constexpr float SAMPLE_OUTPUT_SLEW_MAX = 0.08f;
constexpr float SAMPLE_DC_BLOCK_R = 0.995f;
constexpr float SAMPLE_POST_LP_ALPHA = 1.0f;
constexpr uint32_t BUTTON_CONTROL_SUPPRESS_MS = 140;

// Filtros de lectura/control para minimizar zipper noise.
constexpr float POT_SMOOTH_ALPHA = 0.08f;
constexpr float PARAM_SMOOTH_ALPHA = 0.0013f;
constexpr float POT_NORMALIZED_DEADBAND = 0.007f;

// Endurecimiento de lectura MUX/ADC para reducir ruido de conversion y arrastre.
constexpr uint16_t MUX_SETTLE_US = 300;
constexpr uint16_t MUX_INTER_SAMPLE_US = 40;
constexpr uint8_t MUX_DUMMY_READS = 2;
constexpr uint8_t MUX_AVG_SAMPLES = 4;

// Rangos ampliados de controles:
// I4: 0.0001..50 Hz, I3: -100..300 %, I2: 50..4000 Hz.
constexpr float LFO_RATE_MIN_HZ = 0.0001f;
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
constexpr float DELAY_MIX = 0.45f;
constexpr float DELAY_WET_TRIM = 1.0f;
constexpr float DELAY_FEEDBACK_DARKEN_ALPHA = 0.05f;

// Envelope para trigger manual.
constexpr float ENV_ATTACK_MS = 1.0f;
constexpr float ENV_DECAY_MS = 10000.0f;
constexpr float ENV_SUSTAIN_LEVEL = 1.0f;
constexpr float ENV_SUSTAIN_MS = 10000.0f;
constexpr float ENV_RELEASE_MIN_MS = 1.0f;
constexpr float ENV_RELEASE_MAX_MS = 10000.0f;

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
float muxRawSmoothEnvRelease = 2048.0f;
float muxRawSmoothVcoBase = 2048.0f;
float muxRawSmoothFmDepth = 2048.0f;
float muxRawSmoothLfoRate = 2048.0f;
float muxRawSmoothVcaLevel = 2048.0f;
float muxRawSmoothDelayFeedback = 2048.0f;
float potDelayTimeNorm = 0.5f;
float potEnvReleaseNorm = 0.5f;
float potVcoBaseNorm = 0.5f;
float potFmDepthNorm = 0.5f;
float potLfoRateNorm = 0.5f;
float potVcaLevel = 0.5f;
float potDelayFeedbackNorm = 0.3f;
float smoothedDelayTimeNorm = 0.5f;
float smoothedEnvReleaseNorm = 0.5f;
float smoothedVcoBaseNorm = 0.5f;
float smoothedFmDepthNorm = 0.5f;
float smoothedLfoRateNorm = 0.5f;
float smoothedVcaLevel = 0.5f;
float smoothedDelayFeedbackNorm = 0.3f;
uint32_t lastControlMs = 0;
uint8_t muxScanIndex = 0;
bool autoMode = true;
bool autoModePrev = true;
bool manualGateHeld = false;
bool manualGateHeldPrev = false;
float gateLevel = 1.0f;

enum class EnvelopeStage : uint8_t {
  Idle = 0,
  Attack = 1,
  Decay = 2,
  Sustain = 3,
  Release = 4
};

EnvelopeStage envelopeStage = EnvelopeStage::Idle;
float envelopeLevel = 0.0f;
uint32_t envelopeSustainSamples = 0;

enum class LfoWaveform : uint8_t {
  SawDown = 0,
  Sine = 1,
  SawUp = 2,
  Square = 3,
  Count = 4
};

enum class LfoStartPoint : uint8_t {
  ZeroRising = 0,
  Peak = 1,
  ZeroFalling = 2,
  Trough = 3,
  Count = 4
};

LfoWaveform lfoWaveform = LfoWaveform::SawDown;
LfoStartPoint lfoStartPoint = LfoStartPoint::ZeroRising;
bool redLastReading = HIGH;
bool redStableState = HIGH;
uint32_t redLastChangeMs = 0;
bool whiteLastReading = HIGH;
bool whiteStableState = HIGH;
uint32_t whiteLastChangeMs = 0;
bool blueLastReading = HIGH;
bool blueStableState = HIGH;
uint32_t blueLastChangeMs = 0;
bool greenLastReading = HIGH;
bool greenStableState = HIGH;
uint32_t greenLastChangeMs = 0;
uint32_t bluePressedStartMs = 0;
bool blueLongPressLatched = false;

bool sdInitialized = false;
File samplePlaybackFile;
bool samplePlaybackActive = false;
uint8_t samplePlaybackChannels = 0;
uint16_t samplePlaybackBits = 0;
uint32_t samplePlaybackRateHz = 0;
uint32_t samplePlaybackDataBytesRemaining = 0;
String samplePlaybackPath;
String samplePreferredPath;
bool sampleMetaReady = false;
uint8_t sampleMetaChannels = 0;
uint16_t sampleMetaBits = 0;
uint32_t sampleMetaRateHz = 0;
uint32_t sampleMetaDataOffset = 0;
uint32_t sampleMetaDataBytes = 0;
uint8_t samplePlaybackReadBuffer[SAMPLE_SD_BLOCK_BYTES] = {};
uint16_t samplePlaybackReadPos = 0;
uint16_t samplePlaybackReadValid = 0;
volatile uint32_t sampleRingReadIndex = 0;
volatile uint32_t sampleRingWriteIndex = 0;
int16_t sampleRingBuffer[SAMPLE_RING_CAPACITY] = {};
volatile bool samplePlaybackFeeding = false;
float samplePlaybackFadeGain = 0.0f;
float samplePlaybackLastValue = 0.0f;
float sampleDcX1 = 0.0f;
float sampleDcY1 = 0.0f;
float samplePostLp = 0.0f;
TaskHandle_t sampleStreamTaskHandle = nullptr;
volatile bool sampleStartRequest = false;
volatile bool sampleOutputEnabled = false;
uint32_t suppressControlUntilMs = 0;

bool initSdCard();
String findFirstWavPath();

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

float getLfoStartPhaseNorm(LfoWaveform waveform, LfoStartPoint startPoint) {
  constexpr float kNearOne = 0.9999f;
  switch (waveform) {
    case LfoWaveform::Sine:
      switch (startPoint) {
        case LfoStartPoint::ZeroRising:
          return 0.0f;
        case LfoStartPoint::Peak:
          return 0.25f;
        case LfoStartPoint::ZeroFalling:
          return 0.5f;
        case LfoStartPoint::Trough:
        default:
          return 0.75f;
      }
    case LfoWaveform::SawUp:
      switch (startPoint) {
        case LfoStartPoint::ZeroRising:
          return 0.5f;
        case LfoStartPoint::Peak:
          return kNearOne;
        case LfoStartPoint::ZeroFalling:
          return 0.5f;
        case LfoStartPoint::Trough:
        default:
          return 0.0f;
      }
    case LfoWaveform::Square:
      switch (startPoint) {
        case LfoStartPoint::ZeroRising:
          return 0.0f;
        case LfoStartPoint::Peak:
          return 0.25f;
        case LfoStartPoint::ZeroFalling:
          return 0.5f;
        case LfoStartPoint::Trough:
        default:
          return 0.75f;
      }
    case LfoWaveform::SawDown:
    default:
      switch (startPoint) {
        case LfoStartPoint::ZeroRising:
          return 0.5f;
        case LfoStartPoint::Peak:
          return 0.0f;
        case LfoStartPoint::ZeroFalling:
          return 0.5f;
        case LfoStartPoint::Trough:
        default:
          return kNearOne;
      }
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

uint32_t readLE32(File &f) {
  uint8_t b[4] = {0, 0, 0, 0};
  if (f.read(b, 4) != 4) {
    return 0;
  }
  return static_cast<uint32_t>(b[0]) |
         (static_cast<uint32_t>(b[1]) << 8) |
         (static_cast<uint32_t>(b[2]) << 16) |
         (static_cast<uint32_t>(b[3]) << 24);
}

uint16_t readLE16(File &f) {
  uint8_t b[2] = {0, 0};
  if (f.read(b, 2) != 2) {
    return 0;
  }
  return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
}

bool parseWavHeader(File &f, uint8_t &channels, uint32_t &rateHz, uint16_t &bitsPerSample, uint32_t &dataBytes) {
  channels = 0;
  rateHz = 0;
  bitsPerSample = 0;
  dataBytes = 0;

  char riff[4];
  if (f.read(reinterpret_cast<uint8_t *>(riff), 4) != 4 || strncmp(riff, "RIFF", 4) != 0) {
    return false;
  }

  (void)readLE32(f);

  char wave[4];
  if (f.read(reinterpret_cast<uint8_t *>(wave), 4) != 4 || strncmp(wave, "WAVE", 4) != 0) {
    return false;
  }

  bool gotFmt = false;
  bool gotData = false;
  uint16_t audioFormat = 0;

  while (f.available()) {
    char chunkId[4];
    if (f.read(reinterpret_cast<uint8_t *>(chunkId), 4) != 4) {
      break;
    }

    uint32_t chunkSize = readLE32(f);
    uint32_t dataStart = f.position();

    if (strncmp(chunkId, "fmt ", 4) == 0) {
      if (chunkSize < 16) {
        return false;
      }
      audioFormat = readLE16(f);
      channels = static_cast<uint8_t>(readLE16(f));
      rateHz = readLE32(f);
      (void)readLE32(f);
      (void)readLE16(f);
      bitsPerSample = readLE16(f);
      gotFmt = true;
    } else if (strncmp(chunkId, "data", 4) == 0) {
      dataBytes = chunkSize;
      gotData = true;
      break;
    }

    uint32_t nextPos = dataStart + chunkSize + (chunkSize & 0x01);
    f.seek(nextPos);
  }

  bool pcmClassic = (audioFormat == 1);
  bool pcmExtensible = (audioFormat == 0xFFFE);

  return gotFmt && gotData && (pcmClassic || pcmExtensible) &&
         ((channels == 1) || (channels == 2)) && (bitsPerSample == 16);
}

bool prepareSampleMetadata() {
  if (!sdInitialized && !initSdCard()) {
    return false;
  }
  sdInitialized = true;

  String selectedPath = samplePreferredPath;
  if (selectedPath.length() == 0 || !SD.exists(selectedPath)) {
    selectedPath = findFirstWavPath();
    samplePreferredPath = selectedPath;
  }
  if (selectedPath.length() == 0) {
    sampleMetaReady = false;
    return false;
  }

  File f = SD.open(selectedPath, FILE_READ);
  if (!f) {
    sampleMetaReady = false;
    return false;
  }

  uint8_t channels = 0;
  uint32_t rateHz = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataBytes = 0;
  bool valid = parseWavHeader(f, channels, rateHz, bitsPerSample, dataBytes);
  uint32_t dataOffset = static_cast<uint32_t>(f.position());
  f.close();

  if (!valid || bitsPerSample != 16 || (channels != 1 && channels != 2) || rateHz != static_cast<uint32_t>(SAMPLE_RATE)) {
    sampleMetaReady = false;
    return false;
  }

  samplePreferredPath = selectedPath;
  sampleMetaChannels = channels;
  sampleMetaBits = bitsPerSample;
  sampleMetaRateHz = rateHz;
  sampleMetaDataOffset = dataOffset;
  sampleMetaDataBytes = dataBytes;
  sampleMetaReady = true;
  return true;
}

bool hasWavExtension(const String &name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".wav");
}

String findFirstWavPath() {
  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
    return "";
  }

  String selectedPath;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (hasWavExtension(name)) {
        selectedPath = name.startsWith("/") ? name : ("/" + name);
        entry.close();
        break;
      }
    }
    entry.close();
  }
  root.close();
  return selectedPath;
}

bool initSdCard() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) {
    Serial.println("SD init failed");
    return false;
  }
  return true;
}

void stopSamplePlayback() {
  if (samplePlaybackFile) {
    samplePlaybackFile.close();
  }
  samplePlaybackActive = false;
  samplePlaybackFeeding = false;
  samplePlaybackChannels = 0;
  samplePlaybackBits = 0;
  samplePlaybackRateHz = 0;
  samplePlaybackDataBytesRemaining = 0;
  samplePlaybackPath = "";
  samplePlaybackReadPos = 0;
  samplePlaybackReadValid = 0;
  sampleRingReadIndex = 0;
  sampleRingWriteIndex = 0;
  samplePlaybackFadeGain = 0.0f;
  samplePlaybackLastValue = 0.0f;
  sampleDcX1 = 0.0f;
  sampleDcY1 = 0.0f;
  samplePostLp = 0.0f;
  sampleOutputEnabled = false;
}

uint32_t sampleRingAvailable() {
  uint32_t write = sampleRingWriteIndex;
  uint32_t read = sampleRingReadIndex;
  return write - read;
}

uint32_t sampleRingFree() {
  return static_cast<uint32_t>(SAMPLE_RING_CAPACITY) - sampleRingAvailable();
}

bool sampleRingPush(int16_t value) {
  if (sampleRingFree() == 0) {
    return false;
  }
  uint32_t write = sampleRingWriteIndex;
  sampleRingBuffer[write & (SAMPLE_RING_CAPACITY - 1)] = value;
  sampleRingWriteIndex = write + 1;
  return true;
}

bool sampleRingPop(int16_t &value) {
  if (sampleRingAvailable() == 0) {
    return false;
  }
  uint32_t read = sampleRingReadIndex;
  value = sampleRingBuffer[read & (SAMPLE_RING_CAPACITY - 1)];
  sampleRingReadIndex = read + 1;
  return true;
}

bool refillSamplePlaybackBuffer() {
  if (!samplePlaybackActive || !samplePlaybackFile || samplePlaybackDataBytesRemaining == 0) {
    samplePlaybackReadPos = 0;
    samplePlaybackReadValid = 0;
    return false;
  }

  uint32_t pending = samplePlaybackDataBytesRemaining;
  size_t bytesToRead = static_cast<size_t>((pending > SAMPLE_SD_BLOCK_BYTES) ? SAMPLE_SD_BLOCK_BYTES : pending);
  size_t bytesRead = samplePlaybackFile.read(samplePlaybackReadBuffer, bytesToRead);
  samplePlaybackReadPos = 0;
  samplePlaybackReadValid = static_cast<uint16_t>(bytesRead);

  if (bytesRead == 0) {
    stopSamplePlayback();
    return false;
  }
  return true;
}

bool readSamplePlaybackBytes(uint8_t *dst, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    if (samplePlaybackReadPos >= samplePlaybackReadValid) {
      if (!refillSamplePlaybackBuffer()) {
        return false;
      }
    }
    dst[i] = samplePlaybackReadBuffer[samplePlaybackReadPos++];
  }
  return true;
}

bool startSamplePlaybackFromSd() {
  if (!sdInitialized && !initSdCard()) {
    return false;
  }
  sdInitialized = true;

  if (samplePlaybackActive) {
    return true;
  }

  if (!sampleMetaReady) {
    if (!prepareSampleMetadata()) {
      return false;
    }
  }

  if (samplePreferredPath.length() == 0 || !SD.exists(samplePreferredPath)) {
    sampleMetaReady = false;
    if (!prepareSampleMetadata()) {
      return false;
    }
  }

  stopSamplePlayback();

  File f = SD.open(samplePreferredPath, FILE_READ);
  if (!f) {
    sampleMetaReady = false;
    return false;
  }

  if (!f.seek(sampleMetaDataOffset)) {
    f.close();
    sampleMetaReady = false;
    return false;
  }

  samplePlaybackFile = f;
  samplePlaybackChannels = sampleMetaChannels;
  samplePlaybackBits = sampleMetaBits;
  samplePlaybackRateHz = sampleMetaRateHz;
  samplePlaybackDataBytesRemaining = sampleMetaDataBytes;
  samplePlaybackPath = samplePreferredPath;
  samplePlaybackReadPos = 0;
  samplePlaybackReadValid = 0;
  sampleRingReadIndex = 0;
  sampleRingWriteIndex = 0;
  samplePlaybackFadeGain = 0.0f;
  samplePlaybackLastValue = 0.0f;
  sampleDcX1 = 0.0f;
  sampleDcY1 = 0.0f;
  samplePostLp = 0.0f;
  sampleOutputEnabled = false;
  samplePlaybackActive = true;
  samplePlaybackFeeding = true;
  return true;
}

bool decodeNextSampleFromFile(int16_t &monoPcm) {
  if (!samplePlaybackFeeding || !samplePlaybackFile) {
    return false;
  }
  if (samplePlaybackDataBytesRemaining == 0) {
    samplePlaybackFeeding = false;
    if (samplePlaybackFile) {
      samplePlaybackFile.close();
    }
    return false;
  }

  if (samplePlaybackChannels == 1) {
    if (samplePlaybackDataBytesRemaining < 2) {
      samplePlaybackFeeding = false;
      if (samplePlaybackFile) {
        samplePlaybackFile.close();
      }
      return false;
    }
    uint8_t monoBytes[2] = {0, 0};
    if (!readSamplePlaybackBytes(monoBytes, 2)) {
      samplePlaybackFeeding = false;
      if (samplePlaybackFile) {
        samplePlaybackFile.close();
      }
      return false;
    }
    monoPcm = static_cast<int16_t>(static_cast<uint16_t>(monoBytes[0]) |
                                   (static_cast<uint16_t>(monoBytes[1]) << 8));
    samplePlaybackDataBytesRemaining -= 2;
  } else {
    if (samplePlaybackDataBytesRemaining < 4) {
      samplePlaybackFeeding = false;
      if (samplePlaybackFile) {
        samplePlaybackFile.close();
      }
      return false;
    }
    uint8_t stereoBytes[4] = {0, 0, 0, 0};
    if (!readSamplePlaybackBytes(stereoBytes, 4)) {
      samplePlaybackFeeding = false;
      if (samplePlaybackFile) {
        samplePlaybackFile.close();
      }
      return false;
    }
    int16_t left = static_cast<int16_t>(static_cast<uint16_t>(stereoBytes[0]) |
                                        (static_cast<uint16_t>(stereoBytes[1]) << 8));
    int16_t right = static_cast<int16_t>(static_cast<uint16_t>(stereoBytes[2]) |
                                         (static_cast<uint16_t>(stereoBytes[3]) << 8));
    samplePlaybackDataBytesRemaining -= 4;
    monoPcm = static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) / 2);
  }

  if (samplePlaybackDataBytesRemaining == 0) {
    samplePlaybackFeeding = false;
    if (samplePlaybackFile) {
      samplePlaybackFile.close();
    }
  }
  return true;
}

void sampleStreamTask(void *param) {
  (void)param;
  while (true) {
    if (sampleStartRequest) {
      sampleStartRequest = false;
      (void)startSamplePlaybackFromSd();
    }

    if (!samplePlaybackActive || !samplePlaybackFeeding) {
      vTaskDelay(pdMS_TO_TICKS(8));
      continue;
    }

    if (sampleRingFree() < SAMPLE_RING_LOW_WATER) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    while (sampleRingFree() > 0 && samplePlaybackFeeding) {
      int16_t sampleMono = 0;
      if (!decodeNextSampleFromFile(sampleMono)) {
        break;
      }
      if (!sampleRingPush(sampleMono)) {
        break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

float getNextSamplePlaybackValue() {
  if (!samplePlaybackActive) {
    samplePlaybackLastValue = 0.0f;
    sampleDcX1 = 0.0f;
    sampleDcY1 = 0.0f;
    samplePostLp = 0.0f;
    return 0.0f;
  }

  if (!sampleOutputEnabled) {
    if (sampleRingAvailable() >= SAMPLE_PREROLL_SAMPLES || !samplePlaybackFeeding) {
      sampleOutputEnabled = true;
    } else {
      return 0.0f;
    }
  }

  int16_t monoPcm = 0;
  bool hasData = sampleRingPop(monoPcm);

  if (hasData) {
    samplePlaybackFadeGain = clampf(samplePlaybackFadeGain + SAMPLE_FADE_STEP, 0.0f, 1.0f);
    float target = (static_cast<float>(monoPcm) / 32767.0f) * samplePlaybackFadeGain;
    float delta = target - samplePlaybackLastValue;
    delta = clampf(delta, -SAMPLE_OUTPUT_SLEW_MAX, SAMPLE_OUTPUT_SLEW_MAX);
    float slewed = samplePlaybackLastValue + delta;
    samplePlaybackLastValue += (slewed - samplePlaybackLastValue) * SAMPLE_OUTPUT_SMOOTH_ALPHA;
  } else {
    if (samplePlaybackFeeding) {
      // Evitar modulación/granulado: si hay underrun temporal, cerrar salida del
      // sample y esperar nuevo preroll antes de reanudar.
      sampleOutputEnabled = false;
      samplePlaybackFadeGain = clampf(samplePlaybackFadeGain - 4.0f * SAMPLE_FADE_STEP, 0.0f, 1.0f);
      samplePlaybackLastValue *= SAMPLE_UNDERRUN_DECAY;
    } else {
      samplePlaybackFadeGain = clampf(samplePlaybackFadeGain - SAMPLE_FADE_STEP, 0.0f, 1.0f);
      samplePlaybackLastValue *= SAMPLE_UNDERRUN_DECAY;
      if (samplePlaybackFadeGain <= 0.0f) {
        samplePlaybackActive = false;
        sampleRingReadIndex = 0;
        sampleRingWriteIndex = 0;
        samplePlaybackLastValue = 0.0f;
        sampleDcX1 = 0.0f;
        sampleDcY1 = 0.0f;
        samplePostLp = 0.0f;
        return 0.0f;
      }
    }
  }

  // Limpieza dedicada del sample: DC-block para evitar offset; sin LP agresivo
  // para conservar timbre natural del WAV.
  float dcBlocked = samplePlaybackLastValue - sampleDcX1 + SAMPLE_DC_BLOCK_R * sampleDcY1;
  sampleDcX1 = samplePlaybackLastValue;
  sampleDcY1 = dcBlocked;
  samplePostLp += (dcBlocked - samplePostLp) * SAMPLE_POST_LP_ALPHA;
  return samplePostLp;
}

void setupI2S() {
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = SAMPLE_RATE;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = 24;
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
  pinMode(BTN_GREEN_PIN, INPUT_PULLUP);

  muxRawSmoothDelayTime = static_cast<float>(readMuxRaw(MUX_CH_DELAY_TIME));
  muxRawSmoothEnvRelease = static_cast<float>(readMuxRaw(MUX_CH_ENV_RELEASE));
  muxRawSmoothVcoBase = static_cast<float>(readMuxRaw(MUX_CH_VCO_BASE));
  muxRawSmoothFmDepth = static_cast<float>(readMuxRaw(MUX_CH_FM_DEPTH));
  muxRawSmoothLfoRate = static_cast<float>(readMuxRaw(MUX_CH_LFO_RATE));
  muxRawSmoothVcaLevel = static_cast<float>(readMuxRaw(MUX_CH_VCA_LEVEL));
  muxRawSmoothDelayFeedback = static_cast<float>(readMuxRaw(MUX_CH_DELAY_FEEDBACK));

  potDelayTimeNorm = clampf(muxRawSmoothDelayTime / 4095.0f, 0.0f, 1.0f);
  potEnvReleaseNorm = clampf(muxRawSmoothEnvRelease / 4095.0f, 0.0f, 1.0f);
  potVcoBaseNorm = clampf(muxRawSmoothVcoBase / 4095.0f, 0.0f, 1.0f);
  potFmDepthNorm = clampf(muxRawSmoothFmDepth / 4095.0f, 0.0f, 1.0f);
  potLfoRateNorm = clampf(muxRawSmoothLfoRate / 4095.0f, 0.0f, 1.0f);
  potVcaLevel = clampf(muxRawSmoothVcaLevel / 4095.0f, 0.0f, 1.0f);
  potDelayFeedbackNorm = clampf(muxRawSmoothDelayFeedback / 4095.0f, 0.0f, 1.0f);

  smoothedDelayTimeNorm = potDelayTimeNorm;
  smoothedEnvReleaseNorm = potEnvReleaseNorm;
  smoothedVcoBaseNorm = potVcoBaseNorm;
  smoothedFmDepthNorm = potFmDepthNorm;
  smoothedLfoRateNorm = potLfoRateNorm;
  smoothedVcaLevel = potVcaLevel;
  smoothedDelayFeedbackNorm = potDelayFeedbackNorm;
  fmHzSmoothed = mapLinear(smoothedVcoBaseNorm, VCO_BASE_MIN_HZ, VCO_BASE_MAX_HZ);

  setupI2S();
  sdInitialized = initSdCard();
  if (sdInitialized) {
    (void)prepareSampleMetadata();
  }
  xTaskCreatePinnedToCore(sampleStreamTask,
                          "sample_stream",
                          4096,
                          nullptr,
                          2,
                          &sampleStreamTaskHandle,
                          0);

  Serial.println("Synth_tone_test: VCV FM siren (I4=LFO Hz, I3=FM %, I2=VCO Hz, C0/S0=delay time, C6/S6=delay feedback, I5=volume, red=waveform, white=auto/manual, blue=tap trigger + long-press start-point, green=play SD WAV)");
  if (sdInitialized) {
    Serial.println("SD ready");
  } else {
    Serial.println("SD not ready");
  }
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
    suppressControlUntilMs = now + BUTTON_CONTROL_SUPPRESS_MS;
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
    suppressControlUntilMs = now + BUTTON_CONTROL_SUPPRESS_MS;
    if (whiteStableState == LOW) {
      autoMode = !autoMode;
    }
  }

  if (autoMode != autoModePrev) {
    if (autoMode) {
      envelopeStage = EnvelopeStage::Sustain;
      envelopeLevel = 1.0f;
      envelopeSustainSamples = 0;
    } else {
      // Al salir de AUTO, no cortar en seco: aplicar release segun I1/C1.
      envelopeStage = EnvelopeStage::Release;
      if (envelopeLevel < 0.001f) {
        envelopeLevel = 1.0f;
      }
      envelopeSustainSamples = 0;
    }
    autoModePrev = autoMode;
  }

  bool blueReading = digitalRead(BTN_BLUE_PIN);
  if (blueReading != blueLastReading) {
    blueLastReading = blueReading;
    blueLastChangeMs = now;
  }
  if ((now - blueLastChangeMs) > DEBOUNCE_MS && blueReading != blueStableState) {
    blueStableState = blueReading;
    suppressControlUntilMs = now + BUTTON_CONTROL_SUPPRESS_MS;
    if (blueStableState == LOW) {
      bluePressedStartMs = now;
      blueLongPressLatched = false;
    } else {
      if (blueLongPressLatched) {
        lfoStartPoint = static_cast<LfoStartPoint>((static_cast<uint8_t>(lfoStartPoint) + 1) % static_cast<uint8_t>(LfoStartPoint::Count));
      }
      blueLongPressLatched = false;
    }
  }
  if (blueStableState == LOW && !blueLongPressLatched && (now - bluePressedStartMs >= BLUE_LONG_PRESS_MS)) {
    blueLongPressLatched = true;
  }
  manualGateHeld = (blueStableState == LOW);

  bool greenReading = digitalRead(BTN_GREEN_PIN);
  if (greenReading != greenLastReading) {
    greenLastReading = greenReading;
    greenLastChangeMs = now;
  }
  if ((now - greenLastChangeMs) > DEBOUNCE_MS && greenReading != greenStableState) {
    greenStableState = greenReading;
    suppressControlUntilMs = now + BUTTON_CONTROL_SUPPRESS_MS;
    if (greenStableState == LOW) {
      sampleStartRequest = true;
    }
  }

  // En modo manual, al presionar el trigger reiniciamos fase para que cada disparo
  // arranque desde el punto inicial predecible de cada forma de onda.
  bool manualTriggerPressed = (!autoMode && manualGateHeld && !manualGateHeldPrev);
  bool manualTriggerReleased = (!autoMode && !manualGateHeld && manualGateHeldPrev);
  if (manualTriggerPressed) {
    float startPhaseNorm = getLfoStartPhaseNorm(lfoWaveform, lfoStartPoint);
    lfoPhase = startPhaseNorm * TWO_PI;
    carrierPhase = 0.0f;
    vcoToneState = 0.0f;
    outputLpState = 0.0f;
    dcBlockX1 = 0.0f;
    dcBlockY1 = 0.0f;
    envelopeStage = EnvelopeStage::Attack;
    envelopeLevel = 0.0f;
    envelopeSustainSamples = 0;
  }
  if (manualTriggerReleased && envelopeStage != EnvelopeStage::Idle) {
    envelopeStage = EnvelopeStage::Release;
  }
  manualGateHeldPrev = manualGateHeld;

  if (now >= suppressControlUntilMs && now - lastControlMs >= CONTROL_UPDATE_MS) {
    // Round-robin: 1 canal por tick para reducir actividad ADC instantanea y ruido.
    switch (muxScanIndex) {
      case 0:
        potDelayTimeNorm = readSmoothedNormalized(MUX_CH_DELAY_TIME, muxRawSmoothDelayTime);
        break;
      case 1:
        potEnvReleaseNorm = readSmoothedNormalized(MUX_CH_ENV_RELEASE, muxRawSmoothEnvRelease);
        break;
      case 2:
        potVcoBaseNorm = readSmoothedNormalized(MUX_CH_VCO_BASE, muxRawSmoothVcoBase);
        break;
      case 3:
        potFmDepthNorm = readSmoothedNormalized(MUX_CH_FM_DEPTH, muxRawSmoothFmDepth);
        break;
      case 4:
        potLfoRateNorm = readSmoothedNormalized(MUX_CH_LFO_RATE, muxRawSmoothLfoRate);
        break;
      case 5:
        potVcaLevel = readSmoothedNormalized(MUX_CH_VCA_LEVEL, muxRawSmoothVcaLevel);
        break;
      case 6:
      default:
        potDelayFeedbackNorm = readSmoothedNormalized(MUX_CH_DELAY_FEEDBACK, muxRawSmoothDelayFeedback);
        break;
    }
    muxScanIndex = (muxScanIndex + 1) % 7;
    lastControlMs = now;
  }

  float targetDelayTimeNorm = potDelayTimeNorm;
  float targetEnvReleaseNorm = potEnvReleaseNorm;
  float targetVcoBaseNorm = potVcoBaseNorm;
  float targetFmDepthNorm = potFmDepthNorm;
  float targetLfoRateNorm = potLfoRateNorm;
  float targetVcaLevel = mapVcaPotToGain(potVcaLevel) * VCA_LEVEL_MAX;
  float targetDelayFeedbackNorm = potDelayFeedbackNorm;
  float targetGateLevel = (autoMode || envelopeStage != EnvelopeStage::Idle) ? 1.0f : 0.0f;

  for (int i = 0; i < BUFFER_SIZE; ++i) {
    smoothedDelayTimeNorm += (targetDelayTimeNorm - smoothedDelayTimeNorm) * PARAM_SMOOTH_ALPHA;
    smoothedEnvReleaseNorm += (targetEnvReleaseNorm - smoothedEnvReleaseNorm) * PARAM_SMOOTH_ALPHA;
    smoothedVcoBaseNorm += (targetVcoBaseNorm - smoothedVcoBaseNorm) * PARAM_SMOOTH_ALPHA;
    smoothedFmDepthNorm += (targetFmDepthNorm - smoothedFmDepthNorm) * PARAM_SMOOTH_ALPHA;
    smoothedLfoRateNorm += (targetLfoRateNorm - smoothedLfoRateNorm) * PARAM_SMOOTH_ALPHA;
    smoothedVcaLevel += (targetVcaLevel - smoothedVcaLevel) * PARAM_SMOOTH_ALPHA;
    smoothedDelayFeedbackNorm += (targetDelayFeedbackNorm - smoothedDelayFeedbackNorm) * PARAM_SMOOTH_ALPHA;
    gateLevel += (targetGateLevel - gateLevel) * GATE_SMOOTH_ALPHA;

    // Envelope para modo manual: al presionar trigger abre a ganancia plena y
    // al soltar cae segun release (I1/C1). AUTO queda siempre a nivel 1.
    if (autoMode) {
      envelopeLevel = 1.0f;
      envelopeStage = EnvelopeStage::Sustain;
      envelopeSustainSamples = 0;
    } else {
      float attackSamples = fmaxf(ENV_ATTACK_MS * SAMPLE_RATE / 1000.0f, 1.0f);
      float decaySamples = fmaxf(ENV_DECAY_MS * SAMPLE_RATE / 1000.0f, 1.0f);
      float releaseMs = mapLinear(smoothedEnvReleaseNorm, ENV_RELEASE_MIN_MS, ENV_RELEASE_MAX_MS);
      float releaseSamples = fmaxf(releaseMs * SAMPLE_RATE / 1000.0f, 1.0f);

      switch (envelopeStage) {
        case EnvelopeStage::Attack:
          envelopeLevel += 1.0f / attackSamples;
          if (envelopeLevel >= 1.0f) {
            envelopeLevel = 1.0f;
            envelopeStage = EnvelopeStage::Decay;
          }
          break;
        case EnvelopeStage::Decay:
          envelopeLevel += (ENV_SUSTAIN_LEVEL - 1.0f) / decaySamples;
          if (envelopeLevel <= ENV_SUSTAIN_LEVEL) {
            envelopeLevel = ENV_SUSTAIN_LEVEL;
            envelopeStage = EnvelopeStage::Sustain;
            envelopeSustainSamples = 0;
          }
          break;
        case EnvelopeStage::Sustain:
          envelopeLevel = ENV_SUSTAIN_LEVEL;
          break;
        case EnvelopeStage::Release:
          envelopeLevel -= 1.0f / releaseSamples;
          if (envelopeLevel <= 0.0f) {
            envelopeLevel = 0.0f;
            envelopeStage = EnvelopeStage::Idle;
          }
          break;
        case EnvelopeStage::Idle:
        default:
          envelopeLevel = 0.0f;
          break;
      }
      envelopeLevel = clampf(envelopeLevel, 0.0f, 1.0f);
    }

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
    float drySample = vcoToneState * OUTPUT_GAIN * gateLevel * envelopeLevel;

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
    float synthSample = drySample * (1.0f - DELAY_MIX) + wetSample * DELAY_MIX;
    float sdSample = getNextSamplePlaybackValue() * SAMPLE_MIX_GAIN;
    float feedbackInput = drySample + delayedSample * currentDelayFeedback;
    delayFeedbackFiltered += (feedbackInput - delayFeedbackFiltered) * DELAY_FEEDBACK_DARKEN_ALPHA;

    // Escritura del delay a tasa reducida para habilitar 5 s de retardo dentro de la RAM disponible.
    delayWritePhase += DELAY_SAMPLE_RATE;
    if (delayWritePhase >= SAMPLE_RATE) {
      delayWritePhase -= SAMPLE_RATE;
      delayBuffer[delayIndex] = static_cast<int16_t>(clampf(delayFeedbackFiltered, -1.0f, 1.0f) * 32767.0f);
      delayIndex = (delayIndex + 1) % DELAY_BUFFER_SAMPLES;
    }

    // Ruta de sirena: saturacion + limpieza final.
    synthSample = tanhf(synthSample * OUTPUT_DRIVE) / outputDriveNorm;
    float dcBlocked = synthSample - dcBlockX1 + DC_BLOCK_R * dcBlockY1;
    dcBlockX1 = synthSample;
    dcBlockY1 = dcBlocked;
    outputLpState += (dcBlocked - outputLpState) * OUTPUT_LP_ALPHA;

    float synthOut = outputLpState * smoothedVcaLevel;
    if (samplePlaybackActive) {
      synthOut *= 0.82f;
    }
    float mixed = synthOut + sdSample;
    mixed = clampf(mixed, -1.1f, 1.1f);
    if (!samplePlaybackActive) {
      mixed = mixed - (mixed * mixed * mixed) * 0.12f;
    }
    mixed = clampf(mixed, -1.0f, 1.0f);

    int16_t pcm = static_cast<int16_t>(mixed * 32767.0f);
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
