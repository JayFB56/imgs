#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <TAMC_GT911.h>

// --- CONFIGURACIÓN DE HARDWARE ---
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
      cfg.freq_write = 27000000; // Bajado para estabilidad
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
      cfg.panel_width      = 320; // Resolución nativa vertical
      cfg.panel_height     = 480;
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
TAMC_GT911 ts = TAMC_GT911(I2C_SDA, I2C_SCL, 21, 25, 480, 320);

static const uint32_t screenWidth  = 480;
static const uint32_t screenHeight = 320;
uint8_t *draw_buf;
bool fondo_blanco = false;

// Callback Pantalla
void my_disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.pushImage(area->x1, area->y1, w, h, (uint16_t*)px_map);
  lv_display_flush_ready(disp);
}

// Callback Táctil con MONITOR DE COORDENADAS
void my_touchpad_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
  ts.read();
  if (ts.isTouched) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = ts.points[0].x;
    data->point.y = ts.points[0].y;
    
    // MONITOR SERIAL DE COORDENADAS
    Serial.print("Touch X: "); Serial.print(data->point.x);
    Serial.print(" | Y: "); Serial.println(data->point.y);
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void btn_event_cb(lv_event_t * e) {
  fondo_blanco = !fondo_blanco;
  lv_obj_set_style_bg_color(lv_screen_active(), fondo_blanco ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1); // Paisaje
  tft.setBrightness(128);
  tft.fillScreen(TFT_BLACK);

  ts.begin();
  ts.setRotation(1);

  lv_init();
  
  // Aumentar buffer para fluidez
  size_t buf_size = screenWidth * 64 * sizeof(lv_color_t); 
  draw_buf = (uint8_t *)malloc(buf_size);
  
  lv_display_t * disp = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(disp, my_disp_flush_cb);
  lv_display_set_buffers(disp, draw_buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read_cb);

  // --- ARREGLO DE POSICIÓN ---
  lv_obj_t * scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  
  // Forzamos a que el "escritorio" de LVGL sea del tamaño correcto
  lv_obj_set_size(scr, screenWidth, screenHeight);

  lv_obj_t * btn = lv_button_create(scr);
  lv_obj_set_size(btn, 240, 90);
  
  // Usamos alineación explícita en lugar de center directo para asegurar posición
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0); 

  // Estilo visual mejorado
  lv_obj_set_style_radius(btn, 20, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x0055ff), 0);
  lv_obj_set_style_border_width(btn, 3, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);

  lv_obj_t * label = lv_label_create(btn);
  lv_label_set_text(label, "CAMBIAR COLOR");
  lv_obj_center(label);

  Serial.println("Monitor de coordenadas activo...");
}

void loop() {
  lv_tick_inc(5);
  lv_timer_handler();
  delay(5);
}