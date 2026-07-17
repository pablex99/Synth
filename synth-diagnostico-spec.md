# Especificación: Entorno de Diagnóstico Hardware/Software — Proyecto Synth

> Entregar este archivo a Claude Code, posicionado en la raíz del proyecto:
> `C:\Users\terra\OneDrive\Documentos\PlatformIO\Projects\Synth`

## Prompt inicial sugerido para Claude Code

```
Lee este archivo (synth-diagnostico-spec.md) y luego:
1. Explora el proyecto completo (estructura de carpetas, platformio.ini, src/, lib/, include/, docs/, esquemáticos si existen).
2. Resume qué módulos de firmware encontraste y qué función de audio/control cumple cada uno.
3. Usa esa información para completar las secciones marcadas como [A COMPLETAR POR CLAUDE CODE] en este documento.
4. Luego construye el entorno de diagnóstico descrito en la Sección 4.
```

---

## 1. Objetivo

Crear una herramienta (puede ser una página HTML local, un script con interfaz simple, o documentación interactiva) que permita:

- Ver de un vistazo qué bloques de **hardware analógico** y qué módulos de **firmware (ESP32)** existen en el proyecto y cómo se conectan entre sí (señal y control).
- Ingresar un **síntoma audible** (lo que el usuario escucha) y recibir una lista priorizada de **causas probables**, cada una apuntando a:
  - el bloque de hardware específico a revisar (con valores de componentes si están documentados), y/o
  - el archivo/función de firmware específico a revisar.
- Servir como referencia viva que se actualice a medida que el proyecto cambie.

## 2. Mapa del sistema (a completar por Claude Code)

### 2.1 Bloques de hardware analógico

> Confirmado con el usuario (2026-06-21): este sintetizador es **100% digital** — toda la síntesis (osciladores, LFOs, filtro, reverb, delay) ocurre en firmware (DSP en el ESP32). No existen etapas VCO/VCF/VCA analógicas discretas en hardware. La única "etapa analógica" es el filtro RC interno de alimentación del PCM5102A y el propio filtro de salida integrado del DAC.

| Bloque | Función | Componentes clave | Señal in/out | Pines ESP32 asociados |
|---|---|---|---|---|
| **Fuente de alimentación** | Boost converter eleva a 5V; regulador lineal baja a 3.3V para lógica (ESP32, OLED, MUX, PCM5102A digital) | Convertidor Boost + regulador 3.3V (modelo no especificado) | DC in (batería/USB) → 5V → 3.3V | — |
| **Filtro RC de alimentación analógica del DAC** | Filtra el riel de 3.3V que alimenta el pin "Analog" del módulo PCM5102A, reduciendo ruido digital acoplado | Filtro RC (resistor + capacitor, valores no documentados) en la entrada del pin Analog 3.3V del módulo PCM5102A | 3.3V regulado (con ruido digital) → 3.3V filtrado | — |
| **DAC PCM5102A (módulo)** | Convierte el flujo I2S de 16-bit/44.1kHz a una señal de audio analógica estéreo; incluye su propio filtro de salida interno (sin etapas externas adicionales) | Módulo breakout PCM5102A | I2S in (BCK/WS/DIN) → salida de línea analógica estéreo (jack o pads) | GPIO26 (BCK), GPIO25 (WS/LRCK), GPIO22 (DIN) |
| **Potenciómetros (x12) + MUX CD4067** | Entrada de control analógica: 12 pots se multiplexan a un solo canal ADC | CD4067 (mux analógico 16:1, se usan 12 canales) | 12 señales analógicas 0–3.3V → 1 señal multiplexada al ADC | GPIO35 (SIG/ADC1), GPIO16/17/19/18 (S0–S3, selección de canal) |
| **Botones (x6)** | Entradas digitales de control (INPUT_PULLUP) | Pulsadores momentáneos | Digital, activo en bajo | GPIO27, 13, 4, 5, 32, 33 |
| **OLED SSD1306** | Display de control/diagnóstico visual (no forma parte de la cadena de audio) | Módulo SSD1306 128x64, I2C @ 0x3C | I2C (SDA/SCL) | GPIO21 (SDA), GPIO23 (SCL) |

**Nota para diagnóstico**: dado que no hay etapas analógicas discretas (VCO/VCF/VCA), la mayoría de los síntomas de audio (distorsión, aliasing, drift de pitch, clicks) deben diagnosticarse primero en **firmware** (Sección 2.2). El hardware solo puede introducir: ruido de alimentación/tierra acoplado al PCM5102A, fallas de conexión I2S, o saturación si el nivel de salida del DAC excede la entrada del amplificador/parlante aguas abajo.

### 2.2 Módulos de firmware (ESP32)

Todo el firmware vive en un único archivo: [src/main.cpp](src/main.cpp) (~1212 líneas). No hay separación en módulos/archivos adicionales todavía; las "secciones" abajo son funciones/bloques lógicos dentro de ese archivo.

| Sección/función | Archivo:línea aprox. | Responsabilidad | Periféricos ESP32 | Hardware relacionado |
|---|---|---|---|---|
| `setupI2S()` | [src/main.cpp:1042](src/main.cpp) | Configura I2S maestro, 16-bit estéreo @ 44.1kHz, 8 buffers DMA x 256 muestras | I2S_NUM_0 | DAC PCM5102A |
| `evalSirenCarrier()` | [src/main.cpp:754](src/main.cpp) | Genera el oscilador portador (sirena): 5 formas de onda (ORIGINAL, SIN, SQR, TRI, SAW) | — (matemática pura) | — |
| `nextLfoRaw()` / `evalWaveByPhase()` | [src/main.cpp:778](src/main.cpp), [src/main.cpp:737](src/main.cpp) | Generan las 5 formas de onda de LFO y avanzan su fase | — | — |
| Lógica de modulación de pitch (loop principal) | [src/main.cpp:~1126](src/main.cpp) | Aplica Pitch LFO a la frecuencia portadora, clamp 60–2800 Hz | — | — |
| Lógica de VCA / Volume LFO | [src/main.cpp:~1128-1135](src/main.cpp) | Aplica envolvente de amplitud y `OUTPUT_GAIN` (0.10) | — | — |
| `processFilter()` | [src/main.cpp:787](src/main.cpp) | Filtro morphing 1-polo LP↔HP, cutoff 280–6780 Hz, modulado por Filter LFO | — | — |
| `processReverb()` | [src/main.cpp:803](src/main.cpp) | Reverb tipo Schroeder, 2 líneas de delay (31.8ms / 43.1ms) | — | — |
| `processDelay()` | [src/main.cpp:817](src/main.cpp) | Delay con feedback, buffer circular de hasta ~272ms (reducido desde 371ms al agregar WiFi, ver Sección 4) | — | — |
| `processDcBlock()` | [src/main.cpp:378](src/main.cpp) | Filtro de bloqueo DC antes de la salida final | — | — |
| `updateControls()` | [src/main.cpp:842](src/main.cpp) | Lee los 12 potenciómetros vía MUX, actualiza parámetros, detecta botones | ADC1 (GPIO35), GPIO16/17/19/18 (MUX select) | Pots + CD4067 |
| `selectMuxChannel()` / `readMuxRaw()` | [src/main.cpp:365](src/main.cpp), [src/main.cpp:372](src/main.cpp) | Selecciona canal del MUX y lee su valor ADC crudo | ADC1, GPIO16-19 | CD4067 |
| `updateButtonPressed()` | [src/main.cpp:345](src/main.cpp) | Debounce de botones (40ms) | GPIO 4/5/13/27/32/33 | Botones |
| `drawDisplay()` / `drawScopePage()` | [src/main.cpp:948](src/main.cpp), [src/main.cpp:539](src/main.cpp) | Renderiza las 7 páginas del OLED (control + osciloscopio) | I2C (Wire) | OLED SSD1306 |
| `pushScopeSample()` | [src/main.cpp:396](src/main.cpp) | Captura muestras de audio para el osciloscopio en pantalla (OLED, 1 canal) | — | — |
| `pushMultiScopeSample()` / `writeMultiScopeFrame()` | [src/main.cpp](src/main.cpp) | Osciloscopio multicanal: graba en paralelo 5 puntos de la cadena de audio en un buffer circular y los vuelca (binario, protocolo `SCP1`) bajo demanda, para inspección con zoom/pan en la pestaña "Osciloscopio en vivo" de [diagnostico/index.html](diagnostico/index.html) | UART (Serial @ 921600) | — |
| `setupWiFiScope()` / `handleWifiScopeClients()` | [src/main.cpp](src/main.cpp) | Servidor HTTP mínimo (`WiFiServer`) que expone `GET /scope` devolviendo el mismo frame `SCP1` por WiFi, para diagnóstico remoto sin USB. Deshabilitado si `WIFI_SSID` no está configurado | WiFi (modo estación) | — |
| Loop principal (síntesis por muestra) | [src/main.cpp:~1100-1190](src/main.cpp) | Orquesta la cadena completa por muestra: oscilador → VCA → filtro → reverb → delay → DC block → clamp → int16 → I2S write; también alimenta el osciloscopio multicanal en cada etapa | I2S_NUM_0 | DAC PCM5102A |

### 2.3 Mapa de interacción

**Cadena de audio (por muestra, 44.1kHz):**

```
Sweep LFO (0.85Hz, interno) ─┐
                              ▼
Pitch LFO (pot I0/I1) ──► evalSirenCarrier() [oscilador portador, 5 formas]
                              │
                              ▼
Volume LFO (pot I2/I3) ──► VCA (OUTPUT_GAIN 0.10)
                              │
                              ▼
Filter LFO (pot I5/I6) ──► processFilter() [morph LP↔HP, base: pot I4]
                              │
                              ▼
Reverb LFO ────────────► processReverb() [mix: pot I7, decay: pot I8]
                              │
                              ▼
Delay LFO ─────────────► processDelay() [time: pot I9, fb: pot I10, mix: pot I11]
                              │
                              ▼
                        processDcBlock()
                              │
                              ▼
                  clamp[-1,1] → int16_t → i2s_write()
                              │
                              ▼
            I2S (GPIO26 BCK / GPIO25 WS / GPIO22 DIN)
                              │
                              ▼
                  Filtro RC alimentación 3.3V Analog
                              │
                              ▼
                    DAC PCM5102A (filtro de salida interno)
                              │
                              ▼
                   Salida de línea analógica estéreo
```

**Cadena de control:**

```
12 Potenciómetros ──► CD4067 MUX (GPIO16/17/19/18 = S0-S3)
                              │
                              ▼
                    GPIO35 (ADC1, lectura multiplexada)
                              │
                              ▼
                      updateControls() ──► actualiza parámetros de cada LFO/efecto

6 Botones (GPIO4/5/13/27/32/33) ──► updateButtonPressed() ──► cambia forma de onda /
                                                                 página OLED / activa LFO

drawDisplay() / drawScopePage() ──► I2C (GPIO21 SDA / GPIO23 SCL) ──► OLED SSD1306
```

**Alimentación:**

```
Fuente DC ──► Boost converter ──► 5V ──► Regulador lineal ──► 3.3V ──┬──► ESP32 (lógica)
                                                                        ├──► OLED, MUX, pots
                                                                        └──► Filtro RC ──► PCM5102A (pin Analog)
```

---

## 3. Árbol de diagnóstico por síntoma

Esta es la estructura lógica que debe alimentar el entorno de diagnóstico. Claude Code debe usar esta plantilla y completarla cruzando cada síntoma con los bloques reales encontrados en la Sección 2.

| Síntoma audible | Causas probables (orden de probabilidad) | Bloque de hardware a revisar | Módulo/función de firmware a revisar |
|---|---|---|---|
| Distorsión / clipping | Nivel de señal saturado antes del DAC; ganancia excesiva en etapa analógica; offset DC mal calibrado | Etapa de salida, VCA, alimentación | Función de generación de envolvente/ganancia, escalado de samples |
| Ruido de fondo constante (hiss) | Mala referencia de tierra analógica/digital; desacoplo insuficiente en alimentación; PWM mal filtrado | Filtro de salida, alimentación, plano de tierra | Configuración de PWM/DAC, frecuencia de muestreo |
| Zumbido periódico (hum 50/60Hz) | Lazo de tierra; alimentación no regulada/no filtrada | Fuente de alimentación, blindaje | — (normalmente no es firmware) |
| Aliasing / artefactos metálicos en tonos agudos | Frecuencia de muestreo insuficiente; falta de filtro anti-aliasing; mal oversampling | Filtro anti-aliasing analógico | Tasa de muestreo, tabla de ondas, interpolación |
| Clicks/pops al cambiar de nota o parámetro | Cambios abruptos sin interpolación; interrupciones bloqueantes | — | Manejo de interrupciones, suavizado de parámetros (smoothing/ramping) |
| Desafinación o drift de pitch | Componentes analógicos sensibles a temperatura (VCO); reloj/timer del ESP32 impreciso | VCO, referencia de voltaje | Timer/clock usado para generación de frecuencia |
| Audio intermitente / se corta | Buffer underrun; tarea de audio interrumpida por otra tarea; problema de alimentación | Alimentación, conexión física | Prioridad de tareas (FreeRTOS), tamaño de buffer I2S |
| Canal/voz no responde | Pin mal configurado; etapa analógica desconectada o componente dañado | Etapa específica de esa voz/canal | Inicialización de pines, lógica de esa voz |

**El árbol de diagnóstico completo y específico de este proyecto vive en [diagnostico/data.json](diagnostico/data.json)** (campo `sintomas`), con 10 síntomas cruzados contra los bloques reales de hardware y firmware (ver Sección 2). La tabla genérica de arriba se mantiene como referencia/plantilla; **edita `data.json`, no esta tabla**, para que el diagnóstico interactivo (Sección 4) se mantenga actualizado.

---

## 4. Entorno de diagnóstico a construir

### Requisitos funcionales
1. **Vista de mapa del sistema**: diagrama simple (puede ser texto estructurado, ASCII, o un HTML con boxes) mostrando los bloques de hardware y software y sus conexiones (a partir de la Sección 2).
2. **Buscador de síntomas**: el usuario selecciona o escribe un síntoma audible, y la herramienta muestra:
   - Causas probables ordenadas
   - Bloque de hardware a inspeccionar
   - Archivo/función de firmware a revisar (con link/ruta directa si es posible)
3. **Acceso a archivos del proyecto**: la herramienta debe poder leer la carpeta del proyecto para mantenerse actualizada (no hardcodear todo de forma estática si es evitable) — Claude Code decide la mejor implementación dado el entorno (ej. script Python local con una interfaz simple, o HTML que se regenera cada vez que se le pide actualizar).
4. **Extensible**: debe ser fácil agregar nuevos síntomas o bloques a medida que el proyecto crece.

### Formato sugerido
- Si se prioriza simplicidad: un único archivo HTML local (sin dependencias de red) con la tabla de diagnóstico y el mapa, generado a partir de los datos reales del proyecto.
- Si se prioriza que se mantenga sincronizado con el código: un script que escanee el proyecto y regenere el HTML/reporte cada vez que el usuario lo pida.

Claude Code debe decidir la implementación más simple y mantenible, pero siempre debe:
- Guardar el árbol de diagnóstico como un archivo de datos separado (ej. JSON o YAML) para que sea fácil de editar a mano sin tocar código.
- Dejar documentado en un README cómo actualizar el mapa cuando se modifique el hardware o el firmware.

### Implementación construida

Se eligió **HTML local + JSON separado**, sin dependencias de red ni build step:

- [diagnostico/index.html](diagnostico/index.html) — interfaz: buscador de síntomas con chips sugeridos + vista de mapa del sistema (tablas de hardware/firmware + diagrama de flujo). Abrir directo en el navegador, no requiere servidor.
- [diagnostico/data.json](diagnostico/data.json) — fuente de datos editable a mano: bloques de hardware, módulos de firmware y árbol de síntomas (10 síntomas documentados, cruzados con los bloques reales de la Sección 2).
- [diagnostico/README.md](diagnostico/README.md) — instrucciones para agregar/editar síntomas y bloques sin tocar el HTML/JS.

Se descartó el script que escanea el proyecto automáticamente: con un solo archivo fuente (`src/main.cpp`, ~1200 líneas) y un hardware simple, mantener `data.json` a mano es más simple y menos frágil que un parser.

---

## 5. Notas para Claude Code

- Si faltan datos de hardware (esquemáticos no digitalizados, BOM incompleta), pedir al usuario que los complete antes de continuar con la Sección 2.1, en vez de inventar valores de componentes.
- No asumir topología de circuito; preguntar si hay ambigüedad.
- Priorizar que el sistema sea fácil de actualizar por el usuario sin programar, ya que el árbol de diagnóstico crecerá con el uso.
