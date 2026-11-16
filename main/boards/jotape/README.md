# JOTAPE

Board propia basada en `waveshare/esp32-s3-touch-lcd-1.54`
(Waveshare ESP32-S3-Touch-LCD-1.54: ST7789 240x240, táctil CST816,
audio ES8311 + ES7210). El hardware y los pines son idénticos a la board
de Waveshare; ver `config.h`.

La IMU QMI8658 todavía no se inicializa.

## Interfaz JOTAPE

`JotapeDisplay` (`jotape_display.*`) reemplaza la pantalla estándar por una
interfaz estilo terminal retro (`jotape_ui.*`, solo LVGL):

| Pantalla | Cuándo aparece |
|---|---|
| Arranque | Al encender (2,5 s): solo la cabeza flotando |
| Terminal | Configuración Wi-Fi, activación, actualización, errores (con texto) |
| Inicio (reloj) | En espera: fecha, Wi-Fi, batería, hora y la cabeza asomando abajo |
| Funciones (menú) | Tocando la pantalla de inicio o deslizando a la izquierda |
| Escuchando | Cabeza flotando + onda de audio naranja |
| Hablando | Cabeza flotando + onda de audio verde, sin subtítulos |

La onda de audio es una animación: no sigue el volumen real del micrófono.

Desde el menú se vuelve deslizando a la derecha, tocando "HORA Y FECHA" o
esperando 20 s. Las demás funciones muestran "PRÓXIMAMENTE".

La cara de JOTAPE es un casco fijo (`jotape_helmet.c`, generado con IA y
convertido a RGB565 en 150, 112 y 60 px) con ojos dibujados encima en la pantalla
vacía del casco. Los ojos son barras/puntos animados (parpadean, miran a los
costados, laten al hablar) o formas A8 teñidas (`jotape_eyes.c`: ^ ^, > <,
corazones, estrellas, espirales, enojado, triste, lágrima), según la emoción
que manda el servidor.

Fuentes generadas (`font_jotape_*.c`): VT323 y Material Symbols, ambas con
licencia SIL OFL 1.1 (ver `fonts/`).

## Configuración fija (`sdkconfig.jotape`)

| Opción | Valor |
|---|---|
| Placa | `CONFIG_BOARD_TYPE_JOTAPE` |
| Idioma | Español (`CONFIG_LANGUAGE_ES_ES`) |
| Wake word | "Jotape" vía MultiNet7 en inglés (`"ho tah pay"`, umbral 10) |
| OTA | `https://api.tenclass.net/xiaozhi/ota/` (servidor oficial) |

## Compilar

Desde la raíz del repo, con ESP-IDF activado. La primera vez (o después
de cambiar `sdkconfig.jotape`) borrá el `sdkconfig` viejo:

```bash
rm -f sdkconfig
```

```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;main/boards/jotape/sdkconfig.jotape" set-target esp32s3
```

```bash
idf.py build flash monitor
```

`sdkconfig.defaults.esp32s3` se agrega solo (ESP-IDF suma
`<archivo>.esp32s3` por cada archivo de la lista).

No uses `scripts/build.py` para esta board: ese script reemplaza la wake
word por un modelo WakeNet de su lista fija y descarta "Jotape".

## Ajustar la wake word

- Si no reconoce "Jotape": bajá `CONFIG_CUSTOM_WAKE_WORD_THRESHOLD`
  (por ejemplo 10-15) o probá otra escritura (`"ho ta pe"`, `"hoe tah pay"`).
- Si se despierta solo: subí el umbral (25-35).
