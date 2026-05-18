# Synth ESP32 + PCM5102A

Sintetizador simple con ESP32 y DAC PCM5102A.

Funciones principales:
- Control de frecuencia con potenciometro.
- Cambio de forma de onda con boton.
- Salida de audio por DAC I2S (PCM5102A).

## Formas de onda
- 0: seno
- 1: cuadrada
- 2: sierra

## Conexionado
### ESP32 -> PCM5102A (I2S)
- GPIO26 -> BCK
- GPIO25 -> LRCK/WS
- GPIO22 -> DIN

### Alimentacion PCM5102A
- VIN (digital) -> 3.3V
- AVCC (analogico) -> 3.3V filtrado
- GND digital -> GND comun
- AGND -> GND comun (punto estrella)
- XSMT -> 3.3V
- FMT -> GND
- SCK -> GND

### Potenciometro
- Lateral 1 -> GND
- Centro -> GPIO34
- Lateral 2 -> 3.3V

### Boton
- Un pin -> GPIO27
- Otro pin -> GND

## Diagrama rapido

```text
ESP32                        PCM5102A
-----                        --------
GPIO26  -------------------> BCK
GPIO25  -------------------> LRCK/WS
GPIO22  -------------------> DIN
3.3V    -------------------> VIN (digital)
3.3V filtrado -------------> AVCC
GND     -------------------> GND
GND     -------------------> AGND
3.3V    -------------------> XSMT
GND     -------------------> FMT
GND     -------------------> SCK

POT: 3.3V ---/\/\/--- GND
              |
            GPIO34

BTN: GPIO27 ---o o--- GND
```

## Compilar y subir
Desde la carpeta del proyecto:

- Compilar:
  - pio run
- Subir firmware:
  - pio run -t upload --upload-port COM3
- Monitor serie:
  - pio device monitor --port COM3 --baud 115200

## Nota de calidad de audio
La limpieza de audio depende en gran parte del hardware:
- Alimentacion limpia para AVCC.
- Masa en punto estrella.
- Cableado corto y ordenado para I2S.
- Salida del DAC conectada a entrada AUX/LINE IN del equipo de audio.

## Estructura del proyecto
- src/main.cpp: firmware principal
- platformio.ini: configuracion de PlatformIO
