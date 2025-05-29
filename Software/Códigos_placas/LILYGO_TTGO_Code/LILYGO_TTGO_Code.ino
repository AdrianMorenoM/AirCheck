// Define el modelo exacto del módem GSM (SIM7070 en este caso)
#define TINY_GSM_MODEM_SIM7070

// Usa el puerto Serial principal para monitorización/debug
#define SerialMon Serial

/*
 * Configuración del puerto serial para el módem:
 * - En la TTGO T-SIM7070G usamos Serial2 (pines 16-RX, 17-TX)
 * - El condicional AVR es innecesario en esta placa (es ESP32)
 * - Se mantiene por compatibilidad pero siempre usará Serial2
 */
#ifndef AVR_Atmega328P
  #define SerialAT Serial2  // HardwareSerial para el módem en ESP32
#else
  #include <SoftwareSerial.h>
  SoftwareSerial SerialAT(2, 3);  // No aplica para TTGO
#endif

// Habilita debug de la librería TinyGSM por SerialMon
#define TINY_GSM_DEBUG SerialMon

// Habilita funcionalidad GPRS (necesario para datos móviles)
#define TINY_GSM_USE_GPRS true

// Deshabilita WiFi (ahorra memoria, ya que usamos GPRS)
#define TINY_GSM_USE_WIFI false

// Configuración APN para Telcel México
const char apn[] = "internet.itelcel.com";
const char gprsUser[] = "webgprs";      // Usuario común para Telcel
const char gprsPass[] = "webgprs2002";  // Contraseña común

// Configuración MQTT (broker público HiveMQ)
const char* broker = "broker.hivemq.com";
const char* topicData = "AirCheck";  // Tópico para publicar datos

/*
 * Inclusión de librerías específicas:
 * - ESP-NOW para comunicación peer-to-peer con otros ESP32
 * - WiFi para funcionalidad de red (requerida por ESP-NOW)
 * - ArduinoJson para manejo eficiente de datos JSON
 * - TinyGsmClient para manejo del módem SIM7070
 * - PubSubClient para protocolo MQTT
 */
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>  // Versión 6+ recomendada para ESP32
#include <TinyGsmClient.h>
#include <PubSubClient.h>

// Objeto modem usando el puerto SerialAT configurado
TinyGsm modem(SerialAT);

// Cliente GPRS basado en el modem
TinyGsmClient client(modem);

// Cliente MQTT que usa el cliente GPRS
PubSubClient mqtt(client);

// Variable para controlar reintentos de conexión MQTT
uint32_t lastReconnectAttempt = 0;

// String para almacenar datos JSON a enviar
String jsonData = "";

// Pin del LED integrado en la placa TTGO (varía según modelo)
#define LED_PIN 12

/*
 * Callback MQTT - Se ejecuta al recibir mensajes suscritos
 * @param topic Tópico del mensaje recibido
 * @param payload Contenido del mensaje
 * @param len Longitud del payload
 */
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  SerialMon.print("📩 Mensaje recibido [");
  SerialMon.print(topic);
  SerialMon.print("]: ");
  SerialMon.write(payload, len);  // Imprime el contenido crudo
  SerialMon.println();
}

/*
 * Función para conectar al broker MQTT
 * @return true si la conexión fue exitosa
 */
boolean mqttConnect() {
  SerialMon.print("🔌 Conectando a MQTT: ");
  SerialMon.println(broker);
  
  // Intenta conectar con ID único
  boolean status = mqtt.connect("ESP32SIMClient");
  
  if (!status) {
    SerialMon.println("❌ Falló conexión MQTT");
    return false;
  }
  
  SerialMon.println("✅ Conectado a MQTT");
  return true;
}

/*
 * Callback ESP-NOW - Se ejecuta al recibir datos
 * @param recv_info Información del remitente
 * @param incomingData Datos recibidos
 * @param len Longitud de los datos
 */
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  // Convertir datos a String null-terminated
  char buffer[len + 1];
  memcpy(buffer, incomingData, len);
  buffer[len] = '\0';
  String rawData = String(buffer);

  SerialMon.print("📡 JSON recibido: ");
  SerialMon.println(rawData);

  // Parsear JSON (tamaño fijo 200 bytes para eficiencia)
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, rawData);

  if (error) {
    SerialMon.print("❌ Error parseando JSON: ");
    SerialMon.println(error.c_str());
    return;
  }

  // Extraer valores de sensores (ajustar claves según dispositivo emisor)
  float sensor1 = doc["mq135"];  // Sensor de CO₂
  float sensor2 = doc["mq2"];    // Sensor de humo/gas
  float sensor3 = doc["dust"];   // Sensor de polvo

  // Reconstruir JSON en formato para Node-RED
  jsonData = "{\"sensor1\":" + String(sensor1, 2) + 
             ",\"sensor2\":" + String(sensor2, 2) + 
             ",\"sensor3\":" + String(sensor3, 2) + "}";

  SerialMon.print("✅ JSON procesado: ");
  SerialMon.println(jsonData);
}

/*
 * Setup - Configuración inicial
 */
void setup() {
  // Iniciar serial para debug a 115200 bauds
  SerialMon.begin(115200);
  delay(10);  // Breve espera para estabilización

  // Configurar LED de estado
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  /*
   * Secuencia de encendido del módem SIM7070G:
   * 1. Poner HIGH el pin PWRKEY por 1.5 segundos
   * 2. Poner LOW para completar la secuencia
   * 3. Esperar inicialización (6 segundos recomendados)
   */
  pinMode(4, OUTPUT);        // Pin PWRKEY del módem
  digitalWrite(4, HIGH);     // Inicia pulso de encendido
  delay(1000);               // 1 segundo (mejor que 1500ms para SIM7070)
  digitalWrite(4, LOW);      // Completa secuencia
  delay(100);                // Pequeña espera adicional

  SerialMon.println("📶 Inicializando módem...");
  
  // Iniciar comunicación serial con módem a 115200 bauds
  SerialAT.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17
  delay(6000);  // Espera crítica para inicialización del módem

  // Reiniciar módem para asegurar estado conocido
  modem.restart();
  delay(1000);

  // Verificar presencia de tarjeta SIM
  if (modem.getSimStatus() != SIM_READY) {
    SerialMon.println("❌ SIM no detectada");
    return;  // Bloquea ejecución si no hay SIM
  }

  // Esperar conexión a red móvil
  SerialMon.println("Buscando red...");
  if (!modem.waitForNetwork()) {
    SerialMon.println("❌ Red no disponible");
    return;
  }

  // Conectar GPRS con credenciales APN
  SerialMon.println("Conectando GPRS...");
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println("❌ Error en conexión GPRS");
    return;
  }

  SerialMon.println("✅ GPRS conectado");

  // Configurar cliente MQTT
  mqtt.setServer(broker, 1883);  // Broker y puerto estándar
  mqtt.setCallback(mqttCallback); // Asignar función callback

  /*
   * Configurar ESP-NOW:
   * 1. Modo estación WiFi (requerido aunque no use WiFi)
   * 2. Desconectar de redes WiFi
   * 3. Inicializar protocolo ESP-NOW
   * 4. Registrar callback de recepción
   */
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != ESP_OK) {
    SerialMon.println("❌ Error iniciando ESP-NOW");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
}

/*
 * Loop - Bucle principal
 */
void loop() {
  // Verificar y mantener conexión GPRS
  if (!modem.isNetworkConnected() || !modem.isGprsConnected()) {
    SerialMon.println("🔄 Reconectando GPRS...");
    modem.gprsConnect(apn, gprsUser, gprsPass);
    delay(5000);  // Espera después de reconexión
  }

  // Manejar conexión MQTT con reintentos cada 10 segundos
  if (!mqtt.connected()) {
    uint32_t t = millis();
    if (t - lastReconnectAttempt > 10000L) {  // 10 segundos
      lastReconnectAttempt = t;
      if (mqttConnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqtt.loop();  // Mantiene conexión MQTT activa

    // Publicar datos si hay JSON válido
    if (jsonData.length() > 0) {
      SerialMon.println("📤 Enviando a MQTT:");
      SerialMon.println(jsonData);

      if (mqtt.publish(topicData, jsonData.c_str())) {
        SerialMon.println("✅ Publicado exitosamente");
        // Feedback visual con LED
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
      } else {
        SerialMon.println("❌ Error al publicar");
      }

      jsonData = "";  // Limpiar buffer después de enviar
    }
  }
}