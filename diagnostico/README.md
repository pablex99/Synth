# Entorno de diagnóstico — Synth

Herramienta local (HTML + JSON, sin dependencias externas) para diagnosticar síntomas de audio/control del proyecto Synth.

## Uso

Abre [index.html](index.html) en cualquier navegador (doble click o `start index.html` en Windows). No requiere servidor ni instalación.

> **Nota técnica**: los navegadores bloquean `fetch()` a archivos locales cuando se abre un HTML con `file://` (error "Failed to fetch"). Por eso los datos no se cargan directo desde `data.json`, sino desde [data.embed.js](data.embed.js), un archivo generado que contiene los mismos datos como variable JavaScript. **Después de editar `data.json` hay que regenerar `data.embed.js`** (ver abajo) para que los cambios se vean en `index.html`.

- **Buscar por síntoma**: escribe lo que escuchas (ej. "ruido", "clicks", "no responde") o haz click en uno de los chips sugeridos. Verás causas probables y los bloques de hardware (naranja) / firmware (verde) a revisar.
- **Mapa del sistema**: tablas con todos los bloques de hardware, módulos de firmware y el flujo de audio/control completo.
- **Esquemático**: diagrama pin a pin (tipo KiCad) con cada pin individual y cada cable entre ESP32, fuente, filtro RC, MUX+pots, botones, OLED y DAC, coloreados por categoría de señal (alimentación, I2S, I2C, MUX/ADC, botones, audio). Pasa el mouse sobre un bloque o un cable para ver su detalle.
- **Osciloscopio en vivo**: osciloscopio multicanal corriendo en el navegador, sin instalar nada aparte. Soporta dos modos: **USB** (Web Serial API, requiere cable) y **Red/WiFi** (sin cable, el ESP32 envía los datos por la red local). Ver detalle más abajo.

## Cómo actualizar

Todo el contenido vive en [data.json](data.json) — **no hay que tocar `index.html`** para agregar o corregir información.

Después de editar `data.json`, regenera `data.embed.js` ejecutando:

```powershell
python diagnostico/build.py
```

(o `py diagnostico/build.py` si `python` no está en el PATH). Luego recarga `index.html` en el navegador.

### Agregar un nuevo síntoma

Agrega un objeto al array `sintomas`:

```json
{
  "id": "id_unico",
  "nombre": "Nombre del síntoma tal como lo describiría el usuario",
  "causas": ["causa más probable", "causa menos probable", "..."],
  "hardware": ["id_de_bloque_hardware", "..."],
  "firmware": ["id_de_modulo_firmware", "..."]
}
```

Los `id` en `hardware`/`firmware` deben coincidir con los `id` definidos en `bloquesHardware` y `modulosFirmware`. Si no aplica ninguno, deja el array vacío `[]`.

### Agregar o modificar un bloque de hardware

Agrega/edita un objeto en `bloquesHardware` con: `id`, `nombre`, `funcion`, `componentes`, `senal`, `pines` (array de strings tipo `"GPIOxx (rol)"`).

### Agregar o modificar un módulo de firmware

Agrega/edita un objeto en `modulosFirmware` con: `id`, `nombre` (de la función), `archivo`, `linea`, `responsabilidad`, `perifericos` (array de strings).

Si el firmware se reorganiza en varios archivos (hoy todo está en `src/main.cpp`), actualiza el campo `archivo` de cada módulo afectado.

### Después de editar hardware (cambios físicos en la placa)

Si se agrega/quita una etapa analógica (ej. un buffer de salida, otra fuente), documenta el cambio en `bloquesHardware` y en [synth-diagnostico-spec.md](../synth-diagnostico-spec.md) sección 2.1, y revisa si algún síntoma en `sintomas` necesita actualizar sus causas o bloques asociados.

### Mover/agregar bloques y pines en el Esquemático

El esquemático es pin a pin, no bloque a bloque: cada cable conecta un pin específico de un bloque con un pin específico de otro.

- **`posicion`** en cada bloque de `bloquesHardware`: `{x, y, w, h}` (coordenadas/tamaño en un lienzo SVG). Si un bloque no tiene `posicion`, no aparece en "Esquemático" (sigue en las tablas de "Mapa del sistema").
- **`pinesEsquema`** en cada bloque: lista de pines con `{"id": "id_unico_del_pin", "texto": "etiqueta visible", "lado": "left"|"right"|"top"|"bottom"}`. El `lado` determina de qué borde del bloque sale el pin; el orden dentro del array determina su posición a lo largo de ese borde.
- **`conexiones`**: cada cable es `{"from": "id_bloque_origen", "fromPin": "id_pin_origen", "to": "id_bloque_destino", "toPin": "id_pin_destino", "label": "texto del cable", "categoria": "power|i2s|i2c|mux|buttons|audio"}`. Los `fromPin`/`toPin` deben coincidir con los `id` definidos en `pinesEsquema` del bloque correspondiente. La `categoria` define el color del cable y aparece en la leyenda (si usas una categoría nueva, agrégale color en `SCH_CATEGORY_COLORS` dentro de `index.html`).

Para agregar un pin nuevo a un bloque existente (ej. si agregas un componente con más señales), agrégalo a su `pinesEsquema` y crea la `conexion` correspondiente — no hace falta tocar el HTML/JS.

## Osciloscopio multicanal por extracción (navegador)

El firmware incluye un **osciloscopio multicanal real** que captura en paralelo 5 señales de la cadena de audio. La pestaña "Osciloscopio en vivo" de `index.html` es la única herramienta de visualización (no depende de ningún script externo) y corre directo en el navegador, en dos modos intercambiables: USB o Red/WiFi.

**Importante**: este NO es un modo "en vivo" que refresca el gráfico sin parar — eso se probó y generaba demasiado tráfico/carga, audible como ruido en el audio. En cambio, el flujo es por **extracción explícita**: el ESP32 solo se comunica con el navegador entre el momento que apretás "Extraer datos" y el momento que apretás "Detener extracción". Mostrar el gráfico o descargar el CSV es un paso aparte, posterior, 100% local (no vuelve a consultar al ESP32).

### Deshabilitar el WiFi temporalmente (para descartar que sea la causa de un problema)

En [src/main.cpp](../src/main.cpp), la constante `WIFI_SCOPE_FEATURE_ENABLED` (cerca del inicio del archivo) es un interruptor maestro: en `false`, el ESP32 no llama ni a `WiFi.mode()` ni a `WiFi.begin()` — cero actividad de radio, sin tocar ni borrar nada del resto del código del osciloscopio remoto. Útil para una prueba A/B rápida: subí el firmware con `false`, confirmá si el problema (audio, pantalla, lo que sea) desaparece, y así sabés si el WiFi es la causa antes de invertir tiempo en otra cosa. Para reactivarlo, volvé a poner `true` y subí de nuevo.

### Señales capturadas

| Canal | Punto de la cadena (ver [src/main.cpp](../src/main.cpp)) |
|---|---|
| Oscilador+VCA | Justo después de `evalSirenCarrier()` y aplicar ganancia/Volume LFO |
| Post-Filtro | Después de `processFilter()` (LP/HP morph) |
| Post-Reverb | Después de `processReverb()` |
| Post-Delay | Después de `processDelay()` |
| Salida final | Después de `processDcBlock()`, lo que realmente sale por I2S al PCM5102A |

El ESP32 graba continuamente estas 5 señales en un buffer circular en RAM (512 muestras por canal, ~11.6ms de historia a 44.1kHz) y, al recibir el byte `'D'` por Serial o una petición `GET /scope` por WiFi, vuelca todo el buffer en binario junto con un snapshot de los 12 potenciómetros y los 5 LFOs (rate, depth, forma de onda, on/off).

### Modo USB (Web Serial API)

- **Chrome o Edge de escritorio** (Web Serial API no está disponible en Firefox/Safari ni en navegadores móviles).
- El ESP32 conectado por USB, con el firmware actualizado ya cargado.
- **El puerto COM debe estar libre**: cierra `pio device monitor` u otra herramienta antes de extraer desde el navegador.
- El puerto se abre automáticamente al apretar **"Extraer datos"** (te pide elegir el puerto la primera vez) y se cierra automáticamente al apretar **"Detener extracción"** — así queda libre para otras herramientas el resto del tiempo.

### Modo Red (WiFi, sin USB)

No requiere tener el ESP32 conectado por cable a la PC en absoluto — solo que esté encendido (por su propia fuente) y conectado a la misma red WiFi que la PC.

1. Configura las credenciales WiFi en [src/main.cpp](../src/main.cpp), constantes `WIFI_SSID` y `WIFI_PASSWORD` (cerca del inicio del archivo). **Importante**: debe ser una red de **2.4GHz** — el ESP32 clásico no soporta 5GHz (si tu router separa las bandas, el SSID de 2.4GHz suele ser el mismo nombre sin sufijo "5G"). Vuelve a subir el firmware.
2. La conexión WiFi corre en segundo plano sin bloquear el arranque (audio y OLED funcionan desde el primer instante). Para ver el estado **sin necesidad de USB**, navegá con el botón blanco hasta la página **"WIFI / OSCILOSCOPIO"** del OLED (es una sección propia, separada de la pantalla de la sirena), donde aparece una de estas líneas:
   - `WiFi: conectando...` — todavía intentando asociarse (hasta 10s).
   - `WiFi: 192.168.1.XX` — conectado; esa IP es la que tenés que usar.
   - `WiFi: fallo conexion` — no logró conectarse en 10s (revisa SSID/password, y que sea la red de 2.4GHz).
   - `WiFi: off (solo USB)` — `WIFI_SSID` no está configurado en el firmware.
   - `Datos: TRANSMITIENDO` / `Datos: sin actividad` — indica si en este momento se está sirviendo una extracción al navegador.
3. En `index.html`, pestaña "Osciloscopio en vivo", selecciona **"Red (WiFi, sin USB)"** y escribe la IP del ESP32 antes de apretar "Extraer datos".

### Cómo usarlo: Extraer → Detener → Descargar/Graficar

1. Elegí el modo (USB o Red) y, si es Red, completá la IP.
2. Click en **"Extraer datos"**. El estado va mostrando capturas acumuladas, muestras totales y segundos transcurridos (ej. `Extrayendo... 42 capturas, 21504 muestras, 8.3s transcurridos`). No se dibuja ningún gráfico todavía — a propósito, para no sumarle ninguna carga extra al ESP32 mientras está extrayendo.
3. Click en **"Detener extracción"** cuando tengas suficiente data. Esto corta la comunicación con el ESP32 (y libera el puerto serie en modo USB).
4. Con los datos ya en el navegador, elegí:
   - **"Descargar CSV"**: genera y descarga un archivo `.csv` con una fila por muestra — columnas `frame`, `sample_index`, `t_ms`, `dt_ms`, `dy_per_div`, una por cada canal (`ch_Oscilador_VCA`, `ch_Post_Filtro`, etc.), los 12 potenciómetros (`pot_I0`..`pot_I11`) y los parámetros de cada LFO (`Pitch_enabled`, `Pitch_shape`, `Pitch_rateHz`, `Pitch_depth`, y lo mismo para Volume/Filter/Reverb/Delay) — todo lo necesario para analizar la extracción completa en Excel, pandas, etc. sin volver a tocar el ESP32.
   - **"Graficar"**: dibuja toda la extracción en el navegador (línea continua dentro de cada captura, con un corte visual donde hay un salto de tiempo real entre capturas) con los mismos controles de zoom/escala que antes: botón **"Eje activo: X/Y"** para elegir a cuál eje afectan la rueda del mouse y el arrastre, y dos **sliders logarítmicos** (rango x0,000001 a x1.000.000) para ajuste fino de escala en cada eje. "Restablecer vista" vuelve a ver la extracción completa. Los checkboxes muestran/ocultan cada señal.
   - **"Borrar datos extraídos"**: limpia la extracción actual de la memoria del navegador (por si querés empezar de cero sin descargar).
5. Cada nueva extracción ("Extraer datos" otra vez) reemplaza la anterior — descargá o graficá antes de extraer de nuevo si no querés perderla.

### Sobre `dt` y `dy` en el CSV

- `dt_ms`: tiempo entre muestras consecutivas dentro de una misma captura (`1000 / sampleRate`, constante mientras el `SAMPLE_RATE` del firmware no cambie — hoy 44100 Hz → `dt_ms ≈ 0.0227`).
- `dy_per_div`: la unidad de amplitud por división de grilla usada como referencia visual en el gráfico (`2 / 4 divisiones = 0.5`, en la escala normalizada -1..1 de las señales, sin zoom aplicado). Se incluye en cada fila para que el CSV sea autocontenido si lo cruzás con capturas de pantalla del gráfico.
- `t_ms` es el tiempo real transcurrido desde el inicio de la extracción (no asume muestreo continuo entre capturas — respeta los huecos reales de tiempo que hay entre una captura y la siguiente).

### Notas técnicas

- El baudrate de Serial está en **921600** para poder transferir cada captura (5 canales × 512 muestras × 2 bytes ≈ 5KB) rápido. Si usas `pio device monitor`, ya está configurado en `platformio.ini` (`monitor_speed = 921600`).
- Cada captura individual (durante la ventana de extracción) sigue bloqueando brevemente el loop de audio del ESP32 mientras transfiere ~5KB — es normal escuchar algún glitch puntual mientras se está extrayendo activamente. Por eso el modelo es de ventana acotada (extraer lo que necesites y detener), no continuo indefinidamente.
- El protocolo binario `SCP1` está documentado como comentario encima de `writeMultiScopeFrame()` en `src/main.cpp`.
- El servidor HTTP en el ESP32 (modo Red) es minimalista (`WiFiServer`/`WiFiClient` del core de Arduino, sin librerías externas): un único endpoint `GET /scope` que responde con el frame binario y cierra la conexión. Incluye `Access-Control-Allow-Origin: *` para que `index.html` (abierto como `file://`) pueda hacer `fetch()` sin bloqueos de CORS.
- Si `WIFI_SSID` queda en su valor placeholder (`"TU_RED_WIFI"`) o no se puede conectar en 10s, el firmware sigue funcionando normalmente pero el modo Red queda deshabilitado (solo USB/Serial disponible) — se ve reflejado tanto en el log de Serial como en la pantalla OLED.
- La conexión WiFi (`WiFi.begin()`) es **no bloqueante**: corre en segundo plano y se revisa en cada vuelta del loop principal (`updateWifiScopeState()`), así que no retrasa el arranque del audio ni del OLED.
- Si se agregan/quitan etapas en la cadena de audio (otro efecto, otra etapa de filtro, etc.), agregar el tap correspondiente: incrementar `MSCOPE_CHANNELS`, agregar su nombre en `mscopeChannelNames`, capturar el valor con una variable `stageX` en el loop de síntesis, y pasarlo a `pushMultiScopeSample()`.

### Si usás una fuente standalone débil (sin USB) y notás resets/cuelgues

El radio WiFi tira picos de corriente de ~300-500mA al asociarse/transmitir. Si tu fuente (Boost + regulador lineal) no puede sostener ese pico con suficiente velocidad, el riel de 3.3V puede caer por debajo del umbral de brownout del ESP32. El modelo de extracción acotada (en vez de polling continuo) ya reduce bastante la actividad sostenida del radio, pero si el problema persiste es un tema de **hardware de alimentación**, no de firmware:
- Agregar un capacitor de bulk de baja ESR (470–1000µF) lo más cerca posible del pin 3.3V del ESP32, además de los 100nF cerámicos habituales.
- Verificar que el regulador lineal después del Boost pueda entregar al menos 500-800mA con buena respuesta transitoria (muchos reguladores lineales baratos tipo AMS1117 son lentos ante picos de corriente).
- Considerar reemplazar la cadena Boost→regulador lineal por un solo conversor buck (step-down) directo desde una batería de mayor voltaje, que suele tener mejor respuesta transitoria y mayor eficiencia que la combinación actual.

## Por qué JSON + HTML estático

- **JSON separado del código**: se puede editar a mano sin tocar JavaScript/HTML, sin programar.
- **HTML sin build step**: abre directo en el navegador, no requiere Node/Python ni servidor.
- Si en el futuro se prefiere regenerar esto desde un script que escanee `src/main.cpp` automáticamente, `data.json` puede mantenerse como la fuente de verdad y generarse desde un parser — pero hoy se edita manualmente porque el proyecto es pequeño (un solo archivo fuente).
