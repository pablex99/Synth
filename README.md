# Synth (ESP32 + PCM5102A)

Firmware actual del proyecto `Synth` con sirena pasiva, modulación por LFOs y control completo por MUX + botones + OLED.

## Estado Actual

- Sirena base continua en estéreo por I2S + PCM5102A.
- LFOs disponibles: `Pitch`, `Volume`, `Filter`, `Reverb`, `Delay`.
- Cada LFO tiene `ON/OFF` y forma de onda independiente (`SIN`, `SQR`, `TRI`, `SAW`).
- La sirena también puede cambiar su forma (`ORIG`, `SIN`, `SQR`, `TRI`, `SAW`).
- Pantalla OLED por secciones:
  - Inicio: sección de sirena + guía de botones por color.
  - Navegación: una sección LFO por pantalla con sus parámetros.

## Mapeo De Hardware

### I2S (ESP32 -> PCM5102A)

- `GPIO26` -> `BCK`
- `GPIO25` -> `WS/LRCK`
- `GPIO22` -> `DIN`

### MUX CD4067

- `SIG` -> `GPIO35`
- `S0` -> `GPIO16`
- `S1` -> `GPIO17`
- `S2` -> `GPIO19`
- `S3` -> `GPIO18`

### OLED I2C

- `SDA` -> `GPIO21`
- `SCL` -> `GPIO23`

### Botones

- `GPIO27` (Rojo): cambia forma de onda de la sirena.
- `GPIO13` (Blanco): selecciona sección/LFO activa.
- `GPIO4` (Verde): enciende/apaga el LFO seleccionado.
- `GPIO5` (Negro): cambia forma de onda del LFO seleccionado.

## Potenciómetros (Canales Del MUX)

- `I0` -> `Pitch LFO Depth`
- `I1` -> `Pitch LFO Rate`
- `I2` -> `Volume LFO Rate`
- `I3` -> `Volume LFO Depth`
- `I4` -> `Filter Base` (`-1` LP, `0` neutro, `+1` HP)
- `I5` -> `Filter LFO Rate`
- `I6` -> `Filter LFO Depth`
- `I7` -> `Reverb Mix`
- `I8` -> `Reverb Decay`
- `I9` -> `Delay Time`
- `I10` -> `Delay Feedback`
- `I11` -> `Delay Mix`

## Build Y Carga

Compilar:

```powershell
pio run
```

Subir firmware (ajusta `COMx`):

```powershell
pio run -t upload --upload-port COM4
```

Monitor serie opcional:

```powershell
pio device monitor -b 115200
```
