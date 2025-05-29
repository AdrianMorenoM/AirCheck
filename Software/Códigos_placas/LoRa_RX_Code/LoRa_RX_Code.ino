// Librerías necesarias para LoRa, pantalla OLED, ESP-NOW y WiFi
#include "LoRaWan_APP.h"             // Librería del stack LoRaWAN de Heltec
#include "Arduino.h"
#include <Wire.h>                    // Comunicación I2C
#include "HT_SSD1306Wire.h"         // Librería para manejar la pantalla OLED en Heltec
#include <esp_now.h>                // Protocolo ESP-NOW para comunicación entre ESP32
#include <WiFi.h>                   // Librería para configurar WiFi (necesaria para ESP-NOW)

// Dirección MAC del dispositivo receptor para ESP-NOW (debes reemplazar por la MAC real del receptor)
uint8_t peer_addr[] = {0xA0, 0xA3, 0xB3, 0x19, 0x2E, 0xF8};

// Inicializa el objeto de la pantalla OLED (I2C address 0x3C, velocidad 500kHz)
SSD1306Wire factory_display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// Configuración de parámetros para LoRa
#define RF_FREQUENCY        915000000   // Frecuencia LoRa (usar 915MHz en América)
#define TX_OUTPUT_POWER     14          // Potencia de transmisión (dBm)
#define LORA_BANDWIDTH      0           // Ancho de banda (0 = 125kHz)
#define LORA_SPREADING_FACTOR 7         // Factor de dispersión (entre 7 y 12)
#define LORA_CODINGRATE     1           // Tasa de codificación (4/5)
#define LORA_PREAMBLE_LENGTH 8          // Longitud del preámbulo
#define LORA_SYMBOL_TIMEOUT  0          // Tiempo de espera en símbolos
#define LORA_FIX_LENGTH_PAYLOAD_ON false // Deshabilitar longitud fija de paquetes
#define LORA_IQ_INVERSION_ON false      // Desactivar inversión IQ (modo normal)

#define RX_TIMEOUT_VALUE 1000           // Tiempo de espera para recepción (ms)
#define BUFFER_SIZE 128                 // Tamaño del buffer de recepción

char rxpacket[BUFFER_SIZE];            // Arreglo para almacenar el paquete recibido
bool lora_idle = true;                 // Bandera para indicar si LoRa está inactivo

static RadioEvents_t RadioEvents;      // Estructura para eventos LoRa
int16_t rssi, rxSize;                  // Variables para señal recibida

// Callback cuando se envían datos por ESP-NOW
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Envío de datos: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Éxito" : "❌ Falló");
}

// Función para inicializar ESP-NOW
void initESPNow() {
  WiFi.mode(WIFI_STA);  // Requerido para ESP-NOW

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Error inicializando ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);  // Registra el callback de envío

  // Configura el dispositivo receptor
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peer_addr, 6); // Dirección MAC del receptor
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ No se pudo agregar el peer");
  } else {
    Serial.println("✅ Peer agregado");
  }
}

// Control de alimentación externa (activa Vext de la placa)
void VextON(void) {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);  // LOW enciende Vext
}

void VextOFF(void) {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, HIGH); // HIGH apaga Vext
}

// Configuración inicial
void setup() {
  Serial.begin(115200);                         // Inicializa la comunicación serial
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);       // Inicializa la placa Heltec

  WiFi.mode(WIFI_STA);                          // Requerido por ESP-NOW
  initESPNow();                                 // Inicializa protocolo ESP-NOW

  VextON();                                     // Enciende Vext (pantalla OLED)
  delay(100);
  factory_display.init();                       // Inicializa OLED
  factory_display.clear();
  factory_display.display();

  // Configuración LoRa
  RadioEvents.RxDone = OnRxDone;                // Registra función para recepción
  Radio.Init(&RadioEvents);                     // Inicializa radio con eventos
  Radio.SetChannel(RF_FREQUENCY);               // Configura frecuencia
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true); // RX continuo
}

// Bucle principal
void loop() {
  if (lora_idle) {
    lora_idle = false;
    Serial.println("⏳ Esperando paquetes LoRa...");
    Radio.Rx(0); // RX continua (modo espera)
  }
  Radio.IrqProcess(); // Procesa interrupciones LoRa
}

// Función que se ejecuta cuando se recibe un paquete LoRa
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  rxSize = size;
  memcpy(rxpacket, payload, size);    // Copia el contenido recibido
  rxpacket[size] = '\0';              // Termina el string

  Radio.Sleep();                      // Detiene la radio

  String receivedData = String(rxpacket); // Convierte a String
  receivedData.trim();
  Serial.printf("\n📡 Paquete recibido: \"%s\" con RSSI: %d\n", rxpacket, rssi);

  // Extrae los datos del JSON recibido {"mq135":123,"mq2":456,"dust":789}
  int start1 = receivedData.indexOf("\"mq135\":") + 8;
  int end1 = receivedData.indexOf(",", start1);
  if (end1 == -1) end1 = receivedData.indexOf("}", start1);
  String mq135Value = receivedData.substring(start1, end1);
  mq135Value.trim();

  int start2 = receivedData.indexOf("\"mq2\":") + 6;
  int end2 = receivedData.indexOf(",", start2);
  if (end2 == -1) end2 = receivedData.indexOf("}", start2);
  String mq2Value = receivedData.substring(start2, end2);
  mq2Value.trim();

  int start3 = receivedData.indexOf("\"dust\":") + 7;
  int end3 = receivedData.indexOf("}", start3);
  String dustValue = receivedData.substring(start3, end3);
  dustValue.trim();

  // Verifica que los datos hayan sido extraídos correctamente
  if (mq135Value.length() == 0 || mq2Value.length() == 0 || dustValue.length() == 0) {
    Serial.println("❌ Formato JSON inválido.");
    lora_idle = true;
    return;
  }

  // Muestra los datos en pantalla OLED
  factory_display.clear();
  factory_display.setFont(ArialMT_Plain_10);
  factory_display.setTextAlignment(TEXT_ALIGN_CENTER);
  factory_display.drawString(64, 0, "Valores del Sensor");
  factory_display.setTextAlignment(TEXT_ALIGN_LEFT);
  factory_display.drawString(0, 10, "MQ135: " + mq135Value);
  factory_display.drawString(0, 25, "MQ2:   " + mq2Value);
  factory_display.drawString(0, 40, "Polvo: " + dustValue);
  factory_display.display();

  // Prepara el JSON para enviarlo por ESP-NOW
  String jsonString = "{\"mq135\":" + mq135Value + ",\"mq2\":" + mq2Value + ",\"dust\":" + dustValue + "}";
  Serial.println("📤 Enviando por ESP-NOW: " + jsonString);

  // Envío de datos a otro ESP32 mediante ESP-NOW
  esp_err_t result = esp_now_send(peer_addr, (uint8_t *)jsonString.c_str(), jsonString.length());
  if (result != ESP_OK) {
    Serial.println("❌ Error al enviar el paquete por ESP-NOW");
  }

  lora_idle = true; // Vuelve a esperar otro paquete
}
