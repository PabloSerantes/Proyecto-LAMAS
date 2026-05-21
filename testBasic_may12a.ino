#include "DHT.h"
#include <Wire.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

/*====================== Definiciones =====================*/
#define DAT 17
#define CLK 16
#define RST 5

#define SDA 21
#define SCL 22

#define TEMPDHT22 4
#define DHTTYPE DHT22
/*==================== fin Definiciones ==================*/

//Pines reloj DS1302
ThreeWire myWire(DAT, CLK, RST); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

//Variable para dht22
DHT dht(TEMPDHT22, DHTTYPE);

//Variable para BH1750
BH1750 lightMeter;

//Variables para publicar en dashboard
WiFiClient espClient;
PubSubClient client(espClient);
//Esto para el dashboard
const char* mqtt_server = "thingsboard.cloud";
const char* access_token = "eFeDWbiJ0kWXr1hqDMRq"; // Se obtiene en la plataforma

/*=============== Variables globales ====================*/

//Variables para el ciclo de tareas
unsigned long ready = 0;
unsigned long horaprevia = 0;
unsigned long horaActual = 0;

//Metricas de interes
float humidity = 0, tempDHT = 0;
float lux = 0;

//Datos de conexion a la red
const char* ssid = "GalaxyA15";
const char* password = "1294890";

/*=============== fin Variables globales ================*/

void setup() {
  
  Serial.begin(115200);
  Serial.println("ESP32 OK");

  //--------------- Inicializar RTC ------------------//
  
  Rtc.Begin();
  // Verificar si el RTC está funcionando
  if (!Rtc.IsDateTimeValid()) {
    Serial.println("RTC perdió la fecha/hora! Configurando...");
    Rtc.SetDateTime(CompileDateTime());
  }
  Rtc.SetDateTime(CompileDateTime());
  Serial.println("RTC inicializado.");

  //--------------------------------------------------//
  //--------------- Inicializar DHT22 ----------------//
  
  pinMode(TEMPDHT22, INPUT);
  dht.begin();
  
  //--------------------------------------------------//
  //-------------- Inicializar BH1750 ----------------//
  // Inicializar I2C (SDA = 21, SCL = 22)
  Wire.begin(SDA, SCL);
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println(F("BH1750 inicializado"));
  } else {
    Serial.println(F("Error al inicializar BH1750"));
  }

  //--------------------------------------------------//  
  lcd.begin();
  lcd.backlight();

  //--------------- Pantalla de carga LCD ----------------//
  lcd.setCursor(0, 0);
  lcd.print("Inicializando");
  lcd.setCursor(0, 1);
  lcd.print("Sistema...");
  delay(3000);
  lcd.clear();
  //------------------------------------//

  //--------------- Iniciar conexion wifi ------------------//
  setup_wifi();
  //------------------------------------//  
  client.setServer(mqtt_server, 1883);

}

void loop() {
  
  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  unsigned long ahora = millis();

  if (ahora - ready >= 500) {
    /*Ingresa cada medio segundo*/

    RtcDateTime now = Rtc.GetDateTime();

    horaActual = now.Hour() * 3600UL +
                 now.Minute() * 60UL +
                 now.Second();

    // Cada 15 segundos actualizamos lecturas y impresiones
    if (horaActual - horaprevia >= 15) {

      readDHT();
      readBH1750();
      updateLCD(now);
      sendData();
      horaprevia = horaActual;
    }

    ready = ahora;
  }
}
/*=============== Desarrollo de funciones ====================*/

// Función para obtener fecha/hora de compilación
RtcDateTime CompileDateTime() {
  return RtcDateTime(__DATE__, __TIME__);
}
//Lectura de temperatura y humedad ambiente
void readDHT() {

  humidity = dht.readHumidity();
  tempDHT = dht.readTemperature();

  // Check if reads failed
  if (isnan(humidity) || isnan(tempDHT)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  //Imprimimos en el monitor serial los valores obtenidos
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.print("%  Temperature: ");
  Serial.print(tempDHT);
  Serial.println("°C");
}
//Lectura sensor de luz
void readBH1750() {

  lux = lightMeter.readLightLevel();

  //Imprimimos en el monitor serial
  Serial.print(F("Luz: "));
  Serial.print(lux);
  Serial.println(F(" lx"));
}

// Funcion que escribe en el LCD
void updateLCD(RtcDateTime now) {

  lcd.clear();

  // Primera fila -> Temperatura
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(tempDHT, 1);
  lcd.print((char)223); // simbolo °
  lcd.print("C");

  // Segunda fila -> Luz
  lcd.setCursor(0, 1);
  lcd.print("Luz: ");
  lcd.print((int)lux);
  lcd.print(" lx");
}

/*Funcion destinada a la conexion WIFI */
void setup_wifi() {

  delay(10);

  Serial.println();
  Serial.print("Conectando a ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int intentos = 0;

  while (WiFi.status() != WL_CONNECTED && intentos < 20) {

  delay(500);
  Serial.print(".");
  intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("");
    Serial.println("WiFi conectado");
    Serial.println(WiFi.localIP());

  } else {

    Serial.println("");
    Serial.println("No se pudo conectar al WiFi");
  }

  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.println("IP:");
  Serial.println(WiFi.localIP());
}
/*Nos conectamos al broker mqtt*/
void reconnect() {

  while (!client.connected()) {

    Serial.print("Conectando MQTT...");

    if (client.connect("ESP32_Client", access_token, NULL)) {

      Serial.println("conectado");

    } else {

      Serial.print("Error rc=");
      Serial.print(client.state());
      Serial.println(" reintentando...");

      delay(5000);
    }
  }
}

/*Enviamos datos a la pagina web*/
void sendData() {

  StaticJsonDocument<200> doc;

  doc["airTemperature"] = tempDHT;
  doc["humidity"] = humidity;
  doc["lightIntensity"] = lux;

  char payload[200];

  serializeJson(doc, payload);

  client.publish("v1/devices/me/telemetry", payload);
}