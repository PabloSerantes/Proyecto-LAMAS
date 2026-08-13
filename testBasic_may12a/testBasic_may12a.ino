#include "DHT.h"
#include <Wire.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/*====================== Definiciones =====================*/
#define DAT 17
#define CLK 16
#define RST 5

#define SDA 21
#define SCL 22

#define TEMPDHT22 4
#define DHTTYPE DHT22

#define DS18B20 18

#define BUTTON_1 3  /*Pin provisional */
#define BUTTON_2 1  /*Pin provisional */
#define BUTTON_3 39 /*Pin provisional */
#define BUTTON_4 36 /*Pin provisional */

#define NUM_BUTTONS 4
#define DEBOUNCE_TIME 30

typedef enum {
   BUTTON_RELEASED,
   BUTTON_PRESSING,
   BUTTON_PRESSED,
  BUTTON_RELEASING
} ButtonState;
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

// Sensor sumergible
OneWire oneWire(DS18B20);
DallasTemperature sensors(&oneWire);

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

// Estado de la vista del LCD
int StateView = 1;

//Metricas de interes
float humidity = 0, tempDHT = 0;
float lux = 0;
float tempDeep = 0;

//Datos de conexion a la red
const char* ssid = "FCAL";
const char* password = "fcalconcordia.06-2019";

//Variables para el control de pulsadores
// Estado independiente para cada pulsador
ButtonState button_state[NUM_BUTTONS] = { BUTTON_RELEASED, BUTTON_RELEASED, BUTTON_RELEASED, BUTTON_RELEASED };
// Pines de los pulsadores
const uint8_t button_pins[NUM_BUTTONS] = { BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4 };
// Momento en que se detectó el cambio
unsigned long button_timer[NUM_BUTTONS] = { 0, 0, 0, 0 };


/*=============== fin Variables globales ================*/

void setup() {
  
  Serial.begin(115200);
  Serial.println("ESP32 OK");

  //--------------- Configurar pines ------------------//
  pinMode(BUTTON_1, INPUT);
  pinMode(BUTTON_2, INPUT);
  pinMode(BUTTON_3, INPUT); 
  pinMode(BUTTON_4, INPUT);

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
  //------------- Inicialezar el sumergible ----------//
  sensors.begin();
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
      readDS18B20();
      updateLCD(now);
      sendData();
      horaprevia = horaActual;
    }

    ready = ahora;
  }

  int boton = button_update();
    /*
   * Si boton != 0 significa que se confirmó
   * una nueva pulsación.
   */

  if (boton != 0) {

    Serial.print("Pulsador presionado: ");

    Serial.println(boton);

  }

}
/*=============== Desarrollo de funciones ====================*/

// Funcion para actualizar el estado de los pulsadores
int button_update(void) {


  /* Recorremos los cuatro pulsadores.*/

  for (int i = 0; i < NUM_BUTTONS; i++) {


    /*
     * PULL-DOWN:
     *
     * LOW  = liberado
     * HIGH = presionado
     */

    bool pressed = digitalRead(button_pins[i]);


    switch (button_state[i]) {


      /*==================================================*/
      /* BOTÓN LIBERADO                                   */
      /*==================================================*/

      case BUTTON_RELEASED:


        /*
         * Detectamos el comienzo de una pulsación.
         */

        if (pressed) {

          button_state[i] = BUTTON_PRESSING;

          /*
           * Guardamos el momento exacto en que
           * detectamos el cambio.
           */

          button_timer[i] = millis();

        }

        break;


      /*==================================================*/
      /* POSIBLE PULSACIÓN                                */
      /*==================================================*/

      case BUTTON_PRESSING:


        /*
         * Esperamos DEBOUNCE_TIME.
         */

        if (millis() - button_timer[i] >= DEBOUNCE_TIME) {


          /*
           * Después de 30 ms comprobamos nuevamente
           * el estado del pulsador.
           */

          if (digitalRead(button_pins[i]) == HIGH) {


            /*
             * La pulsación fue confirmada.
             */

            button_state[i] = BUTTON_PRESSED;


            /*
             * ==========================================
             *       PULSACIÓN CONFIRMADA
             * ==========================================
             *
             * Devolvemos el número del botón.
             *
             * i = 0 -> botón 1
             * i = 1 -> botón 2
             * i = 2 -> botón 3
             * i = 3 -> botón 4
             */

            return i + 1;


          } else {


            /*
             * El botón volvió a LOW antes de confirmar.
             *
             * Probablemente fue ruido o rebote.
             */

            button_state[i] = BUTTON_RELEASED;

          }

        }

        break;


      /*==================================================*/
      /* BOTÓN PRESIONADO                                 */
      /*==================================================*/

      case BUTTON_PRESSED:


        /*
         * Mientras permanezca HIGH,
         * seguimos considerando que está presionado.
         */

        if (!pressed) {


          /*
           * Detectamos que comenzó la liberación.
           */

          button_state[i] = BUTTON_RELEASING;


          button_timer[i] = millis();

        }

        break;


      /*==================================================*/
      /* POSIBLE LIBERACIÓN                               */
      /*==================================================*/

      case BUTTON_RELEASING:


        /*
         * Esperamos 30 ms para confirmar
         * que realmente fue liberado.
         */

        if (millis() - button_timer[i] >= DEBOUNCE_TIME) {


          if (digitalRead(button_pins[i]) == LOW) {


            /*
             * Liberación confirmada.
             */

            button_state[i] = BUTTON_RELEASED;


          } else {


            /*
             * Volvió a HIGH.
             *
             * Probablemente fue rebote.
             */

            button_state[i] = BUTTON_PRESSED;

          }

        }

        break;


      /*==================================================*/

      default:

        button_state[i] = BUTTON_RELEASED;

        break;

    }

  }


  /*
   * Si recorremos todos los botones y ninguno
   * generó una pulsación confirmada:
   */

  return 0;
}

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

//lectura de la zonda de temperatura
void readDS18B20(){
  sensors.requestTemperatures();
  tempDeep = sensors.getTempCByIndex(0);
  
  if(tempDeep == DEVICE_DISCONNECTED_C){
    Serial.println("Error DS18B20 desconectado");
    return;
  }

  Serial.print(F("Temperatura agua: "));
  Serial.print(tempDeep);
  Serial.println(F(" C"));
}
// Funcion que escribe en el LCD
void updateLCD(RtcDateTime now) {

  lcd.clear();

  switch(StateView){
    
    case 1:
      /*Primer vista LCD*/
      
      /*Mostramos 
      * Temperatura del sensor DHT22
      * Humedad relativa del sensor DHT22
      */
      // Primera fila -> Temperatura
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(tempDHT, 1);
      lcd.print((char)223); // simbolo °
      lcd.print("C");
      
      // Segunda fila -> Humedad
      lcd.setCursor(0, 1);
      lcd.print("Humedad: ");
      lcd.print(humidity, 1);
      lcd.print("%");
      
      break;
    case 2:
      /*Segunda vista LCD*/
      
      /*Mostramos 
      * Temperatura de la zonda
      * Lux en el habiemte
      */
      // Primera fila -> Temperatura agua
      lcd.setCursor(0, 0);
      lcd.print("Agua: ");
      lcd.print(tempDeep, 1);
      lcd.print((char)223);
      lcd.print("C");

      // Segunda fila -> Luz
      lcd.setCursor(0, 1);
      lcd.print("Luz: ");
      lcd.print((int)lux);
      lcd.print(" lx");
      
      break;
    default:
      // Si por algún motivo StateView
      // toma un valor inválido
      StateView = 1;
      break;
  }

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
  doc["waterTemperature"] = tempDeep;

  char payload[200];

  serializeJson(doc, payload);

  client.publish("v1/devices/me/telemetry", payload);
}