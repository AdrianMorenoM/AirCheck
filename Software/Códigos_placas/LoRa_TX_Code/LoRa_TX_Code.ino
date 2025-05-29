// Librerías necesarias
#include "LoRaWan_APP.h"   // Librería de Heltec para radio LoRa SX126x
#include "Arduino.h"
#include <Wire.h>          // Comunicación I2C (no se usa en este sketch, pero puede quedar para otros componentes)

// ====== CONFIGURACIÓN LoRa ======
#define RF_FREQUENCY        915000000 // Frecuencia en Hz (915 MHz para América)
#define TX_OUTPUT_POWER     5         // Potencia de transmisión en dBm
#define LORA_BANDWIDTH      0         // Ancho de banda (0 = 125 kHz)
#define LORA_SPREADING_FACTOR 7       // Factor de propagación LoRa (SF7)
#define LORA_CODINGRATE     1         // Tasa de codificación (4/5)
#define LORA_PREAMBLE_LENGTH 8        // Longitud del preámbulo
#define LORA_SYMBOL_TIMEOUT  0        // Tiempo de espera por símbolo
#define LORA_FIX_LENGTH_PAYLOAD_ON false // Longitud de carga útil fija
#define LORA_IQ_INVERSION_ON false    // Inversión de IQ (solo en algunos modos)

// ====== OTRAS CONSTANTES ======
#define RX_TIMEOUT_VALUE 1000         // Tiempo de espera para recepción (ms)
#define BUFFER_SIZE 128               // Tamaño del buffer para el mensaje

// ====== VARIABLES GLOBALES ======
char txpacket[BUFFER_SIZE];          // Buffer para el paquete a enviar por LoRa
bool lora_idle = true;               // Estado del radio: true = listo para enviar

static RadioEvents_t RadioEvents;    // Estructura de eventos de radio (callbacks)

// ====== PROTOTIPOS ======
void OnTxDone(void);     // Callback cuando termina transmisión
void OnTxTimeout(void);  // Callback si ocurre timeout en transmisión

// ====== FUNCIÓN SETUP ======
void setup() {
    Serial.begin(115200);   // Inicializa monitor serie
    Serial2.begin(115200, SERIAL_8N1, 43, 44); // RX=43, TX=44 (puerto serial adicional)

    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE); // Inicializa el microcontrolador y el chip LoRa

    // Asignar callbacks de eventos
    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;

    // Inicializar radio y configuración LoRa
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000); // Timeout de 3s
}

// ====== FUNCIÓN LOOP ======
void loop() {
    // Verifica si hay datos disponibles en el Serial2 (otro micro o sensor)
    if (Serial2.available()) {
        String receivedData = Serial2.readStringUntil('\n'); // Leer línea completa
        receivedData.trim();

        Serial.println("📥 JSON recibido: " + receivedData);

        // Validar si es un JSON válido (inicio y fin con llaves)
        if (receivedData.startsWith("{") && receivedData.endsWith("}")) {
            // Buscar claves específicas
            int mq135Index = receivedData.indexOf("\"mq135\":");
            int mq2Index = receivedData.indexOf("\"mq2\":");
            int dustIndex = receivedData.indexOf("\"dust\":");

            if (mq135Index != -1 && mq2Index != -1 && dustIndex != -1) {
                // ===== EXTRAER VALORES DE JSON =====

                // MQ135
                int comma1 = receivedData.indexOf(",", mq135Index);
                String mq135Value = receivedData.substring(mq135Index + 8, comma1);
                mq135Value.trim();

                // MQ2
                int comma2 = receivedData.indexOf(",", mq2Index);
                String mq2Value = receivedData.substring(mq2Index + 6, comma2);
                mq2Value.trim();

                // Polvo (dust)
                int endBrace = receivedData.indexOf("}", dustIndex);
                String dustValue = receivedData.substring(dustIndex + 7, endBrace);
                dustValue.trim();

                // Armar paquete final en formato JSON para transmitir
                snprintf(txpacket, BUFFER_SIZE, "{\"mq135\":%s,\"mq2\":%s,\"dust\":%s}",
                         mq135Value.c_str(), mq2Value.c_str(), dustValue.c_str());

                Serial.printf("📡 Enviando por LoRa: %s\n", txpacket);

                // Transmitir por LoRa si está libre
                if (lora_idle) {
                    Radio.Send((uint8_t *)txpacket, strlen(txpacket));
                    lora_idle = false; // Bloquea hasta terminar
                }
            } else {
                Serial.println("⚠️ Error: JSON válido pero falta alguna clave esperada.");
            }
        } else {
            Serial.println("❌ Error: Formato JSON inválido.");
        }
    }

    Radio.IrqProcess(); // Procesa interrupciones del radio
}

// ====== CALLBACK cuando transmisión LoRa termina correctamente ======
void OnTxDone(void) {
    Serial.println("✅ Transmisión LoRa completada.");
    lora_idle = true;
}

// ====== CALLBACK si ocurre un timeout durante transmisión LoRa ======
void OnTxTimeout(void) {
    Radio.Sleep();  // Pone en reposo el módulo
    Serial.println("❌ Error: Timeout en transmisión LoRa.");
    lora_idle = true;
}
