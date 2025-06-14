/* 
   ESTE SKETCH É BASEADO NO EXEMPLO DA LIBRARY esp8266 and esp32 OLED driver for SSD1306
   Bibliografia: 
   - https://www.youtube.com/watch?v=dD2BqAsN96c
   - https://www.curtocircuito.com.br/blog/Categoria%20IoT/desenvolvimento-de-dashboard-mqtt-com-adafruitio
   PINOUT da placa utilizada: https://raw.githubusercontent.com/AchimPieters/esp32-homekit-camera/master/Images/ESP32-30PIN-DEVBOARD.png

   instalar as bibliotecas:
    ultrasonic de Simões
   https://github.com/Pranjal-Prabhat/ultrasonic-arduino
   ssd1136 de ThingPulse
   https://github.com/ThingPulse/esp8266-oled-ssd1306


*/

// ==================== BIBLIOTECAS ====================
#include <WiFi.h>
#include "esp_wifi.h"
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include "SSD1306Wire.h"
#include <Ultrasonic.h>
#include <DNSServer.h>

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
int idSensor = 3; 

// A que distancia a sonda está do nivel máximo de água?
int distanciaSonda = 0; 

// Qual a altura maxima da coluna de água? (Não considerar a distancia da Sonda)
int alturaAgua = 240; 

// Qual Nome da Sonda nas mensagens do Telegram?
String nomeDaSonda = "Torre A Reservatório 01";

// Configurações WiFi padrão
const char* ssid = "TAIFIBRA-BLOCO A"; 
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
#define TRIGGER_PIN  13
#define ECHO_PIN1    12
Ultrasonic sonar1(TRIGGER_PIN, ECHO_PIN1, 40000UL);
int distancex;          // Distancia medida pelo sensor em CM
int distance;           // Distancia medida pelo sensor em CM
int minimo = 0;         // Menor distancia já medida
int maximo = 0;         // Maior distancia já medida
int progresso = 0;      // Calculo da % da barra de progresso

// Configurações de rede
unsigned long lastTime = 0;
unsigned long lastTimeAlert = 0;
unsigned long timerDelay = 15000; // 60 segundos
unsigned long timerAlerta = 600000; // 10 minutos
unsigned long nivelAlerta = 80; // 80%
String StatusInternet = "Sem Wifi...";

// Configurações do servidor web
WebServer server(80);
DNSServer dnsServer;
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
  delay(1000);

  // Inicializa o display primeiro para feedback visual
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "Iniciando...");
  display.display();

  // Carrega configurações salvas
  Config cfg = loadConfig();
  idSensor = cfg.sensorId.toInt(); // Atualiza o ID global

  // Configura eventos WiFi
  WiFi.onEvent(WiFiEvent);

  // Tentativa de conexão WiFi
  int tentativas = 0;
  bool conectado = false;
  
  while(tentativas < 5 && !conectado) {
    display.clear();
    display.drawString(0, 0, "Conectando WiFi");
    display.drawString(0, 18, "Tentativa " + String(tentativas+1));
    display.display();
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    
    unsigned long startTime = millis();
    while (millis() - startTime < 15000 && WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    
    if(WiFi.status() == WL_CONNECTED) {
      conectado = true;
      StatusInternet = "Conectado: " + WiFi.localIP().toString();
    } else {
      tentativas++;
    }
  }

  if(!conectado) {
    // Modo AP após falha
    WiFi.mode(WIFI_AP);
    startAccessPoint();
    StatusInternet = "Modo AP: " + WiFi.softAPIP().toString();
  }

  // Configura servidor web
  setupWebServer();
  Serial.println("Setup completo");
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  static unsigned long lastModeCheck = 0;
  static unsigned long lastStationActivity = 0;
  static bool inAPMode = (WiFi.getMode() & WIFI_AP);

  // Atualizações regulares
  if (millis() - lastDisplayUpdate >= displayInterval) {
    sonar();
    tela();
    IoT();
    lastDisplayUpdate = millis();
  }
  
  // Verificação periódica do modo
  if (millis() - lastModeCheck >= 60000) { // A cada 1 minuto
    lastModeCheck = millis();
    
    if(inAPMode) {
      // Se no modo AP e ninguém conectado por 2 minutos, tenta STA novamente
      wifi_sta_list_t stationList;
      if(esp_wifi_ap_get_sta_list(&stationList) == ESP_OK) {
        if(stationList.num > 0) {
          lastStationActivity = millis();
        } else if(millis() - lastStationActivity > 120000) { // 2 minutos
          switchToStationMode();
          inAPMode = false;
        }
      }
    } else {
      // Se no modo STA e desconectado, verifica
      if(WiFi.status() != WL_CONNECTED) {
        static int failedAttempts = 0;
        failedAttempts++;
        
        if(failedAttempts >= 5) {
          switchToAPMode();
          inAPMode = true;
          failedAttempts = 0;
        }
      }
    }
  }
  
  server.handleClient();
}

void switchToAPMode() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  startAccessPoint();
  StatusInternet = "Modo AP: " + WiFi.softAPIP().toString();
  Serial.println("Alternado para modo AP");
}

void switchToStationMode() {
  Config cfg = loadConfig();
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  
  unsigned long startTime = millis();
  while(millis() - startTime < 10000 && WiFi.status() != WL_CONNECTED) {
    delay(500);
    sonar();
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    StatusInternet = "Conectado: " + WiFi.localIP().toString();
    Serial.println("Conectado como estação");
  } else {
    switchToAPMode();
  }
}

// ==================== FUNÇÕES ====================
void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d - ", event);

  switch(event) {
    case ARDUINO_EVENT_WIFI_READY: 
      Serial.println("WiFi interface ready");
      break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      Serial.println("Completed scan for access points");
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("WiFi client started");
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      Serial.println("WiFi clients stopped");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Connected to access point");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Disconnected from WiFi access point");
      break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
      Serial.println("Authentication mode of access point has changed");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("Obtained IP address: ");
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      Serial.println("Lost IP address and IP address is reset to 0");
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("WiFi access point started");
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("WiFi access point stopped");
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.println("Client connected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("Client disconnected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      Serial.println("Assigned IP address to client");
      break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
      Serial.println("Received probe request");
      break;
    default: 
      Serial.println("Unknown event");
      break;
  }
}


void printNetworkStatus() {
  Serial.println("\n=== Status da Rede ===");
  Serial.printf("Modo WiFi: %s\n", WiFi.getMode() == WIFI_AP_STA ? "AP+STA" : "AP");
  
  wifi_sta_list_t stationList;
  if (esp_wifi_ap_get_sta_list(&stationList) == ESP_OK) {
    if (stationList.num > 0) {
      Serial.println("Dispositivos conectados no AP:");
      for (int i = 0; i < stationList.num; i++) {
        Serial.printf("  %d - MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                     i+1,
                     stationList.sta[i].mac[0], stationList.sta[i].mac[1],
                     stationList.sta[i].mac[2], stationList.sta[i].mac[3],
                     stationList.sta[i].mac[4], stationList.sta[i].mac[5]);
      }
    } else {
      Serial.println("Nenhum dispositivo conectado no AP");
    }
  } else {
    Serial.println("Erro ao obter lista de estações");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado à rede WiFi:");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("IP Local: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Força do sinal: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\nNão conectado a rede WiFi");
  }
  Serial.println("=====================\n");
}

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
  Serial.println(url);
  Serial.println(wget(url));
}

void sonar() {
  distance = sonar1.read();
  distance = distance - distanciaSonda;
  Serial.print("Sonar -> ");
  Serial.println(distance);
}

void tela() {
  display.clear();
  
  // 1ª Linha: Modo e Status (fonte 10)
  display.setFont(ArialMT_Plain_16);
  String header = String((WiFi.getMode() & WIFI_AP) ? "A" : "E") + ":";
  header += (WiFi.status() == WL_CONNECTED) ? 
            ((WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) : 
            "Sem Conexão";
  display.drawString(0, 0, header);

  // 2ª Linha: Nome da Rede/AP (fonte 10)
  String rede = (WiFi.getMode() & WIFI_AP) ? "AP: Torre " + getTorreLetter() 
                                         : "R: " + WiFi.SSID().substring(0, 16);
  display.drawString(0, 18, rede);

  // Valor principal centralizado (fonte 24)
  display.setFont(ArialMT_Plain_24);
  String leitura = String(distance) + " cm";
  int textWidth = display.getStringWidth(leitura);
  display.drawString((130 - textWidth) / 2, 30, leitura);

  display.display();
}

String getTorreLetter() {
  const char* torres[6] = {"E", "F", "A", "B", "C", "D"};
  Config cfg = loadConfig();
  return String(torres[cfg.sensorId.toInt()-1]);
}

void IoT() {
  internet();
  if ((millis() - lastTime) > timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("HORA DE TAREFAS DA WEB");
      eti(distance);
      getParametrosRemotos();
      lastTime = millis();
    }else{
      Serial.println("HORA DE TAREFAS DA WEB MAS ESTOU SEM INTERNET");
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
  // Desconecta primeiro se já estiver em modo AP
  if (WiFi.getMode() & WIFI_AP) {
    WiFi.softAPdisconnect(true);
    delay(100);
  }

  // Configuração do IP
  IPAddress apIP(192, 168, 40, 1);
  IPAddress gateway(192, 168, 40, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  // Carrega configurações para pegar o ID do sensor
  Config cfg = loadConfig();

  // Declara a variável no escopo da função
  String apSsid;

  // Verifica se o ID está no intervalo conhecido (1-6)
  int id = cfg.sensorId.toInt();
  
  // Define o SSID baseado no ID do sensor
  if (id >= 1 && id <= 6) {
    const char* torres[6] = {"E", "F", "A", "B", "C", "D"};
    apSsid = "Sensor_Torre" + String(torres[id - 1]);
  } else {
    apSsid = "Sensor_ID" + cfg.sensorId;  // Usa o valor salvo diretamente
  }

  const char* apPassword = "12345678"; // Senha do AP
  
  // Inicia o AP
  WiFi.softAP(apSsid.c_str(), apPassword, 6, 0, 4);
  
  // Inicia o DNS server para captive portal
  dnsServer.start(53, "*", apIP);  // Redireciona todos os domínios para o IP do AP

  Serial.println("\nAccess Point iniciado com sucesso");
  Serial.print("SSID: ");
  Serial.println(apSsid);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

Config loadConfig() {
  Config cfg;
  prefs.begin("wifi-config", true); // true = read-only
  cfg.ssid = prefs.getString("ssid", "TAIFIBRA-BLOCO A");
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
  Config cfg = loadConfig();
  
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Configuração do Sensor</title>
      <style>
        body { 
          font-family: Arial, sans-serif; 
          margin: 20px;
          background-color: #f5f5f5;
        }
        .container {
          background-color: white;
          padding: 20px;
          border-radius: 5px;
          box-shadow: 0 2px 5px rgba(0,0,0,0.1);
          max-width: 500px;
          margin: 0 auto;
        }
        h2 {
          color: #333;
          text-align: center;
        }
        form {
          margin-top: 20px;
        }
        .form-group {
          margin-bottom: 15px;
        }
        label {
          display: block;
          margin-bottom: 5px;
          font-weight: bold;
        }
        input[type="text"], 
        input[type="password"],
        input[type="number"],
        select {
          width: 100%;
          padding: 10px;
          border: 1px solid #ddd;
          border-radius: 4px;
          box-sizing: border-box;
          font-size: 16px;
        }
        input[type="submit"], button {
          background-color: #4CAF50;
          color: white;
          padding: 12px 15px;
          border: none;
          border-radius: 4px;
          cursor: pointer;
          width: 100%;
          font-size: 16px;
          margin-top: 10px;
        }
        input[type="submit"]:hover, button:hover {
          background-color: #45a049;
        }
        #ssid-list {
          display: none;
          margin-top: 5px;
        }
        #manual-sensor-id {
          display: none;
          margin-top: 5px;
        }
      </style>
    </head>
    <body>
    <div class="container">
      <h2>Configuração do Sensor</h2>
      <form action="/save" method="GET">
        <div class="form-group">
          <label for="ssid">SSID:</label>
          <input type="text" id="ssid" name="ssid" value=")rawliteral";
  html += escapeHTML(cfg.ssid);
  html += R"rawliteral(">
          <button type="button" id="scan-wifi" onclick="scanWiFi()">Buscar Redes WiFi</button>
          <select id="ssid-list" onchange="document.getElementById('ssid').value = this.value">
            <option value="">Selecione uma rede...</option>
          </select>
        </div>
        
        <div class="form-group">
          <label for="pass">Senha WiFi:</label>
          <input type="text" id="pass" name="pass" value=")rawliteral" + escapeHTML(cfg.pass) + R"rawliteral(">
        </div>
        
        <div class="form-group">
          <label for="sensor-id">ID do Sensor:</label>
          <input type="number" id="sensor-id" name="sensor_id" value=")rawliteral" + cfg.sensorId + R"rawliteral(" 
                list="sensor-suggestions" min="1" required>
          <datalist id="sensor-suggestions">
            <option value="1">Torre E</option>
            <option value="2">Torre F</option>
            <option value="3">Torre A</option>
            <option value="4">Torre B</option>
            <option value="5">Torre C</option>
            <option value="6">Torre D</option>
          </datalist>
        </div>
        
        <input type="submit" value="Salvar Configurações">

        <div class="form-group">
          <button type="button" onclick="
              window.location = '/esquema';
            ">Esquema Elétrico</button>
        </div>

      </form>


    </div>
    
    
    </body>
    </html>
  )rawliteral";
  
  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", html);
}

void handleEsquema(){
  String html =R"rawliteral(
                                  <!DOCTYPE html>
                              <html lang="pt-BR">
                              <head>
                                  <meta charset="UTF-8">
                                  <meta name="viewport" content="width=device-width, initial-scale=1.0">
                                  <title>ESP32 e Componentes</title>
                                  <style>
                                      body {
                                          display: flex;
                                          justify-content: center;
                                          align-items: center;
                                          min-height: 100vh;
                                          background-color: #f0f0f0;
                                          margin: 0;
                                          font-family: Arial, sans-serif;
                                      }

                                      .board-container {
                                          display: flex;
                                          gap: 50px;
                                      }

                                      .components-container {
                                          display: flex;
                                          flex-direction: column;
                                          gap: 30px; /* Espaço entre sensor e display */
                                      }

                                      .esp32-board, .sensor-board, .display-board {
                                          background-color: #333;
                                          border-radius: 8px;
                                          position: relative;
                                          box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
                                          display: flex;
                                          justify-content: space-between;
                                          padding: 0 10px;
                                          box-sizing: border-box;
                                      }

                                      .esp32-board {
                                          width: 120px;
                                          height: 430px;
                                      }

                                      .sensor-board, .display-board {
                                          width: 150px;
                                          height: 40px;
                                          align-items: flex-end;
                                          padding: 5px;
                                      }

                                      .esp32-label, .sensor-label, .display-label {
                                          position: absolute;
                                          top: 50%;
                                          left: 50%;
                                          transform: translate(-50%, -50%);
                                          color: #eee;
                                          font-size: 1.2em;
                                          font-weight: bold;
                                          text-shadow: 1px 1px 2px rgba(0,0,0,0.5);
                                          pointer-events: none;
                                          white-space: nowrap;
                                      }

                                      .micro-usb {
                                          width: 40px;
                                          height: 15px;
                                          background-color: #888;
                                          border-radius: 3px;
                                          position: absolute;
                                          top: -10px;
                                          left: 50%;
                                          transform: translateX(-50%);
                                          z-index: 10;
                                          border: 1px solid #666;
                                      }

                                      .pin-column {
                                          display: flex;
                                          flex-direction: column;
                                          justify-content: space-around;
                                          height: 100%;
                                          position: absolute;
                                      }

                                      .pin-column.left {
                                          left: -10px;
                                      }

                                      .pin-column.right {
                                          right: -10px;
                                      }

                                      .pin-row-bottom {
                                          display: flex;
                                          justify-content: space-around;
                                          width: 100%;
                                          position: absolute;
                                          bottom: -10px;
                                          left: 0;
                                          padding: 0 5px;
                                          box-sizing: border-box;
                                      }

                                      .pin {
                                          width: 20px;
                                          height: 20px;
                                          background-color: #bbb;
                                          border-radius: 50%;
                                          border: 1px solid #888;
                                          box-sizing: border-box;
                                          display: flex;
                                          justify-content: center;
                                          align-items: center;
                                          font-size: 9px;
                                          color: #444;
                                          font-weight: bold;
                                          margin: 3px 0;
                                          flex-shrink: 0;
                                      }

                                      .pin-row-bottom .pin {
                                          margin: 0 2px;
                                      }

                                      .pin.sda {
                                          background-color: teal;
                                          border-color: #2E8B57;
                                          color: white;
                                      }
                                      .pin.scl {
                                          background-color: #4CAF50;
                                          border-color: #2E8B57;
                                          color: white;
                                      }
                                      
                                      .pin.vcc5 {
                                          background-color: red;
                                          border-color: #2E8B57;
                                          color: white;
                                      }        
                                      .pin.vcc3 {
                                          background-color: orange;
                                          border-color: #2E8B57;
                                          color: white;
                                      }
                                      .pin.gnd {
                                          background-color: black;
                                          border-color: #2E8B57;
                                          color: white;
                                      }
                                      .pin.trig {
                                          background-color: #007bff;
                                          border-color: #0056b3;
                                          color: white;
                                      }
                                      .pin.echo {
                                          background-color: #ffc107;
                                          border-color: #d39e00;
                                          color: black;
                                      }
                                  </style>
                              </head>
                              <body>
                                  <div class="board-container">
                                      <div class="esp32-board">
                                          <div class="esp32-label">ESP32</div>
                                          <div class="micro-usb"></div>

                                          <div class="pin-column left">
                                              <div class="pin vcc3" title="3V3">3V3</div>
                                              <div class="pin gnd" title="GND">GND</div>
                                              <div class="pin" title="D15">D15</div>
                                              <div class="pin" title="D2">D2</div>
                                              <div class="pin" title="D4">D4</div>
                                              <div class="pin" title="RX2">RX2</div>
                                              <div class="pin" title="TX2">TX2</div>
                                              <div class="pin" title="D5">D5</div>
                                              <div class="pin" title="D18">D18</div>
                                              <div class="pin" title="D19">D19</div>
                                              <div class="pin sda" title="D21 (SDA)">D21</div>
                                              <div class="pin" title="RX0">RX0</div>
                                              <div class="pin" title="TX0">TX0</div>
                                              <div class="pin scl" title="D22 (SCL)">D22</div>
                                              <div class="pin" title="D23">D23</div>
                                          </div>

                                          <div class="pin-column right">
                                              <div class="pin vcc5" title="VIN">VIN</div>
                                              <div class="pin gnd" title="GND">GND</div>
                                              <div class="pin trig" title="D13">D13</div>
                                              <div class="pin echo" title="D12">D12</div>
                                              <div class="pin" title="D14">D14</div>
                                              <div class="pin" title="D27">D27</div>
                                              <div class="pin" title="D26">D26</div>
                                              <div class="pin" title="D25">D25</div>
                                              <div class="pin" title="D33">D33</div>
                                              <div class="pin" title="D32">D32</div>
                                              <div class="pin" title="D35">D35</div>
                                              <div class="pin" title="D33">D33</div>
                                              <div class="pin" title="VN">VN</div>
                                              <div class="pin" title="VP">VP</div>
                                              <div class="pin" title="EN">EN</div>
                                          </div>
                                      </div>

                                      <div class="components-container">
                                          <div class="sensor-board">
                                              <div class="sensor-label">Sensor</div>
                                              <div class="pin-row-bottom">
                                                  <div class="pin vcc5" title="VCC">VCC</div>
                                                  <div class="pin trig" title="TRIG">TRI</div>
                                                  <div class="pin echo" title="ECHO">ECH</div>
                                                  <div class="pin gnd" title="GND">GND</div>
                                              </div>
                                          </div>
                                          
                                          <div class="display-board">
                                              <div class="display-label">Display</div>
                                              <div class="pin-row-bottom">
                                                  <div class="pin gnd" title="GND">GND</div>
                                                  <div class="pin vcc3" title="VCC">VCC</div>
                                                  <div class="pin scl" title="SCL">SCL</div>
                                                  <div class="pin sda" title="SDA">SDA</div>
                                              </div>
                                          </div>
                                      </div>
                                  </div>
                              </body>
                              </html>
  )rawliteral";

  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", html);
}

void handleWiFiScan() {
  // Escaneia redes WiFi
  int n = WiFi.scanNetworks();
  
  // Prepara resposta JSON
  String json = "{\"networks\":[";
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
  }
  json += "]}";
  
  server.sendHeader("Content-Type", "application/json");
  server.send(200, "application/json", json);
  
  WiFi.scanDelete(); // Limpa a lista de redes
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/esquema", handleEsquema);
  server.on("/scan-wifi", handleWiFiScan); // Novo endpoint
  server.begin();
}

// Função auxiliar para escapar caracteres HTML
String escapeHTML(String input) {
  input.replace("&", "&amp;");
  input.replace("\"", "&quot;");
  input.replace("'", "&#39;");
  input.replace("<", "&lt;");
  input.replace(">", "&gt;");
  return input;
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String sensorId = server.arg("sensor_id");  // Nome do campo atualizado

  // Validação básica (UTF-8 corrigido)
  if (ssid.isEmpty() || pass.isEmpty() || sensorId.isEmpty()) {
    server.send(400, "text/html; charset=UTF-8", 
                "<html><meta charset='UTF-8'><body>"
                "<h1>Erro!</h1><p>Preencha todos os campos.</p>"
                "</body></html>");
    return;
  }

  saveConfig(ssid, pass, sensorId);
  server.send(200, "text/html; charset=UTF-8", 
              "<html><meta charset='UTF-8'><body>"
              "<h1>Configurações salvas!</h1>"
              "<p>Reiniciando...</p>"
              "</body></html>");
  delay(5000);
  ESP.restart();
}
