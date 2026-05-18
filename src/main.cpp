#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

// ---------------- CONFIGURACION ----------------
#define SAMPLE_RATE 44100
#define PI 3.14159265

// Pines I2S
#define I2S_BCK 26
#define I2S_WS  25
#define I2S_DO  22

// Entradas
#define POT_PIN 34
#define BTN_PIN 27

// Tamano de buffer
#define BUFFER_SIZE 256

// ---------------- VARIABLES ----------------
float phase = 0.0f;
int waveform = 0; // 0=seno, 1=cuadrada, 2=sierra

// Antirrebote boton
bool lastBtnState = HIGH;
bool stableBtnState = HIGH;
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50;

const char* waveformName(int wf) {
  switch (wf) {
    case 0:
      return "seno";
    case 1:
      return "cuadrada";
    case 2:
      return "sierra";
    default:
      return "desconocida";
  }
}

// ---------------- I2S ----------------
void setupI2S() {
  i2s_config_t config;
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_I2S;
  config.intr_alloc_flags = 0;
  config.dma_buf_count = 8;
  config.dma_buf_len = 128;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;

  i2s_pin_config_t pin_config;
  pin_config.bck_io_num = I2S_BCK;
  pin_config.ws_io_num = I2S_WS;
  pin_config.data_out_num = I2S_DO;
  pin_config.data_in_num = I2S_PIN_NO_CHANGE;
  pin_config.mck_io_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  analogReadResolution(12); // 0-4095 en ESP32
  setupI2S();
}

// ---------------- LOOP ----------------
void loop() {
  // -------- Leer potenciometro --------
  int potValue = analogRead(POT_PIN);

  // Mapeo lineal de frecuencia
  float freq = 100.0f + (potValue / 4095.0f) * 1900.0f; // 100 Hz a 2000 Hz

  // -------- Leer boton (antirrebote robusto, sin delay) --------
  bool reading = digitalRead(BTN_PIN);

  if (reading != lastBtnState) {
    lastDebounceTime = millis();
    lastBtnState = reading;
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (stableBtnState != reading) {
      stableBtnState = reading;

      // Flanco de bajada: boton presionado
      if (stableBtnState == LOW) {
        waveform = (waveform + 1) % 3;
      }
    }
  }

  // -------- Generar audio --------
  int16_t buffer[BUFFER_SIZE * 2]; // estereo

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float sample = 0.0f;

    switch (waveform) {
      case 0: // SENO
        sample = sinf(phase);
        break;

      case 1: // CUADRADA
        sample = (sinf(phase) >= 0.0f) ? 1.0f : -1.0f;
        break;

      case 2: // DIENTE DE SIERRA
        sample = (phase / PI) - 1.0f;
        break;
    }

    // Avanzar fase
    phase += 2.0f * PI * freq / SAMPLE_RATE;
    if (phase >= 2.0f * PI) {
      phase -= 2.0f * PI;
    }

    // Convertir a 16 bits
    int16_t out = (int16_t)(sample * 20000.0f);

    // Estereo (L y R iguales)
    buffer[2 * i]     = out;
    buffer[2 * i + 1] = out;
  }

  // Enviar audio
  size_t bytesWritten;
  i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);

  // Monitor serie cada 500 ms
  static unsigned long lastPrint = 0;
  unsigned long now = millis();
  if (now - lastPrint >= 500) {
    Serial.print("pot=");
    Serial.print(potValue);
    Serial.print(" freq=");
    Serial.print(freq, 1);
    Serial.print("Hz waveform=");
    Serial.print(waveform);
    Serial.print(" (");
    Serial.print(waveformName(waveform));
    Serial.println(")");
    lastPrint = now;
  }
}
