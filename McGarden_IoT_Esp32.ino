
#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>


#define CE_PIN 22
#define CSN_PIN 21
#define thresholdPin 36    // Potenciómetro
#define TOUCH_PIN_1 14     // Touch Pin para 30%
#define TOUCH_PIN_2 27    // Touch Pin para 70%
#define TOUCH_PIN_MAN 32   // Touch Pin para modo manual
#define touchNav_Pin 33   //Touch Pin Navegar por LCD
#define LCD_SDA 16
#define LCD_SCL 17

// Configuración WiFi
const char* ssid = "McMarciano";
const char* password = "McMa1503";

// Configuración radio
const byte thisSlaveAddress[5] = {'R','x','A','A','A'};
const byte masterAddress[5] = {'T','x','A','A','A'};

LiquidCrystal_I2C lcd(0x27,16,2);
RF24 radio(CE_PIN, CSN_PIN);
WebServer server(80);

// Variables globales
int potValue = 0;
int umbral = 0;
int touchThreshold = 35;  // Sensibilidad touch (ajustar según necesidad)
bool manualMode = true;   // Inicia en modo manual
char dataToSend[32] = "ACK-Eslavo";
unsigned long lastTouchTime = 0;
const long touchDebounce = 500;  // Tiempo anti-rebote en ms
// Variables para navegación LCD
int lcdScreen = 0;
bool buttonReleased = true;
//Almacenar datos enviados del arduino, mayor optimizacion

// Estructura para recibir datos (DEBE COINCIDIR con el Arduino)
struct SensorData
  {
    int16_t humedadSuelo1;
    int16_t humedadSuelo2;
    float temperatura;
    float humedadAire;
  };
SensorData newData;



// Funciones
void handleRoot();
void handleSetMode();
void handleGetData();
void enviarUmbral();
void getData();
void botonNavegador();
void initWiFi();
void checkTouchPins();
void ActualizarUmbral();
void ActualizacionLCD();
String getWebPage();

void setup()
 {
    Serial.begin(115200);
    
    // Configurar sensibilidad de los touch pins
    touchAttachInterrupt(TOUCH_PIN_1, [](){}, touchThreshold);
    touchAttachInterrupt(TOUCH_PIN_2, [](){}, touchThreshold);
    touchAttachInterrupt(TOUCH_PIN_MAN, [](){}, touchThreshold);
    touchAttachInterrupt(touchNav_Pin, [](){}, touchThreshold);
    // Inicializar LCD
    Wire.begin(LCD_SDA, LCD_SCL); //Pines personalizados
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.print("Iniciando...");
    // Inicializar radio
    if (!radio.begin()) 
      {
        Serial.println("Error: Módulo RF24 no detectado!");
        while (1);
      }
    
    // Configurar radio
    radio.setChannel(108);
    radio.setDataRate(RF24_250KBPS);
    radio.setRetries(5, 15);
    radio.setPALevel(RF24_PA_LOW);
    radio.openReadingPipe(1, thisSlaveAddress);
    radio.openWritingPipe(masterAddress);
    radio.startListening();
    
    // Inicialización y Configuración rutas del servidor web
    initWiFi();
    server.on("/", handleRoot);
    server.on("/setMode", handleSetMode);
    server.on("/getData", handleGetData);
    server.begin();
    Serial.println("Sistema iniciado");
    Serial.print("Entrar en la Web con IP");
    Serial.println(WiFi.localIP());
 }

void loop() 
 {
    server.handleClient();
    checkTouchPins();
    ActualizarUmbral();
    enviarUmbral();
    getData();
    botonNavegador();
    ActualizacionLCD();
    delay(10);
 }

void initWiFi() 
  {
    Serial.println();
    Serial.print("Conectando a ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) 
    {
     delay(500);
     Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) 
    {
      Serial.println("\nWiFi conectado");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
     Serial.println("\nError WiFi");
    }
  }

void checkTouchPins() {
    if (millis() - lastTouchTime < touchDebounce) return; 
    if (touchRead(TOUCH_PIN_1) < touchThreshold) {
        manualMode = false;
        umbral = 30;
        lastTouchTime = millis();
        lcd.setCursor(0,0);
        lcd.print("Suelo poco humedo");
    }
    else if (touchRead(TOUCH_PIN_2) < touchThreshold) {
        manualMode = false;
        umbral = 70;
        lastTouchTime = millis();

        lcd.setCursor(0,0);
        lcd.print("Suelo casi humedo");
    }
    else if (touchRead(TOUCH_PIN_MAN) < touchThreshold) {
        manualMode = true;
        lastTouchTime = millis();
        lcd.setCursor(0,0);
        lcd.print("Modo manual activado");
    }
}

void ActualizarUmbral() {
    if (manualMode) {
        int potValue = analogRead(thresholdPin);
        umbral = map(potValue, 0, 4095, 0, 100);
    }
}

void ActualizacionLCD() {
    static unsigned long lastUpdate = 0;
    if(millis() - lastUpdate < 500) return;  
    lcd.setCursor(0, 0);
    
    switch(lcdScreen) {
        case 0: // Pantalla principal
        {
            lcd.clear();
            lcd.print("Humedad 1: ");
            lcd.print(newData.humedadSuelo1);
            lcd.print("%");
            lcd.setCursor(0,1);
            lcd.print("Humedad 2: ");
            lcd.print(newData.humedadSuelo2);
            lcd.print("%");

            break;
        }  
        case 1: 
        {   
            lcd.clear();
            lcd.print("Humedad Aire:");
            lcd.print(newData.humedadAire, 0);
            lcd.print("%");
            lcd.setCursor(0, 1);
            lcd.print("Temperatura: ");
            lcd.print(newData.temperatura, 1);
            lcd.print((char)223);
            lcd.print("ºC");
            break;
        }
        case 2: // Pantalla de configuración
         { 
            lcd.clear();
            lcd.print("Umbral en:");
             lcd.print(umbral);
             lcd.print("%");
            lcd.setCursor(0, 1);
            lcd.print("Programa: ");
            lcd.print(umbral == 30 ? "1" : umbral == 70 ? "2" : "Manual");
            lcd.print("  ");
            break;
         }
        case 3: // Pantalla de conexión
         {
            lcd.clear();
            lcd.print("La Web con IP");
            lcd.setCursor(0, 1);
            lcd.print(WiFi.localIP());
         } 
    }  
      lastUpdate = millis();
}
void botonNavegador()
 {
    const int touchNav = touchRead(touchNav_Pin);
    
    if(touchNav < touchThreshold) { // Botón presionado 
        static unsigned long lastTouchTime = 0;
        if (millis() - lastTouchTime > 900) {     
        lcdScreen = (lcdScreen + 1) % 4; // Rotar entre 0, 1, 2
        lcd.clear();
        lastTouchTime = millis();
    }  
    }
}

void enviarUmbral() {
    static unsigned long lastSend = 0;
    static int lastUmbral = -1;
    
    if (umbral != lastUmbral || millis() - lastSend > 500) {
        radio.stopListening();
        delay(10);
        snprintf(dataToSend, sizeof(dataToSend), "u:%d", umbral);
        bool success = radio.write(&dataToSend, sizeof(dataToSend));
        if (success) {
            Serial.print("Umbral enviado: ");
            Serial.print(umbral);
            Serial.println(manualMode ? "% (Manual)" : "% (Preset)");
            lastUmbral = umbral;
            lastSend = millis();
        } else {
            Serial.println("Error al enviar");
        }     
        radio.startListening();
    }
}

void getData() {
    if (radio.available()) {
        radio.read(&newData, sizeof(newData));
        Serial.print("Datos recibidos - S1:");
        Serial.print(newData.humedadSuelo1);
        Serial.print("%, S2:");
        Serial.print(newData.humedadSuelo2);
        Serial.print("%, Aire:");
        Serial.print(newData.humedadAire);
        Serial.print("%, Temp:");
        Serial.print(newData.temperatura);
        Serial.println("°C");
    }
}

String getWebPage() {
    String page = "<!DOCTYPE html><html><head><title>McGarden Labs</title>";
    page += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    page += "<meta http-equiv='refresh' content='2'>"; // Actualización cada 2 segundos
    page += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css'>";
    page += "<style>";
    page += ":root {";
    page += "  --primary: #4CAF50;";
    page += "  --secondary: #2196F3;";
    page += "  --warning: #ff9800;";
    page += "  --card-bg: #ffffff;";
    page += "  --text-color: #333;";
    page += "}";
    page += "body {";
    page += "  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;";
    page += "  margin: 0; padding: 20px;";
    page += "  background-color: #f5f5f5;";
    page += "  color: var(--text-color);";
    page += "}";
    page += ".container {";
    page += "  max-width: 1000px; margin: 0 auto;";
    page += "  background: white; padding: 20px;";
    page += "  border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1);";
    page += "}";
    page += "h1 { color: var(--primary); text-align: center; }";
    page += ".sensor-grid {";
    page += "  display: grid;";
    page += "  grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));";
    page += "  gap: 15px; margin-bottom: 25px;";
    page += "}";
    page += ".sensor-card {";
    page += "  background: var(--card-bg); border-radius: 8px;";
    page += "  padding: 15px; box-shadow: 0 2px 5px rgba(0,0,0,0.1);";
    page += "  border-left: 4px solid var(--primary);";
    page += "}";
    page += ".sensor-value {";
    page += "  font-size: 28px; font-weight: bold;";
    page += "  text-align: center; margin: 10px 0;";
    page += "  color: var(--primary);";
    page += "}";
    page += ".progress-container { margin: 25px 0; }";
    page += ".progress {";
    page += "  width: 100%; height: 20px;";
    page += "  background-color: #e0e0e0; border-radius: 10px;";
    page += "  overflow: hidden;";
    page += "}";
    page += ".progress-bar {";
    page += "  height: 100%;";
    page += "  background: linear-gradient(90deg, var(--primary), #8BC34A);";
    page += "  border-radius: 10px;";
    page += "  display: flex; align-items: center; justify-content: center;";
    page += "  color: white; font-size: 12px; font-weight: bold;";
    page += "}";
    page += ".btn-group {";
    page += "  display: flex; flex-wrap: wrap; gap: 10px;";
    page += "  margin: 20px 0;";
    page += "}";
    page += ".btn {";
    page += "  padding: 10px 15px; border: none; border-radius: 5px;";
    page += "  cursor: pointer; font-weight: bold;";
    page += "  display: flex; align-items: center; justify-content: center;";
    page += "}";
    page += ".btn i { margin-right: 8px; }";
    page += ".btn-preset {";
    page += "  background: linear-gradient(135deg, var(--secondary), #03A9F4);";
    page += "  color: white;";
    page += "}";
    page += ".btn-manual {";
    page += "  background: linear-gradient(135deg, var(--warning), #FFC107);";
    page += "  color: white;";
    page += "}";
    page += ".active {";
    page += "  box-shadow: 0 0 0 3px rgba(0,0,0,0.2);";
    page += "}";
    page += "@media (max-width: 600px) {";
    page += "  .btn-group { flex-direction: column; }";
    page += "  .btn { width: 100%; }";
    page += "}";
    page += "</style>";
    page += "</head><body>";
    page += "<div class='container'>";
    page += "<h1><i class='fas fa-seedling'></i> McGarden Labs</h1>";
    
    // Sensores
    page += "<div class='sensor-grid'>";
    page += "<div class='sensor-card'>";
    page += "<h2><i class='fas fa-tint'></i> Humedad Suelo 1</h2>";
    page += "<div id='soil1-value' class='sensor-value'>" + String(newData.humedadSuelo1) + "%</div>";
    page += "</div>";
    
    page += "<div class='sensor-card'>";
    page += "<h2><i class='fas fa-tint'></i> Humedad Suelo 2</h2>";
    page += "<div id='soil2-value' class='sensor-value'>" + String(newData.humedadSuelo2) + "%</div>";
    page += "</div>";
    
    page += "<div class='sensor-card'>";
    page += "<h2><i class='fas fa-cloud'></i> Humedad Aire</h2>";
    page += "<div id='air-value' class='sensor-value'>" + String(newData.humedadAire, 0) + "%</div>";
    page += "</div>";
    
    page += "<div class='sensor-card'>";
    page += "<h2><i class='fas fa-thermometer-half'></i> Temperatura</h2>";
    page += "<div id='temp-value' class='sensor-value'>" + String(newData.temperatura, 1) + "C</div>";
    page += "</div>";
    page += "</div>";
    
    // Progreso
    page += "<div class='progress-container'>";
    page += "<div class='progress-label'><span>Umbral de humedad:</span> <span id='umbral-value'>" + String(umbral) + "%</span></div>";
    page += "<div class='progress'><div id='progress-bar' class='progress-bar' style='width:" + String(umbral) + "%'>" + String(umbral) + "%</div></div>";
    page += "</div>";
    
    // Botones
    page += "<h2><i class='fas fa-cogs'></i> Modo de Operacion</h2>";
    page += "<div class='btn-group'>";
    page += "<button id='preset30' class='btn btn-preset";
    if (umbral == 30 && !manualMode) page += " active";
    page += "' onclick='setMode(\"preset30\")'><i class='fas fa-leaf'></i> Suelo poco humedo</button>";
    
    page += "<button id='preset70' class='btn btn-preset";
    if (umbral == 70 && !manualMode) page += " active";
    page += "' onclick='setMode(\"preset70\")'><i class='fas fa-tree'></i> suelo casi humedo</button>";
    
    page += "<button id='manual' class='btn btn-manual";
    if (manualMode) page += " active";
    page += "' onclick='setMode(\"manual\")'><i class='fas fa-sliders-h'></i> Manual</button>";
    page += "</div>";
    
    // JavaScript
    page += "<script>";
    page += "function setMode(mode) {";
    page += "  fetch('/setMode?mode=' + mode).then(response => response.text()).then(data => {";
    page += "    console.log('Mode set:', data);";
    page += "    updateStatus();";
    page += "  });";
    page += "}";
    
    page += "function updateStatus() {";
    page += "  fetch('/getData').then(response => response.text()).then(data => {";
    page += "    const parts = data.split(',');";
    page += "    document.getElementById('soil1-value').innerText = parts[0] + '%';";
    page += "    document.getElementById('soil2-value').innerText = parts[1] + '%';";
    page += "    document.getElementById('air-value').innerText = parts[2] + '%';";
    page += "    document.getElementById('temp-value').innerText = parts[3] + '°C';";
    page += "  });";
    page += "}";
    
    page += "// Actualizar cada 2 segundos (complementario al meta refresh)";
    page += "setInterval(updateStatus, 2000);";
    page += "</script>";
    
    page += "</div></body></html>";
    
    return page;
}

void handleRoot() {
    server.send(200, "text/html", getWebPage());
}

void handleSetMode() {
    if (server.hasArg("mode")) {
        String mode = server.arg("mode");
        
        if (mode == "preset30") {
            manualMode = false;
            umbral = 30;
            server.send(200, "text/plain", "Preset 30%");
        } else if (mode == "preset70") {
            manualMode = false;
            umbral = 70;
            server.send(200, "text/plain", "Preset 70%");
        } else if (mode == "manual") {
            manualMode = true;
            server.send(200, "text/plain", "Manual");
        } else {
            server.send(400, "text/plain", "Modo inválido");
        }
    } else {
        server.send(400, "text/plain", "Falta parámetro mode");
    }
}

void handleGetData() {
    String data = String(newData.humedadSuelo1) + "," + 
                 String(newData.humedadSuelo2) + "," +
                 String(newData.humedadAire, 0) + "," +
                 String(newData.temperatura, 1);
    server.send(200, "text/plain", data);
}