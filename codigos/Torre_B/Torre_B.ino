/* 
   ESTE SKETCH É BASEADO NO EXEMPLO DA LIBRARY esp8266 and esp32 OLED driver for SSD1306
   Bibliografia: 
   - https://www.youtube.com/watch?v=dD2BqAsN96c
   - https://www.curtocircuito.com.br/blog/Categoria%20IoT/desenvolvimento-de-dashboard-mqtt-com-adafruitio
   PINOUT da placa utilizada: https://raw.githubusercontent.com/AchimPieters/esp32-homekit-camera/master/Images/ESP32-30PIN-DEVBOARD.png
*/

// ==================== BIBLIOTECAS ====================
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include "SSD1306Wire.h"
#include <Ultrasonic.h>

// ==================== CONFIGURAÇÕES ====================
// Qual id do sensor no site davinunes.eti.br?
/*
 * 1 - Torre E
 * 2 - Torre F
 * 3 - Torre A
 * 4 - Torre B
 * 5 - Torre C
 * 6 - Torre D
 */
int idSensor = 4; 

// A que distancia a sonda está do nivel máximo de água?
int distanciaSonda = 20; 

// Qual a altura maxima da coluna de água? (Não considerar a distancia da Sonda)
int alturaAgua = 240; 

// Qual Nome da Sonda nas mensagens do Telegram?
String nomeDaSonda = "Torre B Reservatório 01";

// Configurações WiFi padrão
const char* ssid = "TAIFIBRA-BLOCO B"; 
const char* password = "taifibratelecom"; 

// URLs para configuração remota
String IntervaloDePush = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/updateTime";
String nivelAlertaTelegram = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/nivelAlerta";
String novoUrlSite = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/novoUrlSite";
String urlSite = "h2o-miami.davinunes.eti.br";

// ==================== VARIÁVEIS GLOBAIS ====================
// Configurações do display
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 500; // Atualizar a tela a cada 500ms
SSD1306Wire display(0x3c, 21, 22); // Endereço I2C 0x3c, SDA=21, SCL=22

// Configurações do sonar
#define TRIGGER_PIN  33
#define ECHO_PIN1    25
Ultrasonic sonar1(TRIGGER_PIN, ECHO_PIN1, 40000UL);
int distancex;          // Distancia medida pelo sensor em CM
int distance;           // Distancia medida pelo sensor em CM
int minimo = 0;         // Menor distancia já medida
int maximo = 0;         // Maior distancia já medida
int progresso = 0;      // Calculo da % da barra de progresso

// Configurações de rede
unsigned long lastTime = 0;
unsigned long lastTimeAlert = 0;
unsigned long timerDelay = 60000; // 60 segundos
unsigned long timerAlerta = 600000; // 10 minutos
unsigned long nivelAlerta = 80; // 80%
String StatusInternet = "Sem Wifi...";

// Configurações do servidor web
WebServer server(80);
Preferences prefs;

// ==================== ESTRUTURAS ====================
struct Config {
  String ssid;
  String pass;
  String sensorId;
};

// ==================== PROTÓTIPOS DE FUNÇÕES ====================
String wget(String url);
void eti(int num);
void sonar();
void tela();
void IoT();
void internet();
void getParametrosRemotos();
String getMacSuffix();
void startAccessPoint();
Config loadConfig();
void saveConfig(String ssid, String pass, String sensorId);
void handleRoot();
void handleSave();
void setupWebServer();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  
  // Carrega configurações salvas
  Config cfg = loadConfig();

  // Exibe configurações carregadas
  Serial.println("\n=== Configurações carregadas da EEPROM ===");
  Serial.printf("SSID: %s\n", cfg.ssid.c_str());
  Serial.printf("Senha: %s\n", cfg.pass.c_str());
  Serial.printf("ID Sensor: %s\n", cfg.sensorId.c_str());
  Serial.println("========================================\n");
  
  // Inicia WiFi em modo AP + STA
  WiFi.mode(WIFI_AP_STA);
  startAccessPoint();
  
  // Conecta à rede WiFi
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  
  // Configura servidor web
  setupWebServer();
  
  // Verifica conexão
  internet();

  // Inicializa o display
  display.init();
  display.flipScreenVertically();
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  

  if (millis() - lastDisplayUpdate >= displayInterval) {
    sonar();
    tela();
    IoT();
    lastDisplayUpdate = millis();
  }
  
  server.handleClient();
}

// ==================== FUNÇÕES ====================

String wget(String url) {
  HTTPClient http;
  http.setTimeout(3000); // define timeout de 3s
  http.begin(url);

  int httpResponseCode = http.GET();
  String payload;

  if (httpResponseCode > 0) {
    payload = http.getString();
  } else {
    Serial.print("Erro HTTP: ");
    Serial.println(httpResponseCode);
    payload = "erro";
  }

  http.end();  // importante!
  return payload;
}

void eti(int num) {
  String url = urlSite + "/sonda/?sensor=" + String(idSensor) + "&valor=" + String(num);
  Serial.println(wget(url));
}

void sonar() {
  distance = sonar1.read();
  distance = distance - distanciaSonda;
  Serial.print("Sonar -> ");
  Serial.println(distance);

  if (minimo == 0 || minimo > distance) {
    minimo = distance;
  }

  if (maximo == 0 || maximo < distance) {
    maximo = distance;
  }

  distance > alturaAgua ? progresso = alturaAgua : progresso = distance;
  progresso = 100 * progresso / alturaAgua;
  progresso = 100 - progresso;
}

void tela() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, StatusInternet);
  display.setFont(ArialMT_Plain_24);
  display.drawString(0, 14, String(distance));
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 14, String(minimo));
  display.drawString(64, 28, String(maximo));
  display.drawString(90, 19, String(progresso) + "%");
  display.drawProgressBar(0, 40, 127, 22, progresso);
  display.display();
}

void IoT() {
  internet();
  if ((millis() - lastTime) > timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("HORA DE TAREFAS DA WEB");
      String msg = nomeDaSonda + " -> Distancia do Sensor: " + String(distance) + "cm";
      eti(distance);
      getParametrosRemotos();
      lastTime = millis();
    }
  }
}

void internet() {
  if (WiFi.status() == WL_CONNECTED) {
    StatusInternet = "NaRede!" + WiFi.localIP().toString();
    Serial.println("Conectado com IP: ");
    Serial.println(WiFi.localIP());
    return;
  } else {
    StatusInternet = "Sem Wifi...";
    return;
  }
}

void getParametrosRemotos() {
  timerDelay = wget(IntervaloDePush).toInt();
  nivelAlerta = wget(nivelAlertaTelegram).toInt();
  urlSite = wget(novoUrlSite);
  Serial.println(timerDelay);
  Serial.println(nivelAlerta);
}

String getMacSuffix() {
  uint8_t mac[6];
  WiFi.macAddress(mac); // Obtém o endereço MAC
  
  char suffix[7]; // 6 caracteres hex + null terminator
  sprintf(suffix, "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(suffix);
}

void startAccessPoint() {
// Configuração do IP
  IPAddress apIP(192, 168, 40, 1);
  IPAddress gateway(192, 168, 40, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  // Aplica a configuração de IP
  WiFi.softAPConfig(apIP, gateway, subnet);
  
  // Cria SSID único baseado no MAC
  String apSsid = "Sensor_" + getMacSuffix();
  const char* apPassword = "12345678"; // Senha do AP
  
  // Inicia o Access Point
  if(WiFi.softAP(apSsid.c_str(), apPassword)) {
    Serial.println("Access Point iniciado com sucesso");
    Serial.print("SSID: ");
    Serial.println(apSsid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("MAC: ");
    Serial.println(WiFi.softAPmacAddress());
  } else {
    Serial.println("Falha ao iniciar Access Point");
  }
  Serial.println("Acesse http://192.168.40.1 para configurar.");
}

Config loadConfig() {
  Config cfg;
  prefs.begin("wifi-config", true); // true = read-only
  cfg.ssid = prefs.getString("ssid", "TAIFIBRA-BLOCO B");
  cfg.pass = prefs.getString("pass", "taifibratelecom");
  cfg.sensorId = prefs.getString("sensorId", "4");
  prefs.end();
  return cfg;
}

void saveConfig(String ssid, String pass, String sensorId) {
  prefs.begin("wifi-config", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("sensorId", sensorId);
  prefs.end();
}

void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
    <html><body>
    <form action="/save" method="GET">
      SSID: <input name="ssid"><br>
      Senha: <input name="pass"><br>
      ID Sensor: <input name="id"><br>
      <input type="submit" value="Salvar">
    </form>
    </body></html>
  )rawliteral");
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String sensorId = server.arg("id");
  saveConfig(ssid, pass, sensorId);
  server.send(200, "text/plain", "Salvo! Reinicie o dispositivo.");
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();
  Serial.println("Servidor Web iniciado");
}
