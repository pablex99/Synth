# Synth (PCM5102A + ESP32)

- Prueba de audio minima: reproduce una sirena dub estilo patch modular (LFO -> FM 
  de VCO cuadrado -> VCA -> delay). 
- Controles activos por MUX: I4 ajusta la frecuencia del LFO (0.0001 .. 50 Hz), I3 
  ajusta la profundidad de FM (-100 .. 300%), I2 ajusta la frecuencia base del VCO (50 .. 4000 Hz), I0/C0 ajusta el tiempo del delay, I6/C6 ajusta el feedback del delay e I5 ajusta el volumen final (0 .. 100% lineal, con techo de salida al 50% del nivel anterior).
- I1/C1 controla el tiempo de release del envelope aplicado al trigger manual.
- No usa OLED; valida que el hardware de audio I2S + PCM5102A, mas el MUX de 
  potenciometros, reproducen correctamente esa cadena de sintesis. Botones: rojo cambia forma de onda del LFO, blanco alterna modo AUTO/MANUAL, azul actua como trigger/gate en modo MANUAL y verde cambia el punto de inicio del LFO para el trigger manual.

## Pines I2S (igual que proyecto Synth)

- BCK: GPIO13
- WS/LRCK: GPIO27
- DIN: GPIO14

## Pines MUX (igual que proyecto Synth, se usan canales I0/C0-I6/C6)

- S0: GPIO16, S1: GPIO17, S2: GPIO19, S3: GPIO18
- SIG/OUT: GPIO35 (ADC)
- Nota: S0, S1, S2 y S3 son lineas de seleccion del CD4067, no canales de control.
- I0/C0 -> Tiempo del delay (0 .. 7000 ms)
- I1/C1 -> Release del envelope de trigger manual (1 .. 10000 ms)
- I2/C2 -> Frecuencia base del VCO (50 .. 4000 Hz)
- I3/C3 -> Profundidad FM del VCO (-100 .. 300%)
- I4/C4 -> Frecuencia del LFO (0.0001 .. 50 Hz)
- I5/C5 -> Volumen de salida (0 .. 100%, lineal)
- I6/C6 -> Feedback del delay (0 .. 100%)

## Botones

- Rojo (GPIO4, INPUT_PULLUP): cicla la forma de onda del LFO en este orden:
  - Diente de sierra descendente (arranca en pico y desciende linealmente).
  - Senoidal.
  - Diente de sierra invertida/ascendente (sube linealmente y cae de golpe al       
    reiniciar).
  - Cuadrada.
- Blanco (GPIO21, INPUT_PULLUP): alterna entre modo AUTO y modo MANUAL.
- Azul (GPIO22, INPUT_PULLUP): en modo MANUAL actua como compuerta/trigger (mantener
  presionado para que suene). En modo AUTO no afecta la salida.
- Verde (GPIO23, INPUT_PULLUP): cicla el punto de inicio del LFO usado al disparar  
  el trigger manual. Orden: cruce por cero ascendente -> maximo -> cruce por cero
  descendente -> minimo -> vuelve al inicio.

## Comportamiento

- Sirena dub con flujo tipo modular (como en VCV Rack): un LFO configurable modula 
  por FM la frecuencia de un VCO cuadrado.
- Controles tonales:
  - I0/C0 controla el tiempo del delay en 0 .. 7000 ms.
  - I1/C1 controla el release del envelope del trigger manual en 1 .. 10000 ms.
  - I2/C2 controla la frecuencia base del VCO en 50 .. 4000 Hz.
  - I3/C3 controla la profundidad de FM en -100 .. 300%.
  - I4/C4 controla la frecuencia del LFO en 0.0001 .. 50 Hz.
  - I5/C5 controla el nivel final del VCA en 0 .. 100%, con techo de salida al 50%
    del nivel maximo anterior.
  - I6/C6 controla el feedback del delay en 0 .. 100%.

- Trigger manual predecible:
  - En modo MANUAL, al presionar el boton azul, la fase del LFO se reinicia en el 
    punto de inicio seleccionado con el boton verde.
  - Esto hace que cada disparo comience de forma repetible (por ejemplo, seno 
    partiendo en cruce por cero ascendente por defecto).
  - En ondas no sinusoidales, los puntos de interes equivalentes se asignan de forma
    coherente para mantener comportamiento consistente entre formas.
  - El trigger manual usa envelope de ganancia: al presionar, el nivel sube a su
    valor maximo (segun I5) con attack = 1 ms; al soltar, cae gradualmente segun
    release variable por I1/C1 (1 .. 10000 ms).
  - Al salir de modo AUTO (pasar a MANUAL), el audio no se corta en seco: tambien
    entra en release y se desvanece segun I1/C1.

- Caracter analogico agregado respecto al oscilador digital original:
  - Ciclo de trabajo ligeramente asimetrico (45/55 en vez de 50/50), como un 
    astable real.
  - Oscilador cuadrado con correccion PolyBLEP (menos aliasing/aspereza digital).
  - Rolloff suave del VCO (simula el tiempo de subida/bajada de un transistor).
  - Saturacion suave (tipo transistor) en la salida final.
- Cadena de audio: salida cuadrada del VCO -> VCA (nivel lineal por I5) -> delay de 
  feedback con tiempo variable por I0/C0 (0 .. 7000 ms), feedback variable por I6/C6 (0 .. 100%) y 45% de mezcla fija, siempre activo, con cada repeat oscureciendose progresivamente (filtro paso-bajo en el lazo de feedback, como un delay analogico a BBD en vez de un repeat digital perfectamente limpio).
- Para habilitar hasta 7000 ms sin exceder la RAM del ESP32, el buffer interno de 
  delay se almacena a tasa reducida (aprox. 7.0 kHz), manteniendo el render final de audio en 44.1 kHz.
- El retorno wet del delay ademas se atenúa internamente (wet trim = 100%), 
  privilegiando una salida MIX claramente audible pero sin perder presencia de la señal dry.
- La realimentacion del delay se calcula desde la señal retardada antes del wet 
  trim de salida, para que el control de feedback tenga recorrido perceptible sin depender del nivel de mezcla.
- El oscurecimiento del lazo de feedback se ajusto para ser mas suave (alpha 0.05), 
  conservando mejor presencia en las repeticiones sin perder el caracter dub.
- Mitigaciones de ruido en la salida:
  - Lectura ADC endurecida (asentamiento + dummy reads + promedio + deadband).
  - Polling de controles desacoplado del audio y escaneo MUX round-robin.
  - Bloqueo de DC y filtro suave de salida para limpiar offset/aspereza residual.
  - Mayor colchon DMA de I2S para evitar artefactos por starvation.
  - I5 funciona como volumen maestro post-delay con remapeo del recorrido real de 
    la perilla (compensacion de offset ADC/MUX) y curva de ganancia suave tipo audio-taper, para un comportamiento organico de 0 .. 100% sin cortes abruptos.
- Sin OLED: la sirena suena en modo AUTO apenas se enciende el ESP32. En modo 
  MANUAL solo suena mientras se mantiene el boton azul. El timbre cambia por I0, I2, I3, I4, I5, I6 y la forma de onda seleccionada con el boton rojo.

## Glosario de audio y sintesis

- Sirena dub: timbre de sirena electronica con ecos marcados y repeticion ritmica,  
  tipico en dub/reggae y sintetizadores de performance.
- Sintesis modular: enfoque de diseno sonoro donde bloques (oscilador, modulador, 
  VCA, delay) se encadenan como modulos independientes.
- Cadena de senal: orden en que se procesa el audio. Aqui: VCO -> VCA -> delay -> 
  salida.
- Oscilador (VCO): generador periodico de audio cuya frecuencia determina el pitch 
  percibido.
- VCO (Voltage Controlled Oscillator): nombre clasico del oscilador principal; en 
  este firmware se controla digitalmente pero conserva la idea de oscilador de tono base.
- LFO (Low Frequency Oscillator): oscilador de baja frecuencia usado para modular 
  parametros, no para sonar como tono principal.
- FM (Frequency Modulation): modulacion de frecuencia; un oscilador altera 
  continuamente la frecuencia de otro, creando movimiento y riqueza timbrica.
- Profundidad de FM: cuanto influye el modulador (LFO) sobre la frecuencia del VCO. 
  A mayor profundidad, mayor barrido o agresividad.
- Frecuencia base: valor central del VCO antes de aplicar modulaciones.
- Forma de onda: geometria de un ciclo periodico (seno, sierra, cuadrada). Cambia 
  fuertemente el caracter del sonido.
- Senoidal: onda suave sin armonicos fuertes. Suele percibirse mas redonda o limpia.
- Diente de sierra ascendente/descendente: onda con rampa lineal y reinicio brusco; 
  rica en armonicos, percibida como mas brillante o agresiva.
- Cuadrada: alterna entre dos niveles. Tiene armonicos impares dominantes y timbre 
  mas hueco/duro.
- Fase: posicion dentro del ciclo de una onda. Reiniciar fase al trigger hace que 
  el ataque sea predecible.
- Trigger: evento instantaneo de disparo (inicio). En manual define cuando comienza 
  la accion audible.
- Gate (compuerta): estado sostenido on/off. Mientras gate esta abierto, el sonido 
  pasa.
- Modo AUTO/MANUAL: en AUTO el sonido corre continuamente; en MANUAL depende del 
  gate/trigger del usuario.
- VCA (Voltage Controlled Amplifier): etapa de ganancia/volumen. Controla nivel de 
  salida sin cambiar pitch.
- Ganancia: factor de amplificacion/atenuacion. En este proyecto I5 regula volumen 
  maestro post-delay.
- Audio-taper: curva no lineal de ganancia que se siente mas natural al oido que 
  una lineal pura.
- Delay: efecto de retardo temporal que repite el audio con cierto tiempo entre 
  repeticiones.
- Tiempo de delay: intervalo entre la senal original y su repeticion.
- Feedback (realimentacion): parte de la repeticion que vuelve a entrar al delay 
  para generar nuevas colas.
- Repeat: cada repeticion sucesiva creada por el delay.
- Mix dry/wet: proporcion entre senal directa (dry) y senal procesada (wet).
- Dry: senal original sin efecto.
- Wet: senal que sale del bloque de efecto (delay).
- Wet trim: atenuacion aplicada solo al camino wet para controlar presencia del 
  efecto.
- Delay MIX fijo: porcentaje wet/dry definido por firmware, no por perilla dedicada.
- Oscurecimiento del feedback: perdida progresiva de agudos en cada repeticion para 
  sonar menos digital.
- Filtro paso-bajo (LP): deja pasar bajas frecuencias y atenua altas; se usa para 
  suavizar repeticiones y salida.
- BBD (Bucket Brigade Device): tecnologia analogica clasica de delay con 
  degradacion/oscurecimiento caracteristica, emulada aqui de forma digital.
- Saturacion suave: compresion no lineal gradual (p. ej. tanh) que redondea picos y 
  agrega caracter sin clipping duro inmediato.
- Clipping: recorte por exceso de nivel. Puede ser deseado (distorsion) o no 
  deseado (artefactos).
- Duty cycle (ciclo de trabajo): porcentaje del periodo en estado alto de una onda 
  pulso/cuadrada.
- PolyBLEP: tecnica antialiasing para osciladores de onda abrupta; reduce aspereza 
  digital en transiciones.
- Aliasing: artefactos espectrales por contenido por encima de Nyquist o 
  discontinuidades no suavizadas.
- Sample rate (tasa de muestreo): cantidad de muestras por segundo. 44.1 kHz en 
  salida principal.
- Tasa interna de delay: frecuencia de muestreo del buffer de delay (menor que la 
  principal para ahorrar RAM).
- Buffer circular de delay: memoria que guarda muestras y se recorre en anillo para 
  leer/escribir retardo continuo.
- Latencia del delay: en este contexto, el tiempo seleccionado de retardo, no 
  latencia del sistema completo.
- DC offset: desplazamiento del centro de la onda respecto de cero; puede causar 
  ruidos o headroom perdido.
- DC block: filtro que elimina componente DC para centrar la senal alrededor de 
  cero.
- Suavizado de parametros: transicion gradual de valores de control para evitar 
  saltos audibles (zipper noise).
- Zipper noise: ruido de escalonado cuando un parametro cambia por pasos bruscos de 
  control digital.
- Deadband: zona de cambio minimo ignorado para evitar jitter por ruido en lectura 
  analogica.
- ADC (Analog to Digital Converter): convierte voltaje del pot en valor numerico.
- MUX CD4067: multiplexor analogico 16:1 que permite leer muchos controles con un 
  solo ADC.
- Round-robin de controles: lectura cíclica de un canal por tick para reducir carga 
  instantanea y ruido.
- I2S: interfaz digital serie para audio entre ESP32 y DAC PCM5102A.
- PCM5102A (DAC): convertidor digital-analogico que transforma el stream I2S en 
  audio analogico.
- DMA (Direct Memory Access): transfiere bloques de audio al periferico sin 
  bloquear CPU en cada muestra.
- Starvation de DMA: falta de datos a tiempo en el buffer de audio, que puede 
  causar clics o cortes.
- Headroom: margen antes de saturar. Ajustes de ganancia y mezcla buscan preservar 
  headroom util.
- Range de control: intervalo efectivo de un parametro asignado a una perilla (por 
  ejemplo Hz, ms, porcentaje).
- Compensacion de offset ADC/MUX: remapeo para que todo el recorrido fisico de la 
  perilla corresponda a un 0..100% util.

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
