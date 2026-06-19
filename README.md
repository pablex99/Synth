# Synth Tone Test (PCM5102A + ESP32)

Proyecto de prueba de sirena usando el mismo conexionado y hardware del proyecto Synth.

## Pines I2S (igual que proyecto Synth)

- BCK: GPIO26
- WS/LRCK: GPIO25
- DIN: GPIO22

## Pines MUX (CD4067)

- SIG: GPIO35
- S0: GPIO16
- S1: GPIO17
- S2: GPIO19
- S3: GPIO18

## Potenciometros (canales MUX)

- I0: Pitch LFO Depth
- I1: Pitch LFO Rate
- I2: Volume LFO Rate
- I3: Volume LFO Depth
- I4: Filter Base (-1 LP / 0 neutral / +1 HP)
- I5: Filter LFO Rate
- I6: Filter LFO Depth
- I7: Reverb Mix
- I8: Reverb Decay
- I9: Delay Time
- I10: Delay Feedback
- I11: Delay Mix

## Botones

- GPIO27: cambia forma de onda de la sirena (ORIG/SIN/SQR/TRI/SAW)
- GPIO13: selecciona seccion LFO activa (Pitch/Volume/Filter/Reverb/Delay)
- GPIO4: enciende/apaga (ON/OFF) el LFO seleccionado
- GPIO5: cambia forma del LFO activo (SIN/SQR/TRI/SAW)

## OLED I2C

- SDA: GPIO21
- SCL: GPIO23

## Comportamiento

- Sirena pasiva continua en estereo.
- LFO de pitch, volumen, filtro, reverb y delay controlados desde I0-I11.
- Cada LFO tiene forma independiente (SIN/SQR/TRI/SAW) y estado ON/OFF.
- Forma de onda de la sirena con selector dedicado (ORIG/SIN/SQR/TRI/SAW).
- Filtro base en I4: centro sin sesgo, izquierda prioriza LP, derecha prioriza HP.
- OLED muestra:
  - al iniciar: solo seccion SIRENA y su forma de onda
  - al seleccionar con GPIO13: solo la seccion del LFO elegido y sus parametros

## Compilar

```powershell
pio run
```

## Subir (ejemplo COM3)

```powershell
pio run -t upload --upload-port COM3
```

## Monitor serie (opcional)

```powershell
pio device monitor -b 115200
```
