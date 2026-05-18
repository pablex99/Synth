#include <driver/i2s.h>
#include <math.h>

// ---------------- CONFIGURACIÓN ----------------
#define SAMPLE_RATE 44100
#define PI 3.14159265

// Pines I2S
#define I2S_BCK 26
#define I2S_WS  25
#define I2S_DO  22

// Entradas
#define POT_PIN 34
#define BTN_PIN 27

// Tamaño de buffer
#define BUFFER_SIZE 256

// ---------------- VARIABLES ----------------
float phase = 0.0;
int waveform = 0; // 0=sine, 1=square, 2=saw

// Antirrebote botón
bool lastBtnState = HIGH;
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50;

// ---------------- I2S ----------------
void setupI2S() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_DO,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

// ---------------- SETUP ----------------
void setup() {
  pinMode(BTN_PIN, INPUT_PULLUP);
  analogReadResolution(12); // 0-4095
  setupI2S();
}

// ---------------- LOOP ----------------
void loop() {

  // -------- Leer potenciómetro --------
  int potValue = analogRead(POT_PIN);

  // mapear frecuencia (ajustable)
  float freq = 100.0 + (potValue / 4095.0) * 1900.0;
  // rango: 100 Hz a 2000 Hz

  // -------- Leer botón (antirrebote real) --------
  bool reading = digitalRead(BTN_PIN);

  if (reading != lastBtnState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (lastBtnState == HIGH && reading == LOW) {
      waveform = (waveform + 1) % 3;
    }
  }

  lastBtnState = reading;

  // -------- Generar audio --------
  int16_t buffer[BUFFER_SIZE * 2]; // estéreo

  for (int i = 0; i < BUFFER_SIZE; i++) {

    float sample = 0.0;

    switch (waveform) {
      case 0: // SENO
        sample = sin(phase);
        break;

      case 1: // CUADRADA
        sample = (sin(phase) >= 0) ? 1.0 : -1.0;
        break;

      case 2: // DIENTE DE SIERRA
        sample = (phase / PI) - 1.0;
        break;
    }

    // avanzar fase
    phase += 2.0 * PI * freq / SAMPLE_RATE;
    if (phase >= 2.0 * PI) {
      phase -= 2.0 * PI;
    }

    // convertir a 16 bits
    int16_t out = (int16_t)(sample * 20000);

    // estéreo (L y R iguales)
    buffer[2 * i]     = out; // L
    buffer[2 * i + 1] = out; // R
  }

  // enviar audio
  size_t bytesWritten;
  i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
}