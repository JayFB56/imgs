#include <TFT_eSPI.h> // Librería para la pantalla
#include "initGT911.h" // Librería para el touch GT911
#include <Arduino_JSON.h>
#include <Preferences.h>
#include <Wire.h>
#include "RTClib.h"
#include "HX711.h"
#include <vector>
#include <lvgl.h>
#include <TAMC_GT911.h>

// ============================
// CONFIGURACIÓN HARDWARE
// ============================

// --- Configuración de Pines I2C ---
#define I2C_SDA 33
#define I2C_SCL 32
#define I2C_FREQ 400000

// --- Pantalla TFT ---
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();

// --- Touch GT911 (TAMC_GT911) ---
#include <TAMC_GT911.h>

// Pines típicos ESP32-3248S035C (verifica si tu placa usa otros)
#define TOUCH_SDA 21
#define TOUCH_SCL 22
#define TOUCH_INT 39
#define TOUCH_RST 38

TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST);

// --- Pantalla ---
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 480
#define ROTATION 1

// --- RTC DS3231 ---
RTC_DS3231 rtc;

// --- HX711 (Balanza) ---
const int LOADCELL_DOUT_PIN = 2;
const int LOADCELL_SCK_PIN = 4;
HX711 scale;

// Configuración calibración
float factor_calibracion = -435000.0;
bool modoCalibracion = false;
float pesoPatronCalibracion = 1000.0; // 1 kg en gramos

// Control de lectura HX711
bool leerBalanza = false;

// --- LEDs ---
#define LED_INDICADOR 17  // LED para éxito/operaciones normales
#define LED_ERROR 16      // LED para errores/borrado total

// --- Buzzer Pasivo ---
#define BUZZER_PIN 23

// --- Memoria Flash ---
Preferences preferences;
#define MAX_REGISTROS 500

// ============================
// ESTRUCTURAS DE DATOS
// ============================

enum TipoPesaje {
  PESAJE_AUTOMATICO,
  PESAJE_MATUTINO,
  PESAJE_VESPERTINO
};

struct RegistroPeso {
  int codigo_vaca;
  float peso_lb;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint32_t timestamp;
  bool editado;
  TipoPesaje tipo_pesaje;
};

std::vector<RegistroPeso> registros;

// ============================
// ESTADOS DEL SISTEMA
// ============================

enum EstadoSistema {
  SISTEMA_PRINCIPAL,
  INGRESO_CODIGO,
  SELECCION_TURNO,
  ESPERANDO_PESO,
  CONTANDO_3_SEGUNDOS,
  PESO_REGISTRADO,
  VISUALIZAR_REGISTROS,
  EDITAR_CODIGO,
  EDITANDO_PESO_AUTO,
  CONFIRMAR_BORRAR_TOTAL,
  REGISTRO_DUPLICADO,
  MENU_TIPO_PESAJE,
  EDITAR_TIPO_PESAJE,
  ERROR_BALANZA,
  MODO_CALIBRACION,
  EDITANDO_REGISTRO
};

EstadoSistema estadoActual = SISTEMA_PRINCIPAL;
String entradaCodigo = "";
int indiceRegistroActual = 0;
bool rtcConectado = false;
bool balanzaInicializada = false;
TipoPesaje tipoPesajeSeleccionado = PESAJE_AUTOMATICO;

// Variables para pesado automático
bool objetoDetectado = false;
bool editandoPesoAuto = false;
unsigned long tiempoDeteccion = 0;
const unsigned long TIEMPO_ESPERA = 3000;
float ultimoPesoLeido = 0.0;
float pesoARegistrar = 0.0;
int codigoActual = 0;

// Variables para navegación en pantalla
int desplazamientoPantalla = 0;
const int LINEAS_POR_PAGINA = 8;

// Variables para conteo de 3 segundos
unsigned long tiempoInicioConteo = 0;
int segundosRestantes = 3;

// Variables para touch
bool touchPressed = false;
GTPoint lastTouchPoint;

// ============================
// DECLARACIONES DE FUNCIONES
// ============================

void inicializarHardware();
void inicializarRTC();
void inicializarBalanza();
void inicializarLEDs();
void inicializarPantallaTouch();
void sonidoInicio();
void sonidoExito();
void sonidoError();
void sonidoBeep();
void encenderLED(int pin, int tiempo = 500);
float gramosALibras(float gramos);
float librasAGramos(float libras);
String obtenerFechaHora();
String obtenerFechaActual();
String obtenerTipoPesajeTexto(TipoPesaje tipo);
void actualizarPantalla();
void mostrarMenuPrincipal();
void mostrarIngresoCodigo();
void mostrarSeleccionTurno();
void mostrarEsperandoPeso();
void mostrarContando3Segundos();
void mostrarPesoRegistrado();
void mostrarRegistros();
void mostrarEditarCodigo();
void mostrarEditandoPesoAuto();
void mostrarConfirmarBorrarTotal();
void mostrarRegistroDuplicado();
void mostrarMenuTipoPesaje();
void mostrarEditarTipoPesaje();
void mostrarErrorBalanza();
void mostrarModoCalibracion();
float leerPeso();
void iniciarLecturaBalanza();
void detenerLecturaBalanza();
void procesarDeteccionPeso();
void procesarConteo3Segundos();
void registrarPesoAutomatico();
void procesarEdicionPesoAuto();
void cargarRegistros();
void guardarRegistros();
void guardarTipoPesaje();
bool esRegistroAntiguo(RegistroPeso reg);
bool esDuplicado(int codigo, TipoPesaje turno, uint16_t year, uint8_t month, uint8_t day);
void ajustarTurnoDuplicado(int codigo, TipoPesaje nuevoTurno, uint16_t year, uint8_t month, uint8_t day);
void limpiarRegistrosAntiguos();
void borrarTodosRegistros();
void procesarTouch();
void calibrarBalanza();
TipoPesaje determinarTurnoAutomatico();
void dibujarBoton(int x, int y, int w, int h, String texto, uint16_t color = TFT_DARKGREEN);
void dibujarTecladoNumerico();
void dibujarTeclasMenu();
char detectarTeclaNumerica(int x, int y);
char detectarTeclaMenu(int x, int y);
void procesarTecla(char tecla);

// ============================
// FUNCIONES DE INICIALIZACIÓN
// ============================

void inicializarHardware() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== INICIANDO SISTEMA BALANZA LECHERA ===");

  preferences.begin("balanza", false);
  tipoPesajeSeleccionado = (TipoPesaje)preferences.getUChar(
    "tipo_pesaje", 
    PESAJE_AUTOMATICO
  );

  // Inicializar LEDs
  inicializarLEDs();
  
  
  // Inicializar Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Inicializar I2C
  // Inicializar pantalla
  tft.init();
  tft.setRotation(ROTATION);
  tft.fillScreen(TFT_BLACK);

  // Inicializar touch GT911 (TAMC)
  touch.begin();
  touch.setRotation(ROTATION);
  
  // Inicializar Pantalla Touch
  inicializarPantallaTouch();
  
  // Inicializar RTC
  inicializarRTC();
  
  // Inicializar Balanza
  inicializarBalanza();
  
  // Cargar registros desde memoria
  preferences.begin("balanza", false);
  cargarRegistros();
  
  // Sonido de inicio
  sonidoInicio();
  
  // Mostrar pantalla de inicio
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("SISTEMA BALANZA");
  tft.println("LECHERA");
  tft.setTextSize(1);
  tft.println("======================");
  tft.print("RTC: ");
  tft.println(rtcConectado ? "CONECTADO" : "NO CONECTADO");
  tft.print("BALANZA: ");
  tft.println(balanzaInicializada ? "OK" : "ERROR");
  tft.print("REGISTROS: ");
  tft.println(registros.size());
  tft.print("TOUCH: ");
  tft.println("INICIADO");
  
  delay(2000);
}

void inicializarPantallaTouch() {
  // Iniciar Pantalla TFT
  tft.init();
  tft.setRotation(ROTATION);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  
  Serial.println("Iniciando pantalla y touch...");
  
  // Iniciar GT911 en modo polling
  if (Touchscreen.begin(INT_PIN, RST_PIN, I2C_FREQ)) {
    Serial.println("GT911 inicializado correctamente");
    // Ajusta a la resolución de tu pantalla
    Touchscreen.setupDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, initGT911::Rotate::_0); 
  } else {
    Serial.println("ERROR: No se pudo encontrar el controlador GT911");
    tft.setTextSize(2);
    tft.setCursor(10, 100);
    tft.println("ERROR TOUCH");
    tft.setTextSize(1);
    tft.println("Verificar conexiones");
    while(1);
  }
}

void inicializarRTC() {
  Serial.println("Inicializando RTC...");

  if (rtc.begin()) {
    rtcConectado = true;
    Serial.println("RTC: Conectado correctamente");

    if (rtc.lostPower()) {
      Serial.println("RTC sin energia, ajustando hora");
      //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // Mostrar fecha/hora actual
    DateTime now = rtc.now();
    Serial.print("Fecha RTC: ");
    Serial.print(now.day());
    Serial.print("/");
    Serial.print(now.month());
    Serial.print("/");
    Serial.println(now.year());
    Serial.print("Hora RTC: ");
    Serial.print(now.hour());
    Serial.print(":");
    Serial.print(now.minute());
    Serial.print(":");
    Serial.println(now.second());
    
  } else {
    Serial.println("ERROR: RTC no detectado");
    rtcConectado = false;
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 50);
    tft.println("ERROR RTC");
    tft.setTextSize(1);
    tft.println("No detectado");
    tft.println("Verificar conexiones:");
    tft.println("SDA=33, SCL=32");
    delay(3000);
  }
}

void inicializarBalanza() {
  Serial.println("Inicializando balanza HX711...");
  
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  
  if(scale.wait_ready_timeout(2000)) {
    balanzaInicializada = true;
    scale.set_scale(factor_calibracion);
    
    Serial.println("Haciendo tara inicial...");
    scale.tare(10);
    
    Serial.println("Balanza lista para usar");
    Serial.print("Factor calibracion: ");
    Serial.println(factor_calibracion);
    
  } else {
    Serial.println("ERROR: Balanza HX711 no responde");
    balanzaInicializada = false;
  }
}

void inicializarLEDs() {
  pinMode(LED_INDICADOR, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);
  digitalWrite(LED_INDICADOR, LOW);
  digitalWrite(LED_ERROR, LOW);
}

// ============================
// FUNCIONES DE SONIDO Y LED
// ============================

void sonidoInicio() {
  tone(BUZZER_PIN, 1000);
  delay(100);
  noTone(BUZZER_PIN);
  delay(50);
  tone(BUZZER_PIN, 1500);
  delay(100);
  noTone(BUZZER_PIN);
}

void sonidoExito() {
  tone(BUZZER_PIN, 1500);
  delay(100);
  noTone(BUZZER_PIN);
  delay(50);
  tone(BUZZER_PIN, 2000);
  delay(80);
  noTone(BUZZER_PIN);
  encenderLED(LED_INDICADOR); // LED normal para éxito
}

void sonidoError() {
  tone(BUZZER_PIN, 300);
  delay(200);
  noTone(BUZZER_PIN);
  delay(100);
  tone(BUZZER_PIN, 300);
  delay(200);
  noTone(BUZZER_PIN);
  encenderLED(LED_ERROR); // LED de error para errores
}

void sonidoBeep() {
  tone(BUZZER_PIN, 1000);
  delay(50);
  noTone(BUZZER_PIN);
}

void encenderLED(int pin, int tiempo) {
  digitalWrite(pin, HIGH);
  delay(tiempo);
  digitalWrite(pin, LOW);
}

// ============================
// FUNCIONES DE CONVERSIÓN
// ============================

float gramosALibras(float gramos) {
  return gramos * 0.00220462;
}

float librasAGramos(float libras) {
  return libras / 0.00220462;
}

// ============================
// FUNCIONES DE FECHA/HORA
// ============================

String obtenerFechaHora() {
  if(rtcConectado) {
    DateTime now = rtc.now();
    char buffer[20];
    sprintf(buffer, "%02d/%02d %02d:%02d", 
            now.day(), now.month(), now.hour(), now.minute());
    return String(buffer);
  }
  return "--/-- --:--";
}

String obtenerFechaActual() {
  if(rtcConectado) {
    DateTime now = rtc.now();
    char buffer[11];
    sprintf(buffer, "%04d%02d%02d", now.year(), now.month(), now.day());
    return String(buffer);
  }
  return "00000000";
}

String obtenerTipoPesajeTexto(TipoPesaje tipo) {
  switch(tipo) {
    case PESAJE_MATUTINO: return "AM";
    case PESAJE_VESPERTINO: return "PM";
    default: return "Auto";
  }
}

TipoPesaje determinarTurnoAutomatico() {
  if(!rtcConectado) return PESAJE_AUTOMATICO;
  
  DateTime now = rtc.now();
  int hora = now.hour();
  
  // AM: 5:00 AM a 11:59 AM
  // PM: 12:00 PM a 4:59 PM
  // Auto: fuera de horarios de pesaje
  if (hora >= 5 && hora < 12) {
    return PESAJE_MATUTINO;
  } else if (hora >= 12 && hora < 17) {
    return PESAJE_VESPERTINO;
  } else {
    return PESAJE_AUTOMATICO; // Fuera de horario
  }
}

// ============================
// FUNCIONES DE DIBUJO
// ============================

void dibujarBoton(int x, int y, int w, int h, String texto, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 5, color);
  tft.drawRoundRect(x, y, w, h, 5, TFT_WHITE);
  
  // Centrar texto en el botón
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  int textX = x + (w - tft.textWidth(texto.c_str())) / 2;
  int textY = y + (h - 16) / 2;
  tft.setCursor(textX, textY);
  tft.println(texto);
  tft.setTextSize(1);
}

void dibujarTecladoNumerico() {
  tft.fillRect(0, 150, SCREEN_WIDTH, 170, TFT_DARKGREY);
  
  // Botones numéricos
  const char* teclas[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"*", "0", "#"}
  };
  
  int botonW = 80;
  int botonH = 40;
  int espacio = 10;
  
  for (int fila = 0; fila < 4; fila++) {
    for (int col = 0; col < 3; col++) {
      int x = col * (botonW + espacio) + 50;
      int y = fila * (botonH + espacio) + 160;
      dibujarBoton(x, y, botonW, botonH, teclas[fila][col], TFT_BLUE);
    }
  }
}

void dibujarTeclasMenu() {
  // Botones de control
  dibujarBoton(10, 270, 100, 40, "A", TFT_RED);
  dibujarBoton(120, 270, 100, 40, "B", TFT_RED);
  dibujarBoton(230, 270, 100, 40, "C", TFT_RED);
  dibujarBoton(340, 270, 100, 40, "D", TFT_RED);
}

char detectarTeclaNumerica(int x, int y) {
  // Coordenadas del teclado numérico
  int botonW = 80;
  int botonH = 40;
  int espacio = 10;
  
  const char teclas[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
  };
  
  for (int fila = 0; fila < 4; fila++) {
    for (int col = 0; col < 3; col++) {
      int botonX = col * (botonW + espacio) + 50;
      int botonY = fila * (botonH + espacio) + 160;
      
      if (x >= botonX && x <= botonX + botonW &&
          y >= botonY && y <= botonY + botonH) {
        return teclas[fila][col];
      }
    }
  }
  
  return 0; // No se presionó ninguna tecla
}

char detectarTeclaMenu(int x, int y) {
  if (x >= 10 && x <= 110 && y >= 270 && y <= 310) return 'A';
  if (x >= 120 && x <= 220 && y >= 270 && y <= 310) return 'B';
  if (x >= 230 && x <= 330 && y >= 270 && y <= 310) return 'C';
  if (x >= 340 && x <= 440 && y >= 270 && y <= 310) return 'D';
  
  return 0; // No se presionó ninguna tecla
}

// ============================
// FUNCIONES DE PANTALLA
// ============================

void actualizarPantalla() {
  tft.fillScreen(TFT_BLACK);
  
  // Encabezado (línea superior)
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.print(obtenerFechaHora());
  
  // Indicadores (esquina superior derecha)
  tft.setCursor(SCREEN_WIDTH - 60, 5);
  tft.print("R:");
  tft.print(rtcConectado ? "C" : "X");
  tft.print(" B:");
  tft.print(balanzaInicializada ? "O" : "X");
  
  // Línea separadora
  tft.drawFastHLine(0, 20, SCREEN_WIDTH, TFT_WHITE);
  
  // Contenido principal
  tft.setCursor(0, 30);
  
  switch(estadoActual) {
    case SISTEMA_PRINCIPAL:
      mostrarMenuPrincipal();
      break;
    case INGRESO_CODIGO:
      mostrarIngresoCodigo();
      break;
    case SELECCION_TURNO:
      mostrarSeleccionTurno();
      break;
    case ESPERANDO_PESO:
      mostrarEsperandoPeso();
      break;
    case CONTANDO_3_SEGUNDOS:
      mostrarContando3Segundos();
      break;
    case PESO_REGISTRADO:
      mostrarPesoRegistrado();
      break;
    case VISUALIZAR_REGISTROS:
      mostrarRegistros();
      break;
    case EDITAR_CODIGO:
      mostrarEditarCodigo();
      break;
    case EDITANDO_PESO_AUTO:
      mostrarEditandoPesoAuto();
      break;
    case CONFIRMAR_BORRAR_TOTAL:
      mostrarConfirmarBorrarTotal();
      break;
    case REGISTRO_DUPLICADO:
      mostrarRegistroDuplicado();
      break;
    case MENU_TIPO_PESAJE:
      mostrarMenuTipoPesaje();
      break;
    case EDITAR_TIPO_PESAJE:
      mostrarEditarTipoPesaje();
      break;
    case ERROR_BALANZA:
      mostrarErrorBalanza();
      break;
    case MODO_CALIBRACION:
      mostrarModoCalibracion();
      break;
    case EDITANDO_REGISTRO:
      mostrarRegistros();
      break;
  }
}

void mostrarMenuPrincipal() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("MENU PRINCIPAL");
  tft.setTextSize(1);
  
  // Obtener tipo de pesaje actual
  String tipoActual = obtenerTipoPesajeTexto(tipoPesajeSeleccionado);
  
  // Mostrar opciones
  tft.setCursor(20, 70);
  tft.println("1. Ingresar codigo");
  tft.setCursor(20, 90);
  tft.println("2. Ver registros");
  tft.setCursor(20, 110);
  tft.println("3. Calibrar balanza");
  tft.setCursor(20, 130);
  tft.print("4. Turno: ");
  tft.println(tipoActual);
  tft.setCursor(20, 150);
  tft.println("5. Borrar todo");
  
  // Dibujar teclado virtual
  dibujarTeclasMenu();
  
  // Instrucciones
  tft.setCursor(10, 220);
  tft.println("Toque los botones A,B,C,D para navegar");
}

void mostrarIngresoCodigo() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("INGRESAR CODIGO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Codigo vaca: ");
  tft.setTextSize(2);
  tft.println(entradaCodigo);
  tft.setTextSize(1);
  
  tft.setCursor(20, 110);
  tft.println("# = Continuar");
  tft.setCursor(20, 130);
  tft.println("* = Borrar");
  tft.setCursor(20, 150);
  tft.println("C = Cancelar");
  
  // Dibujar teclado numérico
  dibujarTecladoNumerico();
  dibujarTeclasMenu();
}

void mostrarSeleccionTurno() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("SELECCIONAR TURNO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Codigo: ");
  tft.setTextSize(2);
  tft.println(entradaCodigo);
  tft.setTextSize(1);
  
  tft.setCursor(20, 110);
  tft.println("A. Turno Matutino (AM)");
  tft.setCursor(20, 130);
  tft.println("B. Turno Vespertino (PM)");
  
  // Mostrar turno automático basado en hora actual
  TipoPesaje turnoAuto = determinarTurnoAutomatico();
  String textoAuto = "C. Auto (";
  if (turnoAuto == PESAJE_MATUTINO) textoAuto += "AM)";
  else if (turnoAuto == PESAJE_VESPERTINO) textoAuto += "PM)";
  else textoAuto += "---)";
  
  tft.setCursor(20, 150);
  tft.println(textoAuto);
  tft.setCursor(20, 170);
  tft.println("D = Cancelar");
  
  dibujarTeclasMenu();
}

void mostrarEsperandoPeso() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("COLOCAR PRODUCTO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Cod: ");
  tft.setTextSize(2);
  tft.println(entradaCodigo);
  tft.setTextSize(1);
  
  // Determinar turno automáticamente si está en modo auto
  TipoPesaje turnoActual = tipoPesajeSeleccionado;
  if (turnoActual == PESAJE_AUTOMATICO) {
    turnoActual = determinarTurnoAutomatico();
  }
  
  tft.setCursor(20, 100);
  tft.print("Turno: ");
  tft.println(obtenerTipoPesajeTexto(turnoActual));
  
  tft.setCursor(20, 130);
  if(balanzaInicializada && leerBalanza) {
    // Mostrar peso con texto grande
    tft.setTextSize(3);
    tft.setCursor(50, 160);
    tft.print(gramosALibras(ultimoPesoLeido), 1);
    tft.println(" lb");
    tft.setTextSize(1);
    
    tft.setCursor(20, 200);
    tft.println("Esperando peso estable...");
  } else {
    tft.println("Balanza no disponible");
  }
  
  tft.setCursor(20, 230);
  tft.println("C = Cancelar");
  
  dibujarTeclasMenu();
}

void mostrarContando3Segundos() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("REGISTRANDO PESO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Cod: ");
  tft.setTextSize(2);
  tft.println(entradaCodigo);
  tft.setTextSize(1);
  
  // Determinar turno automáticamente si está en modo auto
  TipoPesaje turnoActual = tipoPesajeSeleccionado;
  if (turnoActual == PESAJE_AUTOMATICO) {
    turnoActual = determinarTurnoAutomatico();
  }
  
  tft.setCursor(20, 100);
  tft.print("Turno: ");
  tft.println(obtenerTipoPesajeTexto(turnoActual));
  
  if(balanzaInicializada) {
    // Mostrar peso con texto grande
    tft.setTextSize(4);
    tft.setCursor(50, 140);
    tft.print(gramosALibras(pesoARegistrar), 1);
    tft.setTextSize(2);
    tft.println(" lb");
    tft.setTextSize(1);
    
    tft.setCursor(20, 200);
    
    // Mostrar conteo regresivo de 3 segundos
    unsigned long tiempoTranscurrido = millis() - tiempoInicioConteo;
    segundosRestantes = 3 - (tiempoTranscurrido / 1000);
    
    if(segundosRestantes > 0) {
      tft.print("Registrando en ");
      tft.print(segundosRestantes);
      tft.println(" s");
    } else {
      tft.println("Registrando...");
    }
  }
  
  tft.setCursor(20, 230);
  tft.println("C = Cancelar");
  
  dibujarTeclasMenu();
}

void mostrarPesoRegistrado() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("REGISTRO EXITOSO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Cod: ");
  tft.setTextSize(2);
  tft.println(entradaCodigo);
  tft.setTextSize(1);
  
  // Mostrar peso con texto grande
  tft.setTextSize(4);
  tft.setCursor(50, 120);
  tft.print(gramosALibras(pesoARegistrar), 1);
  tft.setTextSize(2);
  tft.println(" lb");
  tft.setTextSize(1);
  
  tft.setCursor(20, 170);
  tft.print("Turno: ");
  
  // Determinar turno automáticamente si está en modo auto
  TipoPesaje turnoActual = tipoPesajeSeleccionado;
  if (turnoActual == PESAJE_AUTOMATICO) {
    turnoActual = determinarTurnoAutomatico();
  }
  
  tft.println(obtenerTipoPesajeTexto(turnoActual));
  
  tft.setCursor(20, 200);
  tft.println("Guardando...");
  
  dibujarTeclasMenu();
}

void mostrarRegistros() {
  if(registros.empty()) {
    tft.setTextSize(2);
    tft.setCursor(10, 30);
    tft.println("SIN REGISTROS");
    tft.setTextSize(1);
    
    tft.setCursor(20, 70);
    tft.println("No hay datos almacenados");
    
    tft.setCursor(20, 100);
    tft.println("C = Regresar");
    
    dibujarTeclasMenu();
    return;
  }
  
  // Ajustar índice si es necesario
  if(indiceRegistroActual < 0) indiceRegistroActual = registros.size() - 1;
  if(indiceRegistroActual >= registros.size()) indiceRegistroActual = 0;
  
  RegistroPeso reg = registros[indiceRegistroActual];
  
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.print("REG [");
  tft.print(indiceRegistroActual + 1);
  tft.print("/");
  tft.print(registros.size());
  tft.println("]");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Cod: ");
  tft.setTextSize(2);
  tft.println(reg.codigo_vaca);
  tft.setTextSize(1);
  
  // Mostrar peso con texto grande
  tft.setTextSize(3);
  tft.setCursor(50, 110);
  tft.print(reg.peso_lb, 1);
  tft.setTextSize(2);
  tft.println(" lb");
  tft.setTextSize(1);
  
  char buffer[20];
  sprintf(buffer, "%02d/%02d %02d:%02d", 
          reg.day, reg.month, reg.hour, reg.minute);
  
  tft.setCursor(20, 160);
  tft.print(buffer);
  
  tft.print(" ");
  tft.println(obtenerTipoPesajeTexto(reg.tipo_pesaje));
  
  if(estadoActual == EDITANDO_REGISTRO) {
    tft.setCursor(20, 190);
    tft.println("A=Editar B=↑ C=↓ D=Menu");
  } else {
    tft.setCursor(20, 190);
    tft.println("A=↑ B=↓ C=↩ D=Editar");
  }
  
  dibujarTeclasMenu();
}

void mostrarEditarCodigo() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("EDITAR CODIGO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Actual: ");
  tft.setTextSize(2);
  tft.println(registros[indiceRegistroActual].codigo_vaca);
  tft.setTextSize(1);
  
  tft.setCursor(20, 110);
  tft.print("Nuevo: ");
  tft.setTextSize(2);
  tft.println(entradaCodigo);
  tft.setTextSize(1);
  
  tft.setCursor(20, 150);
  tft.println("# = Guardar");
  tft.setCursor(20, 170);
  tft.println("* = Borrar");
  tft.setCursor(20, 190);
  tft.println("C = Cancelar");
  
  dibujarTecladoNumerico();
  dibujarTeclasMenu();
}

void mostrarEditandoPesoAuto() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("EDITAR PESO AUTO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Cod: ");
  tft.setTextSize(2);
  tft.println(registros[indiceRegistroActual].codigo_vaca);
  tft.setTextSize(1);
  
  if(balanzaInicializada && leerBalanza) {
    // Mostrar peso con texto grande durante edición
    tft.setTextSize(3);
    tft.setCursor(50, 120);
    tft.print(gramosALibras(ultimoPesoLeido), 1);
    tft.setTextSize(2);
    tft.println(" lb");
    tft.setTextSize(1);
    
    tft.setCursor(20, 170);
    if(editandoPesoAuto) {
      // Mostrar conteo regresivo
      unsigned long tiempoTranscurrido = millis() - tiempoInicioConteo;
      segundosRestantes = 3 - (tiempoTranscurrido / 1000);
      
      if(segundosRestantes > 0) {
        tft.print("Actualizando en ");
        tft.print(segundosRestantes);
        tft.println(" s");
      } else {
        tft.println("Actualizando...");
      }
    } else {
      tft.println("Coloque peso");
      tft.println("y espere deteccion");
    }
  }
  
  tft.setCursor(20, 220);
  tft.println("C = Cancelar");
  
  dibujarTeclasMenu();
}

void mostrarConfirmarBorrarTotal() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("BORRAR TODO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.println("¿Eliminar TODOS");
  tft.setCursor(20, 90);
  tft.println("los registros?");
  tft.setCursor(20, 120);
  tft.println("Esta accion NO");
  tft.setCursor(20, 140);
  tft.println("se puede deshacer");
  tft.setCursor(20, 170);
  tft.println("D = Confirmar");
  tft.setCursor(20, 190);
  tft.println("C = Cancelar");
  
  dibujarTeclasMenu();
}

void mostrarRegistroDuplicado() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("REGISTRO DUPLICADO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.println("Ya existe registro");
  tft.setCursor(20, 90);
  tft.println("para esta vaca en");
  tft.setCursor(20, 110);
  tft.println("el turno seleccionado");
  tft.setCursor(20, 140);
  tft.println("# = Sobreescribir");
  tft.setCursor(20, 160);
  tft.println("C = Cancelar");
  
  dibujarTeclasMenu();
}

void mostrarMenuTipoPesaje() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("CAMBIAR MODO TURNO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.println("Seleccione modo:");
  tft.setCursor(20, 100);
  tft.println("A. Matutino (AM)");
  tft.setCursor(20, 120);
  tft.println("B. Vespertino (PM)");
  tft.setCursor(20, 140);
  tft.println("C. Auto (por hora)");
  tft.setCursor(20, 160);
  tft.println("D = Cancelar");
  
  dibujarTeclasMenu();
}

void mostrarEditarTipoPesaje() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("EDITAR TURNO");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.print("Actual: ");
  tft.println(obtenerTipoPesajeTexto(registros[indiceRegistroActual].tipo_pesaje));
  
  tft.setCursor(20, 100);
  tft.println("Nuevo turno:");
  tft.setCursor(20, 130);
  tft.println("A. Matutino (AM)");
  tft.setCursor(20, 150);
  tft.println("B. Vespertino (PM)");
  tft.setCursor(20, 170);
  tft.println("C = Cancelar");
  
  dibujarTeclasMenu();
}

void mostrarErrorBalanza() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("ERROR BALANZA");
  tft.setTextSize(1);
  
  tft.setCursor(20, 70);
  tft.println("Sensor HX711");
  tft.setCursor(20, 90);
  tft.println("no disponible");
  tft.setCursor(20, 120);
  tft.println("Verificar conexion");
  tft.setCursor(20, 140);
  tft.println("o calibrar");
  tft.setCursor(20, 170);
  tft.println("C = Continuar");
  
  dibujarTeclasMenu();
}

void mostrarModoCalibracion() {
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("CALIBRACION");
  tft.setTextSize(1);
  
  if(balanzaInicializada && leerBalanza) {
    // Mostrar lectura en tiempo real durante calibración
    tft.setTextSize(4);
    tft.setCursor(50, 80);
    tft.print(ultimoPesoLeido, 1);
    tft.setTextSize(2);
    tft.println(" g");
    tft.setTextSize(1);
  }
  
  tft.setCursor(20, 130);
  tft.println("1. Balanza vacia");
  tft.setCursor(20, 150);
  tft.println("   Presione A (Tara)");
  tft.setCursor(20, 170);
  tft.println("2. Peso 1kg (1000g)");
  tft.setCursor(20, 190);
  tft.println("   Presione B (Calibrar)");
  tft.setCursor(20, 210);
  tft.println("C = Cancelar");
  
  dibujarTeclasMenu();
}

// ============================
// FUNCIONES DE BALANZA
// ============================

float leerPeso() {
  if(!balanzaInicializada || !leerBalanza) return 0.0;
  
  float peso_g = scale.get_units(5) * 1000.0;
  
  // Filtrar ruido
  if(abs(peso_g) < 2.0) peso_g = 0.0;
  
  return peso_g;
}

void iniciarLecturaBalanza() {
  leerBalanza = true;
}

void detenerLecturaBalanza() {
  leerBalanza = false;
}

void procesarDeteccionPeso() {
  if(!balanzaInicializada || !leerBalanza) return;
  
  ultimoPesoLeido = leerPeso();
  
  // Detectar cuando se coloca un objeto (>100g)
  if(!objetoDetectado && ultimoPesoLeido > 100.0) {
    objetoDetectado = true;
    tiempoDeteccion = millis();
    tiempoInicioConteo = millis(); // Iniciar conteo de 3 segundos
    pesoARegistrar = ultimoPesoLeido;
    segundosRestantes = 3; // Resetear contador
    
    Serial.print("Peso detectado: ");
    Serial.print(gramosALibras(ultimoPesoLeido), 1);
    Serial.println(" lb");
    
    // NO SONAR durante el pesaje (solo al final)
    // Cambiar a estado de conteo
    estadoActual = CONTANDO_3_SEGUNDOS;
  }
}

void procesarConteo3Segundos() {
  if(!balanzaInicializada || !leerBalanza) return;
  
  // Actualizar peso continuamente
  ultimoPesoLeido = leerPeso();
  
  if(ultimoPesoLeido > 100.0) {
    pesoARegistrar = ultimoPesoLeido;
  }
  
  // Verificar si han pasado 3 segundos
  if(millis() - tiempoDeteccion >= TIEMPO_ESPERA) {
    // Peso estabilizado, SONAR y registrar
    sonidoExito(); // SONIDO solo al finalizar pesaje
    
    Serial.print("Peso final: ");
    Serial.print(gramosALibras(pesoARegistrar), 1);
    Serial.println(" lb");
    
    if(editandoPesoAuto) {
      // Actualizar registro existente
      registros[indiceRegistroActual].peso_lb = gramosALibras(pesoARegistrar);
      registros[indiceRegistroActual].editado = true;
      guardarRegistros();
      
      encenderLED(LED_INDICADOR);
      
      editandoPesoAuto = false;
      estadoActual = VISUALIZAR_REGISTROS;
      
    } else {
      // Registrar nuevo peso
      registrarPesoAutomatico();
    }
    
    // Detener lectura de balanza
    detenerLecturaBalanza();
  }
  
  // Si el peso desaparece, cancelar
  if(ultimoPesoLeido < 2.0) {
    Serial.println("Peso removido, cancelando");
    
    objetoDetectado = false;
    editandoPesoAuto = false;
    
    // Encender LED de error
    encenderLED(LED_ERROR);
    
    if(estadoActual == EDITANDO_PESO_AUTO) {
      estadoActual = EDITANDO_REGISTRO;
    } else {
      estadoActual = ESPERANDO_PESO;
    }
    
    sonidoError();
  }
}

void procesarEdicionPesoAuto() {
  if(!balanzaInicializada || !leerBalanza) return;
  
  ultimoPesoLeido = leerPeso();
  
  // Detectar cuando se coloca un objeto para edición
  if(!editandoPesoAuto && ultimoPesoLeido > 100.0) {
    editandoPesoAuto = true;
    tiempoDeteccion = millis();
    tiempoInicioConteo = millis(); // Iniciar conteo de 3 segundos
    pesoARegistrar = ultimoPesoLeido;
    segundosRestantes = 3; // Resetear contador
    
    Serial.print("Peso detectado para edicion: ");
    Serial.print(gramosALibras(ultimoPesoLeido), 1);
    Serial.println(" lb");
    
    // Cambiar a estado de conteo (usa la misma función)
    estadoActual = CONTANDO_3_SEGUNDOS;
  }
}

void registrarPesoAutomatico() {
  if(entradaCodigo.isEmpty()) return;
  
  float peso_lb = gramosALibras(pesoARegistrar);
  codigoActual = entradaCodigo.toInt();
  
  // Obtener fecha actual
  DateTime now = rtc.now();
  uint16_t year = now.year();
  uint8_t month = now.month();
  uint8_t day = now.day();
  
  // Determinar turno final (si es automático, calcular según hora)
  TipoPesaje turnoFinal = tipoPesajeSeleccionado;
  if (turnoFinal == PESAJE_AUTOMATICO) {
    turnoFinal = determinarTurnoAutomatico();
  }
  
  // Verificar duplicado por código + turno + fecha
  if(esDuplicado(codigoActual, turnoFinal, year, month, day)) {
    estadoActual = REGISTRO_DUPLICADO;
    return;
  }
  
  // Crear nuevo registro
  RegistroPeso nuevoRegistro;
  nuevoRegistro.codigo_vaca = codigoActual;
  nuevoRegistro.peso_lb = peso_lb;
  nuevoRegistro.editado = false;
  nuevoRegistro.tipo_pesaje = turnoFinal;
  
  nuevoRegistro.year = year;
  nuevoRegistro.month = month;
  nuevoRegistro.day = day;
  nuevoRegistro.hour = now.hour();
  nuevoRegistro.minute = now.minute();
  nuevoRegistro.second = now.second();
  nuevoRegistro.timestamp = now.unixtime();
  
  // Limitar número de registros
  if(registros.size() >= MAX_REGISTROS) {
    registros.erase(registros.begin());
  }
  
  // Añadir registro
  registros.push_back(nuevoRegistro);
  guardarRegistros();
  
  Serial.print("Registro guardado: Cod ");
  Serial.print(codigoActual);
  Serial.print(", Peso ");
  Serial.print(peso_lb, 1);
  Serial.print(" lb, Turno ");
  Serial.println(obtenerTipoPesajeTexto(turnoFinal));
  
  // Feedback al usuario (ya se hizo sonidoExito en procesarConteo3Segundos)
  estadoActual = PESO_REGISTRADO;
  
  // Esperar 2 segundos y volver al menú
  delay(2000);
  entradaCodigo = "";
  objetoDetectado = false;
  estadoActual = SISTEMA_PRINCIPAL;
}

// ============================
// FUNCIONES DE MEMORIA
// ============================

void cargarRegistros() {
  registros.clear();
  int contador = preferences.getInt("contador", 0);
  
  Serial.print("Cargando registros: ");
  Serial.println(contador);
  
  for(int i = 1; i <= contador; i++) {
    String clave = "reg_" + String(i);
    size_t tamano = preferences.getBytesLength(clave.c_str());
    
    if(tamano == sizeof(RegistroPeso)) {
      RegistroPeso reg;
      preferences.getBytes(clave.c_str(), &reg, sizeof(RegistroPeso));
      
      // Filtrar registros antiguos (>6 meses)
      if(!esRegistroAntiguo(reg)) {
        registros.push_back(reg);
      }
    }
  }
  
  Serial.print("Registros cargados: ");
  Serial.println(registros.size());
}

void guardarRegistros() {
  preferences.putInt("contador", registros.size());
  
  for(int i = 0; i < registros.size(); i++) {
    String clave = "reg_" + String(i+1);
    preferences.putBytes(clave.c_str(), &registros[i], sizeof(RegistroPeso));
  }
  
  Serial.print("Registros guardados: ");
  Serial.println(registros.size());
}

void guardarTipoPesaje() {
  preferences.putUChar("tipo_pesaje", tipoPesajeSeleccionado);
}

bool esRegistroAntiguo(RegistroPeso reg) {
  if(!rtcConectado) return false;
  
  DateTime ahora = rtc.now();
  DateTime regFecha(reg.year, reg.month, reg.day, 0, 0, 0);
  
  uint32_t diff = ahora.unixtime() - regFecha.unixtime();
  return diff > (180 * 86400); // Más de 6 meses
}

bool esDuplicado(int codigo, TipoPesaje turno, uint16_t year, uint8_t month, uint8_t day) {
  for(int i = 0; i < registros.size(); i++) {
    if(registros[i].codigo_vaca == codigo &&
       registros[i].tipo_pesaje == turno &&
       registros[i].year == year &&
       registros[i].month == month &&
       registros[i].day == day) {
      return true;
    }
  }
  return false;
}
bool my_touch_read(lv_indev_drv_t *indev, lv_indev_data_t *data) {

  if (!touch.touched()) {
    data->state = LV_INDEV_STATE_REL;
    return false;
  }

  touch.read();

  data->point.x = touch.points[0].x;
  data->point.y = touch.points[0].y;
  data->state = LV_INDEV_STATE_PR;

  return false;
}


void ajustarTurnoDuplicado(int codigo, TipoPesaje nuevoTurno, uint16_t year, uint8_t month, uint8_t day) {
  // Buscar si existe registro con el turno opuesto
  TipoPesaje turnoOpuesto = (nuevoTurno == PESAJE_MATUTINO) ? PESAJE_VESPERTINO : PESAJE_MATUTINO;
  
  for(int i = 0; i < registros.size(); i++) {
    if(registros[i].codigo_vaca == codigo &&
       registros[i].tipo_pesaje == turnoOpuesto &&
       registros[i].year == year &&
       registros[i].month == month &&
       registros[i].day == day) {
      
      // Cambiar el turno opuesto
      registros[i].tipo_pesaje = nuevoTurno;
      registros[i].editado = true;
      return;
    }
  }
}

void limpiarRegistrosAntiguos() {
  if(!rtcConectado) return;
  
  int eliminados = 0;
  std::vector<RegistroPeso> nuevosRegistros;
  
  for(auto& reg : registros) {
    if(!esRegistroAntiguo(reg)) {
      nuevosRegistros.push_back(reg);
    } else {
      eliminados++;
    }
  }
  
  if(eliminados > 0) {
    registros = nuevosRegistros;
    guardarRegistros();
    
    Serial.print("Eliminados ");
    Serial.print(eliminados);
    Serial.println(" registros antiguos");
  }
}

void borrarTodosRegistros() {
  registros.clear();
  preferences.clear();
  Serial.println("Todos los registros eliminados");
  
  // Encender LED de error (pin 16) al borrar
  encenderLED(LED_ERROR, 1000);
  preferences.end();

}

// ============================
// CALIBRACIÓN DE BALANZA
// ============================

void calibrarBalanza() {
  Serial.println("=== MODO CALIBRACION ===");
  
  modoCalibracion = true;
  iniciarLecturaBalanza();
  
  while(modoCalibracion) {
    // Leer peso en tiempo real para mostrar en pantalla
    ultimoPesoLeido = leerPeso();
    
    // Leer touch
    procesarTouch();
    
    // Actualizar pantalla periódicamente
    static unsigned long ultimaActualizacion = 0;
    if(millis() - ultimaActualizacion > 200) {
      actualizarPantalla();
      ultimaActualizacion = millis();
    }
    
    delay(10);
  }
}

// ============================
// MANEJO DEL TOUCH
// ============================

void procesarTouch() {
  // Leer cantidad de puntos tocados
  uint8_t touchCount = Touchscreen.touched();

  if (touchCount > 0) {
    for (uint8_t i = 0; i < touchCount; i++) {
      // Obtener coordenadas
      GTPoint p = Touchscreen.getPoint(i);
      
      // Ajustar coordenadas según la rotación
      int touchX = p.x;
      int touchY = p.y;
      
      // Solo procesar si es un nuevo toque (evitar mantener presionado)
      if (!touchPressed || (abs(touchX - lastTouchPoint.x) > 10 || abs(touchY - lastTouchPoint.y) > 10)) {
        touchPressed = true;
        lastTouchPoint = p;
        
        // Detectar qué tecla se presionó
        char tecla = 0;
        
        if (estadoActual == INGRESO_CODIGO || estadoActual == EDITAR_CODIGO) {
          // En modo ingreso de código, usar teclado numérico
          tecla = detectarTeclaNumerica(touchX, touchY);
        }
        
        // Si no se encontró en el teclado numérico, buscar en menú
        if (!tecla) {
          tecla = detectarTeclaMenu(touchX, touchY);
        }
        
        if (tecla) {
          // Procesar la tecla según el estado actual
          procesarTecla(tecla);
          sonidoBeep(); // Feedback auditivo
          
          // Actualizar pantalla inmediatamente
          actualizarPantalla();
        }
      }
    }
  } else {
    touchPressed = false;
  }
}

// ============================
// MANEJO DE TECLAS (adaptado para touch)
// ============================

void procesarTecla(char tecla) {
  switch(estadoActual) {
    case SISTEMA_PRINCIPAL:
      if(tecla == '1') {
        entradaCodigo = "";
        estadoActual = INGRESO_CODIGO;
      } else if(tecla == '2') {
        estadoActual = VISUALIZAR_REGISTROS;
        indiceRegistroActual = 0;
        desplazamientoPantalla = 0;
      } else if(tecla == '3') {
        estadoActual = MODO_CALIBRACION;
      } else if(tecla == '4') {
        estadoActual = MENU_TIPO_PESAJE;
      } else if(tecla == '5') {
        estadoActual = CONFIRMAR_BORRAR_TOTAL;
      } else if(tecla == 'A') {
        // Subir en menú
        desplazamientoPantalla = max(0, desplazamientoPantalla - 1);
      } else if(tecla == 'B') {
        // Bajar en menú
        desplazamientoPantalla = min(4, desplazamientoPantalla + 1);
      } else if(tecla == 'C') {
        // Ya estamos en menú principal
      }
      break;
      
    case INGRESO_CODIGO:
      if(tecla == '#') {
        if(!entradaCodigo.isEmpty()) {
          // Determinar turno automáticamente si está en modo auto
          if (tipoPesajeSeleccionado == PESAJE_AUTOMATICO) {
            // Pasar directamente a pesaje
            estadoActual = ESPERANDO_PESO;
            iniciarLecturaBalanza();
            sonidoExito();
            encenderLED(LED_INDICADOR);
          } else {
            // Mostrar selección de turno manual
            estadoActual = SELECCION_TURNO;
            sonidoExito();
            encenderLED(LED_INDICADOR);
          }
        }
      } else if(tecla == '*') {
        if(entradaCodigo.length() > 0) {
          entradaCodigo.remove(entradaCodigo.length() - 1);
        }
      } else if(tecla == 'C') {
        entradaCodigo = "";
        estadoActual = SISTEMA_PRINCIPAL;
      } else if(isdigit(tecla)) {
        if(entradaCodigo.length() < 6) {
          entradaCodigo += tecla;
        }
      }
      break;
      
    case SELECCION_TURNO:
      if(tecla == 'A') {
        tipoPesajeSeleccionado = PESAJE_MATUTINO;
        estadoActual = ESPERANDO_PESO;
        iniciarLecturaBalanza();
        sonidoExito();
        encenderLED(LED_INDICADOR);
      } else if(tecla == 'B') {
        tipoPesajeSeleccionado = PESAJE_VESPERTINO;
        estadoActual = ESPERANDO_PESO;
        iniciarLecturaBalanza();
        sonidoExito();
        encenderLED(LED_INDICADOR);
      } else if(tecla == 'C') {
        // Modo automático (determinado por hora)
        tipoPesajeSeleccionado = PESAJE_AUTOMATICO;
        estadoActual = ESPERANDO_PESO;
        iniciarLecturaBalanza();
        sonidoExito();
        encenderLED(LED_INDICADOR);
      } else if(tecla == 'D') {
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
      
    case ESPERANDO_PESO:
      if(tecla == 'C') {
        entradaCodigo = "";
        objetoDetectado = false;
        detenerLecturaBalanza();
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
      
    case CONTANDO_3_SEGUNDOS:
      if(tecla == 'C') {
        entradaCodigo = "";
        objetoDetectado = false;
        editandoPesoAuto = false;
        detenerLecturaBalanza();
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
      
    case PESO_REGISTRADO:
      // Vuelve automáticamente después de 2 segundos
      if(tecla) {
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
      
    case VISUALIZAR_REGISTROS:
      if(tecla == 'C') {
        estadoActual = SISTEMA_PRINCIPAL;
      } else if(tecla == 'A' && !registros.empty()) {
        indiceRegistroActual--;
        if(indiceRegistroActual < 0) indiceRegistroActual = registros.size() - 1;
      } else if(tecla == 'B' && !registros.empty()) {
        indiceRegistroActual++;
        if(indiceRegistroActual >= registros.size()) indiceRegistroActual = 0;
      } else if(tecla == 'D' && !registros.empty()) {
        estadoActual = EDITANDO_REGISTRO;
      }
      break;
      
    case EDITANDO_REGISTRO:
      if(tecla == 'C') {
        estadoActual = VISUALIZAR_REGISTROS;
      } else if(tecla == 'A') {
        // Subir en lista
        indiceRegistroActual--;
        if(indiceRegistroActual < 0) indiceRegistroActual = registros.size() - 1;
      } else if(tecla == 'B') {
        // Bajar en lista
        indiceRegistroActual++;
        if(indiceRegistroActual >= registros.size()) indiceRegistroActual = 0;
      } else if(tecla == 'D') {
        // Menú de edición
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(10, 30);
        tft.println("EDITAR REGISTRO:");
        tft.setTextSize(1);
        tft.setCursor(20, 80);
        tft.println("Toque una opcion:");
        tft.setCursor(40, 110);
        tft.println("1. Cambiar codigo");
        tft.setCursor(40, 140);
        tft.println("2. Cambiar peso");
        tft.setCursor(40, 170);
        tft.println("3. Cambiar turno");
        tft.setCursor(40, 200);
        tft.println("C. Cancelar");
        
        dibujarTeclasMenu();
        //tft.display();
        
        while(true) {
          procesarTouch();
          char opcion = 0;
          
          // Simular detección de opciones
          // En una implementación real, necesitarías botones específicos
          // Por ahora usamos A=1, B=2, C=3, D=Cancelar
          if (lastTouchPoint.y >= 100 && lastTouchPoint.y < 130) opcion = '1';
          else if (lastTouchPoint.y >= 130 && lastTouchPoint.y < 160) opcion = '2';
          else if (lastTouchPoint.y >= 160 && lastTouchPoint.y < 190) opcion = '3';
          else if (lastTouchPoint.y >= 190) opcion = 'C';
          
          if(opcion == '1') {
            estadoActual = EDITAR_CODIGO;
            entradaCodigo = String(registros[indiceRegistroActual].codigo_vaca);
            break;
          } else if(opcion == '2') {
            estadoActual = EDITANDO_PESO_AUTO;
            editandoPesoAuto = false;
            iniciarLecturaBalanza();
            break;
          } else if(opcion == '3') {
            estadoActual = EDITAR_TIPO_PESAJE;
            break;
          } else if(opcion == 'C') {
            break;
          }
          delay(10);
        }
      }
      break;
      
    case EDITAR_CODIGO:
      if(tecla == '#') {
        if(!entradaCodigo.isEmpty()) {
          int nuevoCodigo = entradaCodigo.toInt();
          
          // Verificar si el nuevo código ya existe en la misma fecha y turno
          DateTime now = rtc.now();
          if(esDuplicado(nuevoCodigo, registros[indiceRegistroActual].tipo_pesaje, 
                         now.year(), now.month(), now.day())) {
            sonidoError();
            encenderLED(LED_ERROR);
            break;
          }
          
          registros[indiceRegistroActual].codigo_vaca = nuevoCodigo;
          registros[indiceRegistroActual].editado = true;
          guardarRegistros();
          sonidoExito();
          encenderLED(LED_INDICADOR);
          estadoActual = EDITANDO_REGISTRO;
          entradaCodigo = "";
        }
      } else if(tecla == '*') {
        if(entradaCodigo.length() > 0) {
          entradaCodigo.remove(entradaCodigo.length() - 1);
        }
      } else if(tecla == 'C') {
        estadoActual = EDITANDO_REGISTRO;
        entradaCodigo = "";
      } else if(isdigit(tecla)) {
        if(entradaCodigo.length() < 6) {
          entradaCodigo += tecla;
        }
      }
      break;
      
    case EDITANDO_PESO_AUTO:
      if(tecla == 'C') {
        editandoPesoAuto = false;
        detenerLecturaBalanza();
        estadoActual = EDITANDO_REGISTRO;
      }
      break;
      
    case CONFIRMAR_BORRAR_TOTAL:
      if(tecla == 'D') {
        borrarTodosRegistros();
        sonidoError();
        estadoActual = SISTEMA_PRINCIPAL;
      } else if(tecla == 'C') {
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
      
    case REGISTRO_DUPLICADO:
      if(tecla == '#') {
        // Sobreescribir registro existente
        // Buscar y eliminar registro duplicado
        DateTime now = rtc.now();
        for(int i = 0; i < registros.size(); i++) {
          if(registros[i].codigo_vaca == codigoActual &&
             registros[i].tipo_pesaje == tipoPesajeSeleccionado &&
             registros[i].year == now.year() &&
             registros[i].month == now.month() &&
             registros[i].day == now.day()) {
            registros.erase(registros.begin() + i);
            break;
          }
        }
        
        // Crear nuevo registro
        RegistroPeso nuevoRegistro;
        nuevoRegistro.codigo_vaca = codigoActual;
        nuevoRegistro.peso_lb = gramosALibras(pesoARegistrar);
        nuevoRegistro.editado = false;
        nuevoRegistro.tipo_pesaje = tipoPesajeSeleccionado;
        
        nuevoRegistro.year = now.year();
        nuevoRegistro.month = now.month();
        nuevoRegistro.day = now.day();
        nuevoRegistro.hour = now.hour();
        nuevoRegistro.minute = now.minute();
        nuevoRegistro.second = now.second();
        nuevoRegistro.timestamp = now.unixtime();
        
        registros.push_back(nuevoRegistro);
        guardarRegistros();
        
        sonidoExito();
        encenderLED(LED_INDICADOR);
        estadoActual = PESO_REGISTRADO;
        
      } else if(tecla == 'C') {
        entradaCodigo = "";
        objetoDetectado = false;
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
      
    case MENU_TIPO_PESAJE:
      if(tecla == 'A') {
        tipoPesajeSeleccionado = PESAJE_MATUTINO;
        guardarTipoPesaje();
        sonidoExito();
        encenderLED(LED_INDICADOR);
        estadoActual = SISTEMA_PRINCIPAL;
      } else if(tecla == 'B') {
        tipoPesajeSeleccionado = PESAJE_VESPERTINO;
        guardarTipoPesaje();
        sonidoExito();
        encenderLED(LED_INDICADOR);
        estadoActual = SISTEMA_PRINCIPAL;
      } else if(tecla == 'C') {
        tipoPesajeSeleccionado = PESAJE_AUTOMATICO;
        guardarTipoPesaje();
        sonidoExito();
        encenderLED(LED_INDICADOR);
        estadoActual = SISTEMA_PRINCIPAL;
      } else if(tecla == 'D') {
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
      
    case EDITAR_TIPO_PESAJE:
      if(tecla == 'A') {
        TipoPesaje nuevoTurno = PESAJE_MATUTINO;
        
        // Verificar duplicado y ajustar si es necesario
        DateTime now = rtc.now();
        ajustarTurnoDuplicado(registros[indiceRegistroActual].codigo_vaca, 
                              nuevoTurno, now.year(), now.month(), now.day());
        
        registros[indiceRegistroActual].tipo_pesaje = nuevoTurno;
        registros[indiceRegistroActual].editado = true;
        guardarRegistros();
        
        sonidoExito();
        encenderLED(LED_INDICADOR);
        estadoActual = EDITANDO_REGISTRO;
        
      } else if(tecla == 'B') {
        TipoPesaje nuevoTurno = PESAJE_VESPERTINO;
        
        // Verificar duplicado y ajustar si es necesario
        DateTime now = rtc.now();
        ajustarTurnoDuplicado(registros[indiceRegistroActual].codigo_vaca, 
                              nuevoTurno, now.year(), now.month(), now.day());
        
        registros[indiceRegistroActual].tipo_pesaje = nuevoTurno;
        registros[indiceRegistroActual].editado = true;
        guardarRegistros();
        
        sonidoExito();
        encenderLED(LED_INDICADOR);
        estadoActual = EDITANDO_REGISTRO;
        
      } else if(tecla == 'C') {
        estadoActual = EDITANDO_REGISTRO;
      }
      break;
      
    case ERROR_BALANZA:
      if(tecla == 'C') {
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
      
    case MODO_CALIBRACION:
      // La calibración maneja su propio teclado
      if(tecla == 'A') {
        // Tara (cero)
        Serial.println("Haciendo tara...");
        scale.tare(10);
        Serial.println("Tara completada");
        
      } else if(tecla == 'B') {
        // Calibrar con peso conocido
        Serial.println("Calibrando con peso de 1 kg...");
        
        // Leer peso actual
        float lectura = scale.get_units(10);
        Serial.print("Lectura: ");
        Serial.println(lectura);
        
        // Calcular nuevo factor
        float nuevoFactor = lectura / (pesoPatronCalibracion / 1000.0);
        scale.set_scale(nuevoFactor);
        factor_calibracion = nuevoFactor;
        
        Serial.print("Nuevo factor: ");
        Serial.println(nuevoFactor);
        
        // Verificar calibración
        float pesoVerificado = scale.get_units(5) * 1000.0;
        Serial.print("Peso verificado: ");
        Serial.print(pesoVerificado);
        Serial.println(" g");
        
      } else if(tecla == 'C') {
        // Salir de calibración
        Serial.println("Saliendo de calibración");
        modoCalibracion = false;
        detenerLecturaBalanza();
        estadoActual = SISTEMA_PRINCIPAL;
      }
      break;
  }
}

// ============================
// SETUP Y LOOP PRINCIPAL
// ============================

void setup() {
  inicializarHardware();
  estadoActual = SISTEMA_PRINCIPAL;
  actualizarPantalla();
}

void loop() {
  // Leer touch
  procesarTouch();
  
  // Procesar según estado actual
  switch(estadoActual) {
    case ESPERANDO_PESO:
      procesarDeteccionPeso();
      break;
      
    case CONTANDO_3_SEGUNDOS:
      procesarConteo3Segundos();
      break;
      
    case EDITANDO_PESO_AUTO:
      procesarEdicionPesoAuto();
      break;
      
    case MODO_CALIBRACION:
      calibrarBalanza();
      break;
      
    default:
      // En otros estados, no leer balanza
      detenerLecturaBalanza();
      break;
  }
  
  // Limpieza automática de registros antiguos (cada hora)
  static unsigned long ultimaLimpieza = 0;
  if(rtcConectado && millis() - ultimaLimpieza > 3600000) {
    limpiarRegistrosAntiguos();
    ultimaLimpieza = millis();
  }
  
  // Actualizar pantalla periódicamente
  static unsigned long ultimaActualizacion = 0;
  if(millis() - ultimaActualizacion > 100) {
    actualizarPantalla();
    ultimaActualizacion = millis();
  }
  
  delay(10);
}