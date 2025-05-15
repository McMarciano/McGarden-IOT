#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DHT.h>

#define CE_PIN   9
#define CSN_PIN 10
#define sensor1 A0
#define sensor2 A1
#define Mosfet_PIN 2
#define Led_Pin 4
#define DHTPIN 3
#define DHTTYPE DHT11


const byte slaveAddress[5] = {'R','x','A','A','A'};
const byte thisMasterAddress[5] = {'T','x','A','A','A'};

RF24 radio(CE_PIN, CSN_PIN);
DHT dht(DHTPIN, DHTTYPE);

struct SensorData {
  int humedadSuelo1;
  int humedadSuelo2;
  float temperatura;
  float humedadAire;
};

char dataReceived[32] = {0};
int umbralRecibido = 0;
bool nuevoUmbral = false;

unsigned long currentMillis;
unsigned long prevSensorRead = 0;
const unsigned long sensorInterval = 2000;
unsigned long prevTxMillis = 0;
const unsigned long txInterval = 2000;
unsigned long MosfetStartTime = 0;
const unsigned long MosfetDuracion = 10000;
bool MosfetActiva = false;

// Variables renombradas consistentemente
int lastHumedadSuelo1 = 0;
int lastHumedadSuelo2 = 0;
float lastTemperatura = 0;
float lastHumedadAire = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("Iniciando Maestro con DHT11");
    
    dht.begin();
    
    if (!radio.begin()) {
        Serial.println("Error: Módulo RF24 no detectado!");
        while (1);
    }
    
    radio.setChannel(108);
    radio.setDataRate(RF24_250KBPS);
    radio.setRetries(5, 15);
    radio.setPALevel(RF24_PA_LOW);
    radio.openWritingPipe(slaveAddress);
    radio.openReadingPipe(1, thisMasterAddress);
    radio.startListening();

    pinMode(Mosfet_PIN, OUTPUT);
    digitalWrite(Mosfet_PIN, LOW);
    pinMode(Led_Pin, OUTPUT);
    digitalWrite(Led_Pin, LOW);
    leerSensores();
}

void loop() {
    currentMillis = millis();
    
    if (currentMillis - prevSensorRead >= sensorInterval) {
        prevSensorRead = currentMillis;
        leerSensores();
    }
    
    if (currentMillis - prevTxMillis >= txInterval) {
        prevTxMillis = currentMillis;
        enviarDatosSensores();
    }
    
    recibirDatosControl();
    controlBomba();
}

void leerSensores() {
    // Humedad del suelo
    int sumaH1 = 0, sumaH2 = 0;
    const int muestras = 5;
    
    for(int i = 0; i < muestras; i++) {
        sumaH1 += analogRead(sensor1);
        sumaH2 += analogRead(sensor2);
        delay(50);
    }
    
    lastHumedadSuelo1 = map(sumaH1/muestras, 0, 1023, 100, 0);
    lastHumedadSuelo2 = map(sumaH2/muestras, 0, 1023, 100, 0);
    
    // Lectura DHT11 con verificación de errores CORREGIDA
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    if (!isnan(h)) { lastHumedadAire = h; }
    if (!isnan(t)) { lastTemperatura = t; }
    
    Serial.print("Lecturas - S1:");
    Serial.print(lastHumedadSuelo1);
    Serial.print("%, S2:");
    Serial.print(lastHumedadSuelo2);
    Serial.print("%, Aire:");
    Serial.print(lastHumedadAire);
    Serial.print("%, Temp:");
    Serial.print(lastTemperatura);
    Serial.println("°C");
}

void enviarDatosSensores() {
    radio.stopListening();
    delay(10);
    
    SensorData datos;
    datos.humedadSuelo1 = lastHumedadSuelo1;
    datos.humedadSuelo2 = lastHumedadSuelo2;
    datos.temperatura = lastTemperatura;
    datos.humedadAire = lastHumedadAire;

    bool resultado = radio.write(&datos, sizeof(datos));
    Serial.print("Enviando - S1:");
    Serial.print(datos.humedadSuelo1);
    Serial.print("%, S2:");
    Serial.print(datos.humedadSuelo2);
    Serial.print("%, Aire:");
    Serial.print(datos.humedadAire);
    Serial.print("%, Temp:");
    Serial.print(datos.temperatura);
    Serial.println(resultado ? "°C -> OK" : "°C -> Fallo");
    
    radio.startListening();
}

void recibirDatosControl() {
    if (radio.available()) {
        memset(dataReceived, 0, sizeof(dataReceived));
        radio.read(&dataReceived, sizeof(dataReceived));
        
        if (strncmp(dataReceived, "u:", 2) == 0) {
            umbralRecibido = atoi(dataReceived + 2);
            nuevoUmbral = true;
            
            Serial.print("Umbral recibido: ");
            Serial.print(umbralRecibido);
            Serial.println("%");
        }
    }
}

void controlBomba() {
    if (nuevoUmbral) {
        nuevoUmbral = false;
        
        // Usar las variables renombradas CORRECTAMENTE
        bool debeActivar = (umbralRecibido > lastHumedadSuelo1) || (umbralRecibido > lastHumedadSuelo2);
        
        if (debeActivar && !MosfetActiva) {
            digitalWrite(Mosfet_PIN, HIGH);
            digitalWrite(Led_Pin, HIGH);
            MosfetStartTime = currentMillis;
            MosfetActiva = true;
            Serial.println("Bomba ACTIVADA por 10 segundos");
        }
    }
    
    if (MosfetActiva && (currentMillis - MosfetStartTime >= MosfetDuracion)) {
        digitalWrite(Mosfet_PIN, LOW);
        digitalWrite(Led_Pin, LOW);
        MosfetActiva = false;
        Serial.println("Bomba DESACTIVADA (tiempo completado)");
    }
}