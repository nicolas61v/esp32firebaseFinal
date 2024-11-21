#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>

// Definiciones de pines
#define LM35_PIN 34
#define DHTPIN 4
#define RELE_PIN 2
#define DHTTYPE DHT11

// Credenciales WiFi
#define WIFI_SSID "mainAzir"
#define WIFI_PASSWORD "saitamadepelos"

// Credenciales Firebase
#define API_KEY "AIzaSyDOIe-kHO8b24BZRcsz6uV_UzR-koPxH20"
#define DATABASE_URL "https://esp32-sensors-33628-default-rtdb.firebaseio.com/"

// Define objetos Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Define objeto DHT
DHT dht(DHTPIN, DHTTYPE);

// Variables para control de tiempo
unsigned long sendDataPrevMillis = 0;
unsigned long heartbeatPrevMillis = 0;
const long INTERVALO_ENVIO = 10000;     // 10 segundos
const long INTERVALO_HEARTBEAT = 5000;   // 5 segundos
const int TIMEOUT_FIREBASE = 15000;      // 15 segundos timeout

// Variables para almacenar lecturas
float lm35Temp = 0.0;
float dhtTemp = 0.0;
float dhtHum = 0.0;
bool releEstado = false;
bool firebaseInitialized = false;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Iniciando Sistema de Monitoreo ESP32 ===");
  
  // Configurar pines
  pinMode(RELE_PIN, OUTPUT);
  pinMode(LM35_PIN, INPUT);
  digitalWrite(RELE_PIN, LOW);
  Serial.println("✓ Pines configurados");

  // Iniciar DHT
  dht.begin();
  Serial.println("✓ Sensor DHT11 iniciado");

  // Conectar WiFi
  if (!conectarWiFi()) {
    Serial.println("❌ Fallo crítico en conexión WiFi");
    ESP.restart();
    return;
  }

  // Configurar Firebase
  configurarFirebase();
}

bool conectarWiFi() {
  Serial.println("\nConectando a WiFi...");
  Serial.printf("SSID: %s\n", WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long startAttemptTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && 
         millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi conectado");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    return true;
  }
  
  Serial.println("\n❌ Conexión WiFi fallida");
  return false;
}

void configurarFirebase() {
  Serial.println("\nConfigurando Firebase...");
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.timeout.serverResponse = TIMEOUT_FIREBASE;

// Añade estas líneas
  auth.user.email = "";           // Deja vacío para auth anónima
  auth.user.password = "";        // Deja vacío para auth anónima
  config.token_status_callback = tokenStatusCallback; // Requerido para autenticación

  Serial.println("Iniciando conexión a Firebase...");
  
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("✓ Autenticación exitosa");
    firebaseInitialized = true;
  } else {
    Serial.printf("❌ Error en autenticación: %s\n", config.signer.signupError.message.c_str());
    return;
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  unsigned long startAttemptTime = millis();
  Serial.println("Esperando conexión Firebase...");
  
  while (!Firebase.ready() && millis() - startAttemptTime < TIMEOUT_FIREBASE) {
    delay(100);
    Serial.print(".");
  }
  Serial.println();

  if (Firebase.ready()) {
    Serial.println("✓ Firebase conectado");
    establecerEstadoInicial();
  } else {
    Serial.println("❌ Timeout en conexión Firebase");
    firebaseInitialized = false;
  }
}

void establecerEstadoInicial() {
  Serial.println("\nEstableciendo estado inicial...");
  
  // Estructura inicial para dispositivo
  FirebaseJson json_dispositivo;
  json_dispositivo.set("estado", "online");
  json_dispositivo.set("ultima_conexion", String(millis()));
  json_dispositivo.set("rele", false);
  
  if (Firebase.RTDB.setJSON(&fbdo, "/dispositivos/esp32_1", &json_dispositivo)) {
    Serial.println("✓ Estado inicial del dispositivo establecido");
  } else {
    Serial.printf("❌ Error estableciendo estado inicial: %s\n", fbdo.errorReason().c_str());
  }
  
  // Estructura inicial para sensores
  FirebaseJson json_sensores;
  json_sensores.set("lm35_temperatura", 0);
  json_sensores.set("dht11_temperatura", 0);
  json_sensores.set("dht11_humedad", 0);
  json_sensores.set("timestamp", String(millis()));
  
  if (Firebase.RTDB.setJSON(&fbdo, "/sensores/lecturas/inicial", &json_sensores)) {
    Serial.println("✓ Estado inicial de sensores establecido");
  } else {
    Serial.printf("❌ Error estableciendo sensores: %s\n", fbdo.errorReason().c_str());
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Conexión WiFi perdida");
    if (!conectarWiFi()) {
      delay(5000);
      ESP.restart();
      return;
    }
  }

  if (!firebaseInitialized || !Firebase.ready()) {
    Serial.println("Reintentando configuración Firebase...");
    configurarFirebase();
    delay(5000);
    return;
  }

  unsigned long currentMillis = millis();
  
  // Actualizar heartbeat
  if (currentMillis - heartbeatPrevMillis >= INTERVALO_HEARTBEAT) {
    heartbeatPrevMillis = currentMillis;
    actualizarEstadoDispositivo();
  }

  // Leer y enviar datos
  if (currentMillis - sendDataPrevMillis >= INTERVALO_ENVIO) {
    sendDataPrevMillis = currentMillis;
    leerSensores();
    enviarDatosFirebase();
  }

  verificarRele();
}

void leerSensores() {
  // Leer LM35
  int rawValue = analogRead(LM35_PIN);
  lm35Temp = (rawValue * 3.3 / 4095.0) * 100.0;
  
  // Leer DHT11
  float nuevaHum = dht.readHumidity();
  float nuevaTemp = dht.readTemperature();

  if (!isnan(nuevaHum) && !isnan(nuevaTemp)) {
    dhtHum = nuevaHum;
    dhtTemp = nuevaTemp;
    Serial.printf("Lecturas - LM35: %.1f°C, DHT11: %.1f°C, %.1f%%\n", 
                 lm35Temp, dhtTemp, dhtHum);
  } else {
    Serial.println("❌ Error leyendo DHT11");
  }
}

void enviarDatosFirebase() {
  if (!Firebase.ready()) return;

  FirebaseJson json_lectura;
  String timestamp = String(millis());

  json_lectura.set("lm35_temperatura", lm35Temp);
  json_lectura.set("dht11_temperatura", dhtTemp);
  json_lectura.set("dht11_humedad", dhtHum);
  json_lectura.set("timestamp", timestamp);

  String path = "/sensores/lecturas/" + timestamp;
  
  if (Firebase.RTDB.setJSON(&fbdo, path, &json_lectura)) {
    Serial.println("✓ Datos enviados a: " + path);
  } else {
    Serial.printf("❌ Error enviando datos: %s\n", fbdo.errorReason().c_str());
  }
}

void actualizarEstadoDispositivo() {
  if (!Firebase.ready()) return;

  FirebaseJson json_estado;
  json_estado.set("estado", "online");
  json_estado.set("ultima_conexion", String(millis()));
  json_estado.set("rele", releEstado);
  
  if (Firebase.RTDB.setJSON(&fbdo, "/dispositivos/esp32_1", &json_estado)) {
    Serial.println("✓ Estado actualizado");
  } else {
    Serial.printf("❌ Error actualizando estado: %s\n", fbdo.errorReason().c_str());
  }
}

void verificarRele() {
  if (!Firebase.ready()) return;

  if (Firebase.RTDB.getBool(&fbdo, "/dispositivos/esp32_1/rele")) {
    bool nuevoEstado = fbdo.boolData();
    if (nuevoEstado != releEstado) {
      releEstado = nuevoEstado;
      digitalWrite(RELE_PIN, releEstado);
      Serial.printf("Relé cambiado a: %s\n", releEstado ? "ON" : "OFF");
    }
  } else {
    Serial.printf("❌ Error leyendo relé: %s\n", fbdo.errorReason().c_str());
  }
}
