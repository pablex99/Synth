# Synth (ESP32 + PCM5102A)

Proyecto de sintetizador embebido con salida I2S, control por MUX CD4067 y reproduccion de sample WAV desde microSD.

Estado actual:
- Audio general estable y limpio en modo sirena.
- Trigger manual y controles de sintesis operativos.
- Reproduccion de sample WAV integrada con pipeline de mitigacion de ruido (pre-roll, buffer y suavizado).
- Codigo en rama principal actualizado con el merge mas reciente.

## 1) Resumen funcional

El firmware genera una sirena tipo dub con esta cadena principal:
- LFO configurable
- FM sobre VCO cuadrado
- VCA
- Delay con feedback
- Salida I2S a PCM5102A

Adicionalmente:
- Boton verde dispara reproduccion de un WAV desde SD.
- El sample se mezcla con la salida del sintetizador usando una ruta con protecciones anti-click/anti-underrun.

## 2) Estado tecnico actual

Implementado y vigente:
- Modo AUTO/MANUAL con envelope en manual.
- Boton azul:
  - toque simple: trigger manual
  - pulsacion larga (2 s) y soltar: ciclo del punto de inicio del LFO
- Boton rojo: ciclo de forma de onda del LFO.
- Boton blanco: cambio AUTO/MANUAL.
- Boton verde: inicio de sample WAV desde SD.

Mejoras recientes incorporadas al firmware:
- Lectura de sample desacoplada del path critico de audio (tarea de streaming + ring buffer).
- Pre-carga de metadatos WAV para minimizar trabajo al momento de disparo.
- Pre-roll antes de habilitar salida del sample.
- Debounce y supresion temporal de escaneo de controles tras eventos de botones.
- Ajustes de mezcla/ganancia para mantener headroom.

## 3) Requisitos del sample WAV

Formato aceptado:
- WAV PCM16
- 44.1 kHz
- Mono o estereo

Comportamiento de seleccion:
- El firmware usa el primer archivo .wav encontrado en la raiz de la SD.
- Se cachea la ruta preferida para no reescanear constantemente.

## 4) Mapeo de pines

### I2S
- BCK: GPIO13
- WS/LRCK: GPIO27
- DIN: GPIO14

### MUX CD4067
- S0: GPIO16
- S1: GPIO17
- S2: GPIO19
- S3: GPIO18
- SIG/OUT (ADC): GPIO35

Canales usados:
- I0/C0: tiempo de delay
- I1/C1: release de envelope (manual)
- I2/C2: frecuencia base VCO
- I3/C3: profundidad FM
- I4/C4: frecuencia LFO
- I5/C5: volumen final
- I6/C6: feedback delay

### Botones (INPUT_PULLUP)
- Rojo: GPIO4
- Blanco: GPIO21
- Azul: GPIO22
- Verde: GPIO23

### microSD (SPI)
- CS: GPIO33
- SCK: GPIO25
- MOSI: GPIO26
- MISO: GPIO34

## 5) Controles de usuario

- Rojo:
  - Cicla formas LFO: saw down -> sine -> saw up -> square
- Blanco:
  - Alterna AUTO/MANUAL
- Azul:
  - MANUAL: trigger/gate de la sirena
  - Pulsacion larga + soltar: cambia punto de inicio del LFO
- Verde:
  - Dispara reproduccion del sample WAV en SD

## 6) Arquitectura de audio

Ruta de sintetizador:
- VCO cuadrado (con PolyBLEP) -> VCA -> delay -> procesamiento final

Ruta de sample:
- SD -> parser WAV -> ring buffer -> salida sample con suavizado/fade

Mezcla:
- Se suma salida synth + sample con control de ganancia y protecciones de headroom.

Objetivo de diseno:
- Mantener timbre del synth.
- Evitar crujidos, clicks y artifacts por lectura SD o pulsaciones de botones.

## 7) Parametros de estabilidad relevantes

Estos parametros viven en src/main.cpp y son los mas sensibles para ajustar limpieza/timbre:
- CONTROL_UPDATE_MS
- DEBOUNCE_MS
- SAMPLE_SD_BLOCK_BYTES
- SAMPLE_RING_CAPACITY
- SAMPLE_RING_LOW_WATER
- SAMPLE_PREROLL_SAMPLES
- SAMPLE_MIX_GAIN
- SAMPLE_OUTPUT_SMOOTH_ALPHA
- SAMPLE_OUTPUT_SLEW_MAX

Si se ajustan, hacerlo con pruebas A/B cortas y manteniendo build estable en RAM.

## 8) Limitaciones conocidas

- La SD puede variar en latencia segun marca/calidad.
- Con WAV de mala calidad o picos extremos, puede requerirse ajuste fino de mezcla.
- El proyecto prioriza estabilidad embebida dentro de RAM del ESP32 (sin PSRAM externa).

## 9) Compilacion y carga

Compilar:

```powershell
pio run
```

Subir (ejemplo COM3):

```powershell
pio run -t upload --upload-port COM3
```

Monitor serie:

```powershell
pio device monitor -b 115200
```

## 10) Glosario de audio y sintesis

- Sirena dub: timbre de sirena electronica con ecos repetitivos y caracter performativo.
- Sintesis modular: enfoque por bloques (LFO, VCO, VCA, delay) encadenados en serie o paralelo.
- Cadena de senal: orden de procesamiento del audio desde la generacion hasta la salida.
- VCO (Voltage Controlled Oscillator): oscilador principal que define el tono base.
- LFO (Low Frequency Oscillator): oscilador de baja frecuencia usado para modular parametros.
- FM (Frequency Modulation): modulacion de frecuencia, donde una senal altera la frecuencia de otra.
- Profundidad FM: magnitud del barrido FM aplicado al VCO.
- Frecuencia base: valor central del VCO antes de aplicar modulaciones.
- Forma de onda: geometria del ciclo (seno, sierra, cuadrada) que define el color timbrico.
- Fase: posicion instantanea dentro del ciclo de una onda periodica.
- Trigger: evento de disparo puntual para iniciar una accion (por ejemplo, envelope o sample).
- Gate: estado sostenido on/off que mantiene activa una etapa.
- Envelope (ADSR simplificado): curva de amplitud en el tiempo (attack/decay/sustain/release).
- Attack: tiempo de subida inicial del nivel al disparar.
- Decay: tiempo de transicion desde nivel maximo al sustain.
- Sustain: nivel sostenido mientras la nota permanece activa.
- Release: tiempo de caida del nivel al soltar gate/trigger.
- VCA (Voltage Controlled Amplifier): etapa de ganancia/volumen controlada.
- Ganancia: factor de amplificacion o atenuacion.
- Headroom: margen disponible antes de saturacion o clipping.
- Saturacion suave: compresion no lineal gradual para redondear picos.
- Clipping: recorte por exceder el rango maximo representable.
- Delay: efecto de repeticion temporal de la senal.
- Tiempo de delay: separacion temporal entre original y repeticion.
- Feedback: porcion de la repeticion que vuelve al delay para generar colas sucesivas.
- Mix dry/wet: balance entre senal original (dry) y procesada (wet).
- Dry: ruta sin efecto.
- Wet: ruta procesada por el efecto.
- Filtro paso-bajo (LP): atenua altas frecuencias, suavizando el sonido.
- DC offset: desplazamiento del centro de la onda respecto de cero.
- DC block: filtro que elimina componente continua (offset DC).
- Aliasing: artefactos espectrales por discontinuidades o contenido sobre Nyquist.
- PolyBLEP: tecnica anti-alias para ondas con transiciones abruptas.
- Duty cycle: proporcion de tiempo en estado alto de una onda pulso/cuadrada.
- Sample rate: muestras por segundo del sistema de audio.
- Buffer de audio: bloque de muestras procesado/escrito por iteracion.
- Ring buffer: buffer circular productor-consumidor para desacoplar tareas.
- Underflow/underrun: faltan muestras a tiempo para la salida de audio.
- Jitter temporal: variacion en tiempos de procesamiento que puede causar artifacts.
- SPI: bus serie para periféricos (microSD en este proyecto).
- I2S: interfaz digital de audio hacia el DAC PCM5102A.
- DAC (PCM5102A): convertidor digital-analogico de salida.
- DMA: transferencia de memoria sin cargar CPU en cada muestra.
- Zipper noise: ruido por cambios discretos bruscos en parametros de control.
- Deadband: zona donde cambios pequenos se ignoran para reducir jitter.
- Debounce: filtrado temporal para lecturas estables de botones.
- Pre-roll: precarga de buffer antes de habilitar salida para evitar cortes iniciales.
- Slew limiting: limitacion de salto entre muestras/valores consecutivos.
- Smooth alpha: factor de suavizado exponencial (filtro de primer orden).
- Monofonia de sample: mezcla a un solo canal interno antes de salida estereo duplicada.
- Estereo duplicado: misma muestra en L/R cuando no hay panoramizacion independiente.
- Escaneo round-robin: lectura ciclica de entradas analógicas para repartir carga.
- MUX CD4067: multiplexor analógico 16:1 para concentrar lecturas ADC.
- Latencia percibida: retardo audible entre accion y respuesta sonora.
- Ruta critica de audio: seccion que debe cumplir tiempo real estricto por buffer.
- Contencion de recursos: competencia entre tareas por CPU/bus que degrada audio.

## 11) Estado de rama principal

La rama main contiene los cambios de limpieza de audio y reproduccion SD integrados via PR reciente.

Si en el visor de tu IDE no aparece de inmediato el ultimo commit:
- refresca/fetch del repositorio
- recarga la vista de grafo
- verifica que estas mirando exactamente este repo Synth y no otro de la workspace

Comando rapido de verificacion:

```powershell
git log --oneline --decorate -n 5
```
