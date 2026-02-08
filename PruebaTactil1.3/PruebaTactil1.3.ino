#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <TAMC_GT911.h>

// --- CONFIGURACIÓN DE HARDWARE (LOVYANGFX) ---
class LGFX_ESP32_35 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:
  LGFX_ESP32_35(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 27000000;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc   = 2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 15;
      cfg.pin_rst          = 25;
      cfg.panel_width      = 320; // Ancho físico
      cfg.panel_height     = 480; // Alto físico
      cfg.bus_shared       = true;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 27;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX_ESP32_35 tft;

// --- CONFIGURACIÓN TÁCTIL ---
#define I2C_SDA 33
#define I2C_SCL 32
TAMC_GT911 ts = TAMC_GT911(I2C_SDA, I2C_SCL, 21, 25, 320, 480); // Ajustado a vertical

// Variables de resolución vertical
static const uint32_t screenWidth  = 320;
static const uint32_t screenHeight = 480;
uint8_t *draw_buf;

// Objetos de la interfaz
lv_obj_t * ta; // Area de texto
lv_obj_t * kb; // Teclado

// Callback Pantalla
void my_disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.pushImage(area->x1, area->y1, w, h, (uint16_t*)px_map);
  lv_display_flush_ready(disp);
}

// Callback Táctil con Monitor de Coordenadas
void my_touchpad_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
  ts.read();
  if (ts.isTouched) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = ts.points[0].x;
    data->point.y = ts.points[0].y;
    
    // MONITOR SERIAL
    Serial.printf("Touch detectado -> X: %d | Y: %d\n", data->point.x, data->point.y);
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// Evento para el teclado (Alerta)
static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        Serial.println("Alerta: Botón numérico presionado");
    }
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0); // ROTACIÓN 0 = VERTICAL (320x480)
  tft.setBrightness(128);
  tft.fillScreen(TFT_BLACK);

  ts.begin();
  ts.setRotation(0); // Táctil en vertical

  lv_init();
  
  size_t buf_size = screenWidth * 40 * sizeof(lv_color_t); 
  draw_buf = (uint8_t *)malloc(buf_size);
  
  lv_display_t * disp = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(disp, my_disp_flush_cb);
  lv_display_set_buffers(disp, draw_buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read_cb);

  // --- INTERFAZ VERTICAL ---
  
  // 1. Crear el área de texto arriba
  ta = lv_textarea_create(lv_screen_active());
  lv_obj_set_size(ta, 280, 60);
  lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 20);
  lv_textarea_set_placeholder_text(ta, "Numero...");
  lv_textarea_set_one_line(ta, true);
  lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);

  // 2. Crear el teclado numérico
  kb = lv_keyboard_create(lv_screen_active());
  lv_obj_set_size(kb, 320, 240);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER); // Modo solo números
  lv_keyboard_set_textarea(kb, ta); // Vincular teclado al area de texto
  lv_obj_add_event_cb(kb, ta_event_cb, LV_EVENT_VALUE_CHANGED, NULL); // Alerta

  Serial.println("Teclado Vertical e Coordenadas Listos.");
}

void loop() {
  lv_tick_inc(5);
  lv_timer_handler();
  delay(5);
}