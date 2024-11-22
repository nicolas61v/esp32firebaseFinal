#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>

// Configuración de pines y sensores
#define DHTPIN 4
#define RELE_PIN 2
#define DHTTYPE DHT11

// Configuración WiFi y Firebase
#define WIFI_SSID "mainAzir"
#define WIFI_PASSWORD "saitamadepelos"
#define API_KEY "AIzaSyDOIe-kHO8b24BZRcsz6uV_UzR-koPxH20"
#define DATABASE_URL "https://esp32-sensors-33628-default-rtdb.firebaseio.com/"

// Objetos globales
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
DHT dht(DHTPIN, DHTTYPE);

// Variables de control
unsigned long ultimoEnvio = 0;
unsigned long ultimoHeartbeat = 0;
bool releEstado = false;
bool firebaseInitialized = false;

void conectarWiFi() {
  Serial.print("\nConectando a WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.printf("\nWiFi conectado, IP: %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(RELE_PIN, OUTPUT);
  digitalWrite(RELE_PIN, LOW);
  dht.begin();

  // Conexión WiFi
  conectarWiFi();

  // Configuración Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;  // Importante

  Serial.println("Conectando a Firebase...");
  
  if (Firebase.signUp(&config, &auth, "", "")) {
    firebaseInitialized = true;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    // Esperar conexión Firebase
    while (!Firebase.ready()) {
      delay(100);
    }
    
    // Estado inicial
    FirebaseJson json;
    json.set("estado", "online");
    json.set("rele", false);
    if (Firebase.RTDB.setJSON(&fbdo, "/dispositivos/esp32_1", &json)) {
      Serial.println("Firebase conectado y listo");
    }
  } else {
    Serial.println("Error conectando a Firebase");
  }
}

void loop() {
  // Verificar conectividad
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconectando WiFi...");
    conectarWiFi();
    return;
  }

  if (!firebaseInitialized || !Firebase.ready()) {
    Serial.println("Firebase no está listo");
    return;
  }
  
  unsigned long ahora = millis();

  // Enviar datos cada 20 segundos
  if (ahora - ultimoEnvio >= 20000) {
    float humedad = dht.readHumidity();
    float temperatura = dht.readTemperature();
    
    if (!isnan(humedad) && !isnan(temperatura)) {
      FirebaseJson datos;
      datos.set("lm35_temperatura", 0);
      datos.set("dht11_temperatura", temperatura);
      datos.set("dht11_humedad", humedad);
      datos.set("timestamp", String(ahora));
      
      if (Firebase.RTDB.setJSON(&fbdo, "/sensores/lecturas/inicial", &datos)) {
        Serial.printf("Datos enviados - Temp: %.1f°C, Hum: %.1f%%\n", temperatura, humedad);
      } else {
        Serial.printf("Error enviando datos: %s\n", fbdo.errorReason().c_str());
      }
    } else {
      Serial.println("Error leyendo DHT11");
    }
    ultimoEnvio = ahora;
  }

  // Actualizar heartbeat cada 5 segundos
  if (ahora - ultimoHeartbeat >= 5000) {
    FirebaseJson estado;
    estado.set("estado", "online");
    estado.set("ultima_conexion", String(ahora));
    estado.set("rele", releEstado);
    
    if (Firebase.RTDB.setJSON(&fbdo, "/dispositivos/esp32_1", &estado)) {
      Serial.println("Estado actualizado");
    }
    ultimoHeartbeat = ahora;
  }

  // Verificar estado del relé
  if (Firebase.RTDB.getBool(&fbdo, "/dispositivos/esp32_1/rele")) {
    bool nuevoEstado = fbdo.boolData();
    if (nuevoEstado != releEstado) {
      releEstado = nuevoEstado;
      digitalWrite(RELE_PIN, releEstado);
      Serial.printf("Relé: %s\n", releEstado ? "ON" : "OFF");
    }
  }
}
