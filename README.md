# AirCheck
Este proyecto busca medir la calidad del aire en áreas cercanas a zonas industriales utilizando sensores de gases contaminantes (MQ-2, MQ-135) y polvo (GP2Y1010AU0F), transmitiendo los datos de forma inalámbrica mediante módulos LoRa hacia una estación central basada en LilyGO TTGO (con ESP32). Los datos pueden visualizarse en tiempo real

## Arquitectura del Sistema

El sistema se compone de varias etapas, cada una con una función específica dentro del proceso de captura, transmisión y visualización de los datos.

### 🔹 Arduino Uno
- **Función:** Lectura de sensores de gas y polvo.
- **Salida de datos:** Objeto JSON con la siguiente estructura:
  ```json
  {"MQ-2":120,"M-135":128,"DUST":200}
  ```
- **Comunicación:** Transmisión de datos mediante **UART** hacia la placa Heltec LoRa 32 V3.

- ### 🔹 Heltec LoRa 32 V3 – Nodo 1
- **Función:** Recepción del JSON vía UART, visualización en pantalla OLED, y envío inalámbrico mediante **LoRa** a un segundo nodo Heltec.

### 🔹 Heltec LoRa 32 V3 – Nodo 2
- **Función:** Recepción de datos LoRa, visualización local, y retransmisión usando **ESP-NOW** hacia una placa LilyGO TTGO SIM7600.

### 🔹 LilyGO TTGO SIM7600
- **Función:** Recepción del JSON vía ESP-NOW y posterior envío a internet mediante red móvil 3G.
- **Protocolo utilizado:** **MQTT**, hacia el broker público [HiveMQ](https://www.hivemq.com/).
 
### 🔹 Node-RED
- **Función:** Suscripción al tópico MQTT, visualización de datos en tiempo real a través de dashboards personalizados.

## Tecnologías utilizadas
- Arduino Uno  
- Heltec LoRa 32 V3 (x2)  
- LilyGO TTGO SIM7070G
- HiveMQ  
- Node-RED  
- 2 sensores de la serie MQ
- Sensor de polvo
