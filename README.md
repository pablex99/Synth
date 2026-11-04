# Synth Tone Test (PCM5102A + ESP32)

Prueba de audio minima: reproduce una sirena dub estilo patch modular (LFO -> FM de VCO
cuadrado -> VCA -> delay). Controles activos por MUX: I4 ajusta la
frecuencia del LFO (0.05 .. 50 Hz), I3 ajusta la profundidad de FM (-100 .. 300%), I2 ajusta la
frecuencia base del VCO (50 .. 4000 Hz), C0/S0 ajusta el tiempo del delay, C6/S6 ajusta el
feedback del delay e I5 ajusta el
volumen final (0 .. 100% lineal, con techo de salida al 50% del nivel anterior).
No usa OLED; valida que el hardware de audio I2S + PCM5102A, mas el MUX de potenciometros,
reproducen correctamente esa cadena de sintesis. Botones: rojo cambia forma de onda del LFO,
blanco alterna modo AUTO/MANUAL y azul actua como trigger/gate en modo MANUAL.

## Pines I2S (igual que proyecto Synth)

- BCK: GPIO13
- WS/LRCK: GPIO27
- DIN: GPIO14

## Pines MUX (igual que proyecto Synth, se usan canales C0, I2-I5 y C6)

- S0: GPIO16, S1: GPIO17, S2: GPIO19, S3: GPIO18
- SIG/OUT: GPIO35 (ADC)
- C0/S0 -> Tiempo del delay (0 .. 5000 ms)
- I2/C2 -> Frecuencia base del VCO (50 .. 4000 Hz)
- I3/C3 -> Profundidad FM del VCO (-100 .. 300%)
- I4/C4 -> Frecuencia del LFO (0.05 .. 50 Hz)
- I5/C5 -> Volumen de salida (0 .. 100%, lineal)
- C6/S6 -> Feedback del delay (0 .. 60%)

## Boton

- Rojo (GPIO4, INPUT_PULLUP): cicla la forma de onda del LFO en este orden:
  - Diente de sierra descendente (arranca en pico y desciende linealmente).
  - Senoidal.
  - Diente de sierra invertida/ascendente (sube linealmente y cae de golpe al reiniciar).
  - Cuadrada.
- Blanco (GPIO21, INPUT_PULLUP): alterna entre modo AUTO y modo MANUAL.
- Azul (GPIO22, INPUT_PULLUP): en modo MANUAL actua como compuerta/trigger (mantener
  presionado para que suene). En modo AUTO no afecta la salida.

## Comportamiento

- Sirena dub con flujo tipo modular (como en VCV Rack): un LFO configurable modula por FM la
  frecuencia de un VCO cuadrado.
- Controles tonales:
  - C0/S0 controla el tiempo del delay en 0 .. 5000 ms.
  - C6/S6 controla el feedback del delay en 0 .. 60%.
  - I4 controla la frecuencia del LFO en 0.05 .. 50 Hz.
  - I3 controla la profundidad de FM en -100 .. 300%.
  - I2 controla la frecuencia base del VCO en 50 .. 4000 Hz.
  - I5 controla el nivel final del VCA en 0 .. 100%, con techo de salida al 50%
    del nivel maximo anterior.
- Caracter analogico agregado respecto al oscilador digital original:
  - Ciclo de trabajo ligeramente asimetrico (45/55 en vez de 50/50), como un astable real.
  - Oscilador cuadrado con correccion PolyBLEP (menos aliasing/aspereza digital).
  - Rolloff suave del VCO (simula el tiempo de subida/bajada de un transistor).
  - Saturacion suave (tipo transistor) en la salida final.
- Cadena de audio: salida cuadrada del VCO -> VCA (nivel lineal por I5) -> delay de feedback
  con tiempo variable por C0/S0 (0 .. 5000 ms), feedback variable por C6/S6 (0 .. 60%) y 4.5% de mezcla,
  siempre activo, con cada repeat
  oscureciendose progresivamente (filtro paso-bajo en el lazo de feedback, como un delay
  analogico a BBD en vez de un repeat digital perfectamente limpio).
- Para habilitar hasta 5000 ms sin exceder la RAM del ESP32, el buffer interno de delay se
  almacena a tasa reducida (aprox. 7.35 kHz), manteniendo el render final de audio en 44.1 kHz.
- El retorno wet del delay ademas se atenúa internamente (wet trim = 45%) para privilegiar
  el camino dry y acercar el resultado al caracter de una salida MIX, no a una salida WET pura.
- Mitigaciones de ruido en la salida:
  - Lectura ADC endurecida (asentamiento + dummy reads + promedio + deadband).
  - Polling de controles desacoplado del audio y escaneo MUX round-robin.
  - Bloqueo de DC y filtro suave de salida para limpiar offset/aspereza residual.
  - Mayor colchon DMA de I2S para evitar artefactos por starvation.
  - I5 funciona como volumen maestro post-delay: en minimo (0%) la salida queda en silencio.
- Sin OLED: la sirena suena en modo AUTO apenas se enciende el ESP32. En modo MANUAL solo
  suena mientras se mantiene el boton azul. El timbre cambia por C0/S0, I2, I3, I4, I5, C6/S6 y la
  forma de onda seleccionada con el boton rojo.

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
