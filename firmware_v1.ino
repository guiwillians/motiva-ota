#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

// ====== CONFIGURE ESTES VALORES ======
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* MANIFEST_URL = "https://guiwillians.github.io/motiva-ota/firmware_v2.bin";
// =====================================

const char* INSTALLED_VERSION = "1.0";
const unsigned long READ_INTERVAL_MS = 2000;
const unsigned long SESSION_INTERVAL_MS = 48000;
const int NUM_READINGS = 5;

const int LED_R = 25;
const int LED_G = 26;
const int LED_B = 27;

int readings[NUM_READINGS];
int readingIndex = 0;
unsigned long sessionStart = 0;
unsigned long nextReadingAt = 0;
int completedSessions = 0;
bool otaChecked = false;

void setLed(bool red, bool green, bool blue) {
  digitalWrite(LED_R, red ? HIGH : LOW);
  digitalWrite(LED_G, green ? HIGH : LOW);
  digitalWrite(LED_B, blue ? HIGH : LOW);
}

void printHeader() {
  Serial.println("========================================");
  Serial.println("MONITORAMENTO DE VEGETACAO - FW 1.0");
  Serial.println("========================================");
}

void startSession(unsigned long startTime) {
  sessionStart = startTime;
  nextReadingAt = startTime;
  readingIndex = 0;
  Serial.println();
  Serial.printf("Inicio da sessao %d (t=%lu ms)\n", completedSessions + 1, sessionStart);
}

void finishSession() {
  long sum = 0;
  for (int i = 0; i < NUM_READINGS; i++) sum += readings[i];
  Serial.printf("Media da sessao: %.1f cm\n", sum / (float)NUM_READINGS);
  Serial.println("Proxima sessao em 48 segundos.");
  completedSessions++;
}

void takeReading() {
  readings[readingIndex] = random(10, 21); // 10 a 20, inclusive
  Serial.printf("Leitura %d: %d cm\n", readingIndex + 1, readings[readingIndex]);
  readingIndex++;
  nextReadingAt += READ_INTERVAL_MS;

  if (readingIndex == NUM_READINGS) {
    finishSession();
    // A proxima sessao e calculada a partir do inicio desta, nao da ultima leitura.
    sessionStart += SESSION_INTERVAL_MS;
    nextReadingAt = sessionStart;
    readingIndex = NUM_READINGS;
  }
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.printf("Conectando a rede %s...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERRO: nao foi possivel conectar ao Wi-Fi.");
    return false;
  }
  Serial.print("Wi-Fi conectado. IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool isNewerVersion(const String& available) {
  return available.toFloat() > String(INSTALLED_VERSION).toFloat();
}

bool performOTA(const String& firmwareUrl) {
  Serial.println("Baixando firmware_v2.bin...");
  WiFiClientSecure client;
  client.setInsecure(); // adequado para a simulacao; em produto use certificado CA
  HTTPClient http;
  if (!http.begin(client, firmwareUrl)) {
    Serial.println("ERRO: nao foi possivel iniciar o download.");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("ERRO: download retornou HTTP %d.\n", code);
    http.end();
    return false;
  }

  int total = http.getSize();
  if (total <= 0) {
    Serial.println("ERRO: tamanho do firmware invalido.");
    http.end();
    return false;
  }
  if (!Update.begin(total)) {
    Serial.printf("ERRO: Update.begin falhou: %s\n", Update.errorString());
    http.end();
    return false;
  }

  size_t written = Update.writeStream(http.getStream());
  bool ok = written == (size_t)total && Update.end(true);
  http.end();
  if (!ok) {
    Serial.printf("ERRO: OTA falhou: %s\n", Update.errorString());
    return false;
  }
  Serial.println("OTA concluida. Reiniciando o ESP32...");
  delay(1000);
  ESP.restart();
  return true;
}

void checkForUpdate() {
  if (otaChecked) return;
  otaChecked = true;
  Serial.println();
  Serial.println("Consultando manifesto remoto apos 3 sessoes...");
  if (!connectWiFi()) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, MANIFEST_URL)) {
    Serial.println("ERRO: nao foi possivel acessar o manifesto.");
    return;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("ERRO: manifesto retornou HTTP %d.\n", code);
    http.end();
    return;
  }

  String body = http.getString();
  http.end();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.printf("ERRO: manifesto JSON invalido: %s\n", error.c_str());
    return;
  }
  String available = doc["version"] | "";
  String firmwareUrl = doc["url"] | "";
  Serial.printf("Versao instalada: %s | Disponivel: %s\n", INSTALLED_VERSION, available.c_str());
  if (available.length() == 0 || firmwareUrl.length() == 0) {
    Serial.println("ERRO: manifesto sem version ou url.");
    return;
  }
  if (!isNewerVersion(available)) {
    Serial.println("Nenhuma atualizacao necessaria.");
    return;
  }
  Serial.printf("Atualizacao %s disponivel.\n", available.c_str());
  performOTA(firmwareUrl);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(false, false, true); // FW 1.0 = azul
  randomSeed(micros());
  printHeader();
  Serial.println("LED azul: Firmware 1.0 em execucao");
  startSession(millis());
}

void loop() {
  unsigned long now = millis();

  if (readingIndex < NUM_READINGS && (long)(now - nextReadingAt) >= 0) {
    takeReading();
  }

  if (readingIndex == NUM_READINGS && (long)(now - sessionStart) >= 0) {
    startSession(sessionStart);
  }

  if (completedSessions >= 3) checkForUpdate();
  delay(5);
}
