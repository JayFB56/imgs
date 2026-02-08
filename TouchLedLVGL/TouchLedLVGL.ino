#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <TAMC_GT911.h>

#define TFT_BL 27
#define LED_PLACA 4  

#define I2C_SDA 33
#define I2C_SCL 32
#define RST_PIN 25
#define INT_PIN -1 

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 320

TFT_eSPI tft = TFT_eSPI();
TAMC_GT911 ts = TAMC_GT911(I2C_SDA, I2C_SCL, INT_PIN, RST_PIN, SCREEN_WIDTH, SCREEN_HEIGHT);

uint8_t *draw_buf;
lv_obj_t *canvas;
lv_draw_line_dsc_t line_dsc;
lv_point_precise_t last_point = {0, 0};

// CALLBACK DE PANTALLA
void my_disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels((uint16_t *)px_map, w * h); 
    tft.endWrite();
    lv_display_flush_ready(disp);
}

// CALLBACK DEL TÁCTIL
void my_touchpad_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    ts.read();
    if (ts.isTouched) {
        digitalWrite(LED_PLACA, LOW); 
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = ts.points[0].x;
        data->point.y = ts.points[0].y;
        
        lv_point_precise_t current_point = { (lv_value_precise_t)data->point.x, (lv_value_precise_t)data->point.y };
        
        if(last_point.x != 0 || last_point.y != 0) {
            // Asignamos puntos al descriptor antes de dibujar
            line_dsc.p1 = last_point;
            line_dsc.p2 = current_point;

            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);
            lv_draw_line(&layer, &line_dsc);
            lv_canvas_finish_layer(canvas, &layer);
        }
        last_point = current_point;

    } else {
        digitalWrite(LED_PLACA, HIGH); 
        data->state = LV_INDEV_STATE_RELEASED;
        last_point.x = 0; last_point.y = 0;
    }
}

void setup() {
    Serial.begin(115200);
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH); 
    pinMode(LED_PLACA, OUTPUT);

    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK); 

    ts.begin();
    ts.setRotation(1);

    lv_init();

    uint32_t buf_size = SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8);
    draw_buf = (uint8_t *)malloc(buf_size);
    
    lv_display_t * disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, my_disp_flush_cb);
    lv_display_set_buffers(disp, draw_buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read_cb);

    // Crear Canvas
    canvas = lv_canvas_create(lv_scr_act());
    uint32_t cbuf_sz = SCREEN_WIDTH * SCREEN_HEIGHT * 2; 
    uint8_t *cbuf = (uint8_t *)malloc(cbuf_sz);
    
    if(cbuf) {
        lv_canvas_set_buffer(canvas, cbuf, SCREEN_WIDTH, SCREEN_HEIGHT, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    }
    lv_obj_center(canvas);

    // Configurar el dibujo de línea (CORREGIDO PARA V9)
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0xFFFFFF);
    line_dsc.width = 4;
    line_dsc.round_start = true; // Antes round_cap
    line_dsc.round_end = true;   // Antes round_cap

    Serial.println("Tamos Listo");
}

void loop() {
    lv_tick_inc(5);
    lv_timer_handler();
    delay(5);
}