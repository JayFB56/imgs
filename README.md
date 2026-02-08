# Guía breve: LVGL y pantalla ESP32 3.5" (480×320)

Objetivo: explicar qué configurar para usar LVGL con una pantalla táctil 3.5" 480×320 en un ESP32, qué archivos modificar y qué hacer dentro de las carpetas de librerías para ejecutar los `.ino` de prueba en este repositorio.

Requisitos
- Arduino IDE (o PlatformIO).
- Soporte ESP32 instalado (Espressif ESP32 board package en Board Manager).
- Librerías: `lvgl` (v7/v8), `TFT_eSPI` o `LovyanGFX` (para la pantalla), y librería de touch (ej. `XPT2046_Touchscreen`) según el controlador táctil.

Instalación rápida
1. En Arduino IDE: `Tools > Board > Boards Manager` → instalar "esp32 by Espressif Systems".
2. `Sketch > Include Library > Manage Libraries` → buscar `lvgl`, `TFT_eSPI` (o `LovyanGFX`), `XPT2046_Touchscreen` e instalarlas.
   - Alternativa: clonar los repos desde GitHub dentro de la carpeta `Arduino/libraries`.

Qué archivos hay que modificar (resumen)
- Configuración de la pantalla y pines: editar la configuración de la librería de la pantalla:
  - Si usas `TFT_eSPI`: editar `User_Setup.h` dentro de la carpeta de la librería `TFT_eSPI` (o crear un `User_Setup.h` personalizado y seleccionarlo). Ahí defines el driver (`#define ILI9488_DRIVER` u otro), resolución (`#define TFT_WIDTH 480` y `#define TFT_HEIGHT 320`) y los pines SPI/CS/DC/RST y el pin del touch CS/IRQ.
  - Si usas `LovyanGFX`: usar su `User_Setup.h` o crear un fichero de configuración en `LovyanGFX/src` según la documentación de la librería.
- Configuración LVGL: copiar `lv_conf_template.h` de la librería `lvgl` dentro de tu carpeta de proyecto y renombrarlo a `lv_conf.h`. Modifica al menos:
  ```c
  #define LV_HOR_RES_MAX 480
  #define LV_VER_RES_MAX 320
  ```
  y ajusta otros parámetros (pools de memoria, ticks, drivers) según ejemplo.
- Arduino sketch (`.ino`): el `.ino` que vas a subir (ejemplos en este repo) será el que inicialice LVGL y registre el driver de pantalla y touch. Normalmente no necesitas tocar la lógica de LVGL, solo asegurar que la inicialización usa las mismas constantes/pines que pusiste en las librerías.

Archivo a modificar en este proyecto (recomendación)
- Revisa los `.ino` dentro de las carpetas `PruebaTactil1.1/`, `PruebaTactil1.2/`, `PruebaTactil1.3/` y `TouchLedLVGL/`.
- Para hacer el "tester", editar el sketch que quieras usar (por ejemplo `TouchLedLVGL/TouchLedLVGL.ino`). Ahí puedes ajustar definiciones como velocidad SPI o llamadas de inicialización si las hay.

Ejemplo de cambios en `TFT_eSPI/User_Setup.h` (puntos clave)
- Selecciona driver:
  ```c
  #define ILI9488_DRIVER
  #define TFT_WIDTH 480
  #define TFT_HEIGHT 320
  ```
- Define pines (ejemplo para SPI hardware; ajusta según tu placa):
  ```c
  #define TFT_MISO 19
  #define TFT_MOSI 23
  #define TFT_SCLK 18
  #define TFT_CS   5
  #define TFT_DC   2
  #define TFT_RST  4
  // Touch (ejemplo XPT2046)
  #define TOUCH_CS 15
  #define TOUCH_IRQ 21
  ```
Ajusta estos números a los pines reales de tu módulo.

Ejemplo mínimo para `lv_conf.h` (colocar en el mismo folder que el sketch):
```c
#define LV_HOR_RES_MAX 480
#define LV_VER_RES_MAX 320
#define LV_MEM_SIZE (32U * 1024U)
// Resto: copiar y ajustar el `lv_conf_template.h` según ejemplos de la librería.
```

Inicialización en el `.ino` (concepto)
- `lv_init();`
- Inicializar la pantalla (llamando a la librería `TFT_eSPI` o `LovyanGFX`).
- Crear buffer de dibujo para LVGL y registrar `lv_disp_drv_t`.
- Inicializar el controlador táctil y registrar `lv_indev_drv_t` para entrada táctil.

Subir el sketch (pasos)
1. En Arduino IDE: `Tools > Board > ESP32 Dev Module` (o la placa correcta).
2. Seleccionar puerto COM correcto (`Tools > Port`).
3. Pulsar `Upload`.
4. Abrir `Serial Monitor` si el sketch imprime mensajes (para calibración o debugging).

Consejos y solución de problemas
- Asegúrate de que las definiciones de pines en la librería y en el `.ino` coincidan.
- Si la pantalla queda en blanco: probar diferentes velocidades SPI o comprobar el `#define` del controlador (ILI9488 vs ILI9341, etc.).
- Para el touch: verifica la librería del controlador táctil y los pines CS/IRQ.
- Si LVGL no respeta la resolución, confirma que `lv_conf.h` está siendo realmente incluido por el sketch (ponerlo junto al `.ino` es la forma más directa).

Ejemplo rápido de flujo de trabajo
- Instala librerías.
- Edita `TFT_eSPI/User_Setup.h` (driver, resolución, pines).
- Copia y edita `lv_conf_template.h` → `lv_conf.h` en la carpeta del sketch con 480×320.
- Abre `TouchLedLVGL/TouchLedLVGL.ino` (o el `PruebaTactil` que prefieras), ajusta si hay defines de pines, compila y sube.

¿Quieres que haga esto por ti?
- Puedo: 1) editar un ejemplo de `User_Setup.h` con pines por defecto, 2) generar un `lv_conf.h` mínimo y colocarlo en la carpeta del sketch elegido, 3) modificar el `.ino` seleccionado para que use esa configuración. Indícame cuál `.ino` quieres usar como tester.

---
Archivo creado: `README-ESP32-LVGL.md` (este documento).