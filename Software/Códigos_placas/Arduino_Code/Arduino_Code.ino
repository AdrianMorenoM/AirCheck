// Pines de los sensores
#define MQ135_PIN A0         // Define el pin analógico A0 para el sensor MQ135 (calidad del aire)
#define MQ2_PIN   A1         // Define el pin analógico A1 para el sensor MQ2 (gas inflamable/humo)
#define GP2Y_LED_PIN 7       // Define el pin digital 7 para controlar el LED del sensor de polvo GP2Y
#define GP2Y_VO_PIN  A2      // Define el pin analógico A2 para leer la salida del sensor de polvo GP2Y

void setup() {
  pinMode(GP2Y_LED_PIN, OUTPUT); // Configura el pin del LED del sensor de polvo como salida
  Serial.begin(115200);          // Inicia la comunicación serial a 115200 baudios para enviar datos (por ejemplo, a una Heltec ESP32)
}

void loop() {
  // Leer valores analógicos de los sensores MQ135 y MQ2
  int mq135_value = analogRead(MQ135_PIN); // Lee el valor del sensor MQ135 (contaminación del aire)
  int mq2_value   = analogRead(MQ2_PIN);   // Lee el valor del sensor MQ2 (humo/gases)

  // Activar el LED del sensor de polvo para iniciar la medición
  digitalWrite(GP2Y_LED_PIN, LOW);     // Enciende el LED del sensor GP2Y (activo en bajo)
  delayMicroseconds(280);             // Espera 280 microsegundos (tiempo recomendado por el fabricante para la lectura)
  int dust_value = analogRead(GP2Y_VO_PIN); // Lee la salida del sensor de polvo mientras el LED está encendido
  delayMicroseconds(40);              // Espera 40 microsegundos adicionales para estabilización
  digitalWrite(GP2Y_LED_PIN, HIGH);   // Apaga el LED del sensor de polvo (se apaga después de la lectura)

  // Enviar todos los datos como un objeto JSON por el puerto serial
  Serial.print("{\"mq135\":");        // Imprime el nombre del campo "mq135"
  Serial.print(mq135_value);          // Imprime el valor leído del MQ135
  Serial.print(",\"mq2\":");          // Imprime el nombre del campo "mq2"
  Serial.print(mq2_value);            // Imprime el valor leído del MQ2
  Serial.print(",\"dust\":");         // Imprime el nombre del campo "dust"
  Serial.print(dust_value);           // Imprime el valor leído del sensor de polvo
  Serial.println("}");                // Cierra el objeto JSON y realiza un salto de línea

  delay(1000); // Espera 1 segundo antes de realizar otra lectura
}
