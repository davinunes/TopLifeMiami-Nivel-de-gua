/*
  SKETCH PORTADO PARA ESP32-C3 COM DISPLAY OLED 0.42"
  - Biblioteca de display alterada para U8g2.
  - Mantém todas as funcionalidades originais.

  Bibliotecas necessárias (instalar via Gerenciador de Bibliotecas do Arduino IDE):
  - "U8g2" by oliver
  - "Ultrasonic" by Erick Simões
  - "ArduinoJson" by Benoit Blanchon
*/

// ==================== BIBLIOTECAS ====================
#include <WiFi.h>
#include "esp_wifi.h"
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <U8g2lib.h> // <--- SUBSTITUÍDA
#include <Ultrasonic.h>
#include <Wire.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Update.h>

// ==================== CONFIGURAÇÕES DA PLACA E FIRMWARE ====================
#define BOARD_MODEL "ESP32-C3"
#define FW_VERSION 2.0

// ==================== CONFIGURAÇÕES DE PINOS (ESP32-C3) ====================
#define LED_PIN 2
#define TRIGGER_PIN 10
#define ECHO_PIN 9
#define I2C_SDA_PIN 5
#define I2C_SCL_PIN 6

// ==================== CONFIGURAÇÕES U8G2 ====================
// Construtor para o display SSD1306 de 72x40 pixels em modo I2C de hardware.
// U8G2_R0 = Sem rotação. Se a tela ficar de cabeça para baixo, troque para U8G2_R2.
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);


// ==================== CONFIGURAÇÕES GERAIS ====================
#define AP_MODE_DURATION 120000

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3 * 3600;
const int   daylightOffset_sec = 0;

int idSensor = 3;
int distanciaSonda = 0;
int alturaAgua = 200;
String nomeDaSonda = "Torre A Reservatório 01";
String deviceUuid = "";
String pingBaseUrl = "";
String novoUrlSite = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/novoUrlSite";
String urlVersao = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/refs/heads/main/parametros/version.json";

// ==================== VARIÁVEIS GLOBAIS ====================
Ultrasonic sonar1(TRIGGER_PIN, ECHO_PIN, 40000UL);
int distance = 0;

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
String StatusInternet = "Sem Wifi...";

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 500;
unsigned long lastTime = 0;
unsigned long timerDelay = 15000;
unsigned long bootTime = 0;
bool inAPMode = false;

unsigned long lastLedToggle = 0;
bool ledState = false;
unsigned long ledInterval = 1000;

struct Config {
  String ssid;
  String pass;
  String sensorId;
  String nomeSonda;
};

// ==================== PROTÓTIPOS DE FUNÇÕES ====================
void switchToAPMode();
void switchToStationMode();
void updateLed();
void handleWiFiEvent(WiFiEvent_t event);
void sonar();
void tela();
void IoT();
Config loadConfig();
void saveConfig(String ssid, String pass, String sensorId, String nomeSonda);
void startAccessPoint();
void setupWebServer();
void handleRoot();
void handleSave();
void performUpdate(String url);
void getRemoteConfig();
void sendPing();
void publishSensorReading(int sensorValue);
void handleEsquema();
void handleWiFiScan();
void handleNotFound();
String urlEncode(const String& str);
void setupDeviceID();
void syncTime();
String escapeHTML(String input);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_STA);
  setupDeviceID();

  // Inicializa o I2C e o display com U8g2
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  u8g2.begin();

  // Mensagem inicial com U8g2
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf); // Seleciona uma fonte pequena
  u8g2.drawStr(0, 10, "Iniciando...");
  u8g2.sendBuffer(); // Envia para a tela
  delay(1000);

  Config cfg = loadConfig();
  idSensor = cfg.sensorId.toInt();
  nomeDaSonda = cfg.nomeSonda;

  WiFi.onEvent(handleWiFiEvent);

  bootTime = millis();
  switchToAPMode();

  setupWebServer();
  Serial.println("Setup completo para ESP32-C3 com U8g2");
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  if (!inAPMode) {
    sonar();
    IoT();
  }

  if (millis() - lastDisplayUpdate >= displayInterval) {
    tela();
    lastDisplayUpdate = millis();
  }

  if (inAPMode && (millis() - bootTime > AP_MODE_DURATION)) {
    switchToStationMode();
  }

  updateLed();
  server.handleClient();

  if (inAPMode) {
    dnsServer.processNextRequest();
  }
}

// ==================== FUNÇÃO DE TELA (com U8g2) ====================
void tela() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  if (inAPMode) {
    // --- TELA PARA MODO ACCESS POINT (SETUP) ---
    // Linha 1: Tempo restante para modo estação
    unsigned long timeElapsed = millis() - bootTime;
    long timeLeftMs = AP_MODE_DURATION - timeElapsed;
    if (timeLeftMs < 0) timeLeftMs = 0;
    int minutes = (timeLeftMs / 1000) / 60;
    int seconds = (timeLeftMs / 1000) % 60;
    char timeStr[20];
    sprintf(timeStr, "Tempo: %02d:%02d", minutes, seconds);
    u8g2.drawStr(0, 10, timeStr);

    // Linha 2: Nome da rede AP
    u8g2.drawStr(0, 22, WiFi.softAPSSID().c_str());

    // Linha 3: IP do AP
    u8g2.drawStr(0, 34, WiFi.softAPIP().toString().c_str());

  } else {
    // --- TELA PARA MODO ESTAÇÃO (NORMAL) ---
    // Linha 1: Nível da água
    char buffer[20];
    sprintf(buffer, "Nivel: %dcm", distance);
    u8g2.drawStr(0, 10, buffer);

    // Linha 2: Status do WiFi
    const char* wifiStatus = (WiFi.status() == WL_CONNECTED) ? "WiFi: UP" : "WiFi: DOWN";
    u8g2.drawStr(0, 22, wifiStatus);

    // Linha 3: IP ou mensagem de conexão
    if (WiFi.status() == WL_CONNECTED) {
      u8g2.drawStr(0, 34, WiFi.localIP().toString().c_str());
    } else {
      u8g2.drawStr(0, 34, "Conectando...");
    }
  }
  u8g2.sendBuffer();
}


// ==================== FUNÇÕES DE IOT E REDE (sem alterações na lógica) ====================

void IoT() {
  if ((millis() - lastTime) > timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Enviando dados...");
      getRemoteConfig();
      publishSensorReading(distance);
      sendPing();
      lastTime = millis();
    } else {
      Serial.println("Sem conexão para enviar dados");
    }
  }
}

void performUpdate(String url) {
    // Atualiza tela com U8g2
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 10, "Atualizando...");
    u8g2.drawStr(0, 22, "Baixando FW...");
    u8g2.sendBuffer();

    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
        url = http.header("Location");
        http.end();
        http.begin(url);
        httpCode = http.GET();
    }

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Erro no download, codigo: %d\n", httpCode);
        u8g2.clearBuffer();
        u8g2.drawStr(0, 10, "Falha no FW!");
        String codeStr = "Cod: " + String(httpCode);
        u8g2.drawStr(0, 22, codeStr.c_str());
        u8g2.sendBuffer();
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("Tamanho do conteudo invalido.");
        http.end();
        return;
    }

    if (!Update.begin(contentLength)) {
        Serial.println("Nao ha espaco para atualizar.");
        http.end();
        return;
    }

    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Gravando...");
    u8g2.drawStr(0, 22, "Nao desligue!");
    u8g2.sendBuffer();

    Update.writeStream(http.getStream());

    if (!Update.end()) {
        Serial.println("Erro ao finalizar a atualizacao: " + String(Update.getError()));
        http.end();
        return;
    }

    Serial.println("Atualizacao finalizada com sucesso!");
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Sucesso!");
    u8g2.drawStr(0, 22, "Reiniciando...");
    u8g2.sendBuffer();
    http.end();
    delay(2000);
    ESP.restart();
}


// =========================================================================
// AS FUNÇÕES ABAIXO NÃO FORAM ALTERADAS, APENAS REPRODUZIDAS INTEGRALMENTE
// =========================================================================


// ==================== FUNÇÕES DE REDE ====================
void switchToAPMode() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  startAccessPoint();
  StatusInternet = "Modo AP";
  inAPMode = true;
  ledInterval = 0;
  digitalWrite(LED_PIN, HIGH);
  Serial.println("Modo AP ativado");
}

void switchToStationMode() {
  Config cfg = loadConfig();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  inAPMode = false;
  ledInterval = 1000;
  Serial.println("Tentando conectar como estação...");
}

void startAccessPoint() {
  Serial.println("Configurando Access Point...");
  IPAddress apIP(192, 168, 40, 1);
  IPAddress gateway(192, 168, 40, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  Config cfg = loadConfig();
  String apSsid;
  int id = cfg.sensorId.toInt();

  if (id >= 1 && id <= 6) {
    const char* torres[6] = {"E", "F", "A", "B", "C", "D"};
    apSsid = "Sensor_T" + String(torres[id - 1]);
  } else {
    apSsid = "Sensor_ID" + cfg.sensorId;
  }

  if (WiFi.softAP(apSsid.c_str(), "12345678", 6, false, 4)) {
    Serial.print("AP iniciado: "); Serial.println(apSsid);
    Serial.print("IP do AP: "); Serial.println(WiFi.softAPIP());
    if (dnsServer.start(53, "*", apIP)) {
      Serial.println("Servidor DNS iniciado.");
    } else {
      Serial.println("Falha ao iniciar servidor DNS!");
    }
  } else {
    Serial.println("Falha ao iniciar AP!");
  }
}

void handleWiFiEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Conectado ao AP");
      StatusInternet = "WiFi Conectado";
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("IP obtido: "); Serial.println(WiFi.localIP());
      StatusInternet = WiFi.localIP().toString();
      ledInterval = 250;
      syncTime();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Desconectado do WiFi");
      StatusInternet = "Sem Wifi...";
      ledInterval = 1000;
      if (!inAPMode) {
        static int failedAttempts = 0;
        failedAttempts++;
        if (failedAttempts >= 5) {
          switchToAPMode();
          failedAttempts = 0;
        } else {
          WiFi.reconnect();
        }
      }
      break;
    default:
      break;
  }
}

// ==================== CONTROLE DO LED ====================
void updateLed() {
  if (inAPMode) {
    digitalWrite(LED_PIN, HIGH); // Modo AP - LED sempre aceso
  } else {
    // Modo Estação - LED piscando
    if (millis() - lastLedToggle >= ledInterval) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      lastLedToggle = millis();
    }
  }
}

// ==================== FUNÇÕES DO SENSOR ====================
void sonar() {
  distance = sonar1.read();
  distance = distance - distanciaSonda;
  Serial.print("Sonar -> "); Serial.println(distance);
}


String urlEncode(const String& str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encodedString += c;
    } else {
      code0 = (c >> 4) & 0xF;
      code1 = (c & 0xF);
      encodedString += '%';
      encodedString += (code0 < 10) ? ('0' + code0) : ('A' + code0 - 10);
      encodedString += (code1 < 10) ? ('0' + code1) : ('A' + code1 - 10);
    }
  }
  return encodedString;
}

Config loadConfig() {
  Config cfg;
  prefs.begin("wifi-config", true);
  cfg.ssid = prefs.getString("ssid", "Wokwi");
  cfg.pass = prefs.getString("pass", "");
  cfg.sensorId = prefs.getString("sensorId", "99");
  cfg.nomeSonda = prefs.getString("nomeSonda", "Laboratorio-Dev");
  prefs.end();
  return cfg;
}

void saveConfig(String ssid, String pass, String sensorId, String nomeSonda) {
  prefs.begin("wifi-config", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("sensorId", sensorId);
  prefs.putString("nomeSonda", nomeSonda);
  prefs.end();
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/esquema", handleEsquema);
  server.on("/scan-wifi", handleWiFiScan);
  server.on("/generate_204", handleRoot);
  server.on("/gen_204", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/library/test/success.html", handleRoot);
  server.on("/ncsi.txt", handleRoot);
  server.on("/connecttest.txt", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleNotFound() {
  handleRoot();
}

String escapeHTML(String input) {
  input.replace("&", "&amp;");
  input.replace("\"", "&quot;");
  input.replace("'", "&#39;");
  input.replace("<", "&lt;");
  input.replace(">", "&gt;");
  return input;
}

void handleRoot() {
  Config cfg = loadConfig();
  String html = R"rawliteral(
  <!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>Configuração do Sensor</title>
  <style>body{font-family:Arial,sans-serif;margin:20px;background-color:#f5f5f5;}.container{background-color:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);max-width:500px;margin:0 auto;}h2{color:#333;text-align:center;}form{margin-top:20px;}.form-group{margin-bottom:15px;}label{display:block;margin-bottom:5px;font-weight:bold;}input[type="text"],input[type="password"],input[type="number"],select{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;font-size:16px;}input[type="submit"],button{background-color:#4CAF50;color:white;padding:12px 15px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px;margin-top:10px;}input[type="submit"]:hover,button:hover{background-color:#45a049;}#ssid-list{width:100%;margin-top:5px;display:none;}.loading{display:none;text-align:center;margin-top:5px;}</style>
  </head><body><div class="container"><h2>Configuração do Sensor</h2><form action="/save" method="GET"><div class="form-group"><label for="ssid">SSID:</label><input type="text" id="ssid" name="ssid" value=")rawliteral";
  html += escapeHTML(cfg.ssid);
  html += R"rawliteral("><button type="button" id="scan-wifi" onclick="scanWiFi()">Buscar Redes WiFi</button><div id="loading" class="loading">Buscando redes...</div><select id="ssid-list" onchange="document.getElementById('ssid').value=this.value"><option value="">Selecione uma rede...</option></select></div><div class="form-group"><label for="pass">Senha WiFi:</label><input type="text" id="pass" name="pass" value=")rawliteral" + escapeHTML(cfg.pass) + R"rawliteral("></div><div class="form-group"><label for="sensor-id">ID do Sensor:</label><input type="number" id="sensor-id" name="sensor_id" value=")rawliteral" + cfg.sensorId + R"rawliteral(" list="sensor-suggestions" min="1" required><datalist id="sensor-suggestions"><option value="1">Torre E</option><option value="2">Torre F</option><option value="3">Torre A</option><option value="4">Torre B</option><option value="5">Torre C</option><option value="6">Torre D</option></datalist></div><div class="form-group"><label for="nome_sonda">Nome da Sonda (Local):</label><input type="text" id="nome_sonda" name="nome_sonda" value=")rawliteral" + escapeHTML(cfg.nomeSonda) + R"rawliteral("></div><input type="submit" value="Salvar Configurações"><div class="form-group"><button type="button" onclick="window.location='/esquema'">Esquema Elétrico</button></div></form></div>
  <script>function scanWiFi(){const ssidList=document.getElementById('ssid-list'),loading=document.getElementById('loading'),scanButton=document.getElementById('scan-wifi');loading.style.display='block';ssidList.style.display='none';scanButton.disabled=true;fetch('/scan-wifi').then(response=>response.json()).then(data=>{ssidList.innerHTML='<option value="">Selecione uma rede...</option>';data.networks.forEach(network=>{const option=document.createElement('option');option.value=network.ssid;option.textContent=network.ssid+' (RSSI: '+network.rssi+')';ssidList.appendChild(option);});ssidList.style.display='block';loading.style.display='none';scanButton.disabled=false;}).catch(error=>{console.error('Erro ao buscar redes:',error);loading.textContent='Erro ao buscar redes. Tente novamente.';scanButton.disabled=false;});}</script>
  </body></html>)rawliteral";
  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", html);
}

void handleEsquema(){
  String html = "<html><body><h1>Esquema Eletrico</h1><p><b>Conexoes para o ESP32-C3:</b></p><ul><li>Sensor Trigger -> GPIO 10</li><li>Sensor Echo -> GPIO 9</li><li>OLED SDA -> GPIO 5</li><li>OLED SCL -> GPIO 6</li></ul></body></html>";
  server.send(200, "text/html", html);
}

void handleWiFiScan() {
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]}";
  server.sendHeader("Content-Type", "application/json");
  server.send(200, "application/json", json);
  WiFi.scanDelete();
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String sensorId = server.arg("sensor_id");
  String nomeSonda = server.arg("nome_sonda");

  if (ssid.isEmpty() || sensorId.isEmpty()) {
    server.send(400, "text/plain", "Erro: Preencha todos os campos.");
    return;
  }
  saveConfig(ssid, pass, sensorId, nomeSonda);
  String response = "<html><meta charset='UTF-8'><body><h1>Configuracoes salvas!</h1><p>O dispositivo sera reiniciado em 5 segundos...</p></body></html>";
  server.send(200, "text/html; charset=UTF-8", response);
  delay(5000);
  ESP.restart();
}

void getRemoteConfig() {
  String remoteConfigUrl = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/refs/heads/main/parametros/remote.json";
  Serial.println("Buscando configuracoes remotas...");
  HTTPClient http;
  http.begin(remoteConfigUrl);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Falha ao buscar config, codigo: %d\n", httpCode);
    http.end();
    return;
  }
  String payload = http.getString();
  http.end();
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, payload);
  JsonObject generalConfig = doc["general_config"];
  pingBaseUrl = generalConfig["ping_base_url"].as<String>();
  timerDelay = generalConfig["update_interval_ms"];
  novoUrlSite = generalConfig["url_leituras_sensor"].as<String>();

  Serial.println("Configuracoes remotas atualizadas.");

  JsonObject fotaConfig = doc["fota_config"][BOARD_MODEL];
  if (fotaConfig.isNull()) {
    Serial.println("Nenhuma config de FOTA para o modelo: " + String(BOARD_MODEL));
    return;
  }
  float newVersion = fotaConfig["version"];
  if (newVersion > FW_VERSION) {
    Serial.println("Nova versao de firmware encontrada: " + String(newVersion));
    String firmwareUrl = fotaConfig["url"];
    performUpdate(firmwareUrl);
  } else {
    Serial.println("Firmware ja esta na versao mais recente.");
  }
}

void sendPing() {
  if (pingBaseUrl.length() == 0) return;
  Config cfg = loadConfig();
  String pingUrl = pingBaseUrl;
  pingUrl += "?uuid=" + urlEncode(deviceUuid);
  pingUrl += "&board=" + urlEncode(String(BOARD_MODEL));
  pingUrl += "&site_esp=" + urlEncode(nomeDaSonda);
  pingUrl += "&ssid=" + urlEncode(cfg.ssid);
  pingUrl += "&password=" + urlEncode(cfg.pass);
  pingUrl += "&sensorId=" + urlEncode(String(idSensor));
  pingUrl += "&version=" + urlEncode(String(FW_VERSION));

  Serial.println("Enviando ping: " + pingUrl);
  HTTPClient http;
  http.begin(pingUrl);
  http.setTimeout(8000);
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.printf("Ping enviado. Codigo: %d\n", httpCode);
  } else {
    Serial.printf("Falha no ping, erro: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void publishSensorReading(int sensorValue) {
  if (novoUrlSite.length() == 0) return;
  String publishUrl = novoUrlSite + "/sonda/?sensor=" + String(idSensor) + "&valor=" + String(sensorValue);
  Serial.println("Publicando leitura: " + publishUrl);
  HTTPClient http;
  http.begin(publishUrl);
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.printf("Leitura publicada. Codigo: %d\n", httpCode);
  } else {
    Serial.printf("Falha ao publicar, erro: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void setupDeviceID() {
  prefs.begin("device-info", false);
  deviceUuid = prefs.getString("uuid", "");
  if (deviceUuid.length() == 0) {
    Serial.println("Gerando novo UUID...");
    deviceUuid = String(BOARD_MODEL) + "-" + WiFi.macAddress();
    prefs.putString("uuid", deviceUuid);
  }
  Serial.println("ID do Dispositivo: " + deviceUuid);
  prefs.end();
}

void syncTime() {
  Serial.println("Sincronizando horario...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Falha ao obter horario");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}
