// PRUEBA TACTIL CAMBIO DE COLOR BACKGROUND BLACK AND WHITE
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <TAMC_GT911.h>

// --- CONFIGURACIÓN DE HARDWARE PERSONALIZADA ---
class LGFX_ESP32_35 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance; // Estructura para la luz

public:
  LGFX_ESP32_35(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
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
      cfg.pin_rst          = 25; // Reset de pantalla
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;
      cfg.bus_shared       = true;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 27; // PIN DE LUZ AQUÍ
      cfg.invert = false;
      cfg.freq   = 44100;
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

// --- VARIABLES LVGL ---
static const uint32_t screenWidth  = 480;
static const uint32_t screenHeight = 320;
uint8_t *draw_buf;
bool fondo_blanco = false;

void my_disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
  if (tft.getStartCount() == 0) tft.startWrite();
  tft.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (uint16_t*)px_map);
  lv_display_flush_ready(disp);
}

void my_touchpad_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
  ts.read();
  if (ts.isTouched) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = ts.points[0].x;
    data->point.y = ts.points[0].y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void btn_event_cb(lv_event_t * e) {
  fondo_blanco = !fondo_blanco;
  lv_obj_set_style_bg_color(lv_screen_active(), fondo_blanco ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
  Serial.println(fondo_blanco ? "Fondo Blanco" : "Fondo Negro");
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  tft.setBrightness(128); // Encender luz al 50%
  tft.fillScreen(TFT_BLACK);

  ts.begin();
  ts.setRotation(1);

  lv_init();
  draw_buf = (uint8_t *)malloc(screenWidth * screenHeight / 10 * 2);
  
  lv_display_t * disp = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(disp, my_disp_flush_cb);
  lv_display_set_buffers(disp, draw_buf, NULL, screenWidth * screenHeight / 10 * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read_cb);

  // Crear Interfaz
  lv_obj_t * btn = lv_button_create(lv_screen_active());
  lv_obj_set_size(btn, 220, 80);
  lv_obj_center(btn);
  lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * label = lv_label_create(btn);
  lv_label_set_text(label, "CAMBIAR COLOR");
  lv_obj_center(label);

  Serial.println("LovyanGFX iniciado correctamente.");
}

void loop() {
  lv_tick_inc(5);
  lv_timer_handler();
  delay(5);
}