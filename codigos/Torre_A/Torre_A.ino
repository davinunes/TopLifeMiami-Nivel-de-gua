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


#include <WiFi.h>
#include "esp_wifi.h"
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include "SSD1306Wire.h"
#include <Ultrasonic.h>
#include <DNSServer.h>


// ==================== CONFIGURAÇÕES ====================
#define LED_PIN 2  // Pino do LED onboard (normalmente GPIO2 para ESP32)
#define AP_MODE_DURATION 120000  // 2 minutos em modo AP

// Configurações do sensor
int idSensor = 3; 
int distanciaSonda = 0; 
int alturaAgua = 240; 
String nomeDaSonda = "Torre A Reservatório 01";

// URLs para configuração remota
String IntervaloDePush = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/updateTime";
String nivelAlertaTelegram = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/nivelAlerta";
String novoUrlSite = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/novoUrlSite";
String urlSite = "h2o-miami.davinunes.eti.br";

// ==================== VARIÁVEIS GLOBAIS ====================
// Display
SSD1306Wire display(0x3c, 21, 22); // Endereço I2C 0x3c, SDA=21, SCL=22

// Sonar
#define TRIGGER_PIN  13
#define ECHO_PIN1    12
Ultrasonic sonar1(TRIGGER_PIN, ECHO_PIN1, 40000UL);
int distance = 0;  // Distância medida pelo sensor em CM

// Rede
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
String StatusInternet = "Sem Wifi...";

// Temporizadores
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 500; // Atualizar a tela a cada 500ms
unsigned long lastTime = 0;
unsigned long timerDelay = 15000; // 15 segundos
unsigned long bootTime = 0;
bool inAPMode = false;

// LED
unsigned long lastLedToggle = 0;
bool ledState = false;
unsigned long ledInterval = 1000; // Intervalo padrão para piscar (1 segundo)

// ==================== ESTRUTURAS ====================
struct Config {
  String ssid;
  String pass;
  String sensorId;
};

// ==================== PROTÓTIPOS DE FUNÇÕES ====================
void setupWiFi();
void switchToAPMode();
void switchToStationMode();
void updateLed();
void handleWiFiEvent(WiFiEvent_t event);
void sonar();
void tela();
void IoT();
void getParametrosRemotos();
Config loadConfig();
void saveConfig(String ssid, String pass, String sensorId);
void startAccessPoint();
void setupWebServer();
void handleRoot();
void handleSave();
String escapeHTML(String input);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Inicia com LED apagado
  
  // Inicializa display
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "Iniciando...");
  display.display();

  // Carrega configurações
  Config cfg = loadConfig();
  idSensor = cfg.sensorId.toInt();

  // Configura eventos WiFi
  WiFi.onEvent(handleWiFiEvent);

  // Inicia em modo AP
  bootTime = millis();
  switchToAPMode();
  
  // Configura servidor web
  setupWebServer();
  Serial.println("Setup completo");
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  // Atualizações regulares
  if (millis() - lastDisplayUpdate >= displayInterval) {
    sonar();
    tela();
    IoT();
    lastDisplayUpdate = millis();
  }
  
  // Verifica se deve sair do modo AP após 2 minutos
  if (inAPMode && (millis() - bootTime > AP_MODE_DURATION)) {
    switchToStationMode();
  }
  
  // Atualiza estado do LED
  updateLed();
  
  // Trata requisições do servidor web
  server.handleClient();
  
  // Se estiver em modo AP, trata também DNS
  if (inAPMode) {
    dnsServer.processNextRequest();
  }
}

// ==================== FUNÇÕES DE REDE ====================
void switchToAPMode() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  startAccessPoint();
  StatusInternet = "Modo AP: " + WiFi.softAPIP().toString();
  inAPMode = true;
  
  // LED aceso constantemente no modo AP
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
  
  // LED começa piscando devagar (modo desconectado)
  ledInterval = 1000; // 1 segundo
  
  Serial.println("Tentando conectar como estação...");
}

void startAccessPoint() {
  WiFi.softAPdisconnect(true);
  delay(100);

  // Configuração do IP
  IPAddress apIP(192, 168, 40, 1);
  IPAddress gateway(192, 168, 40, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  // Cria SSID baseado no ID do sensor
  Config cfg = loadConfig();
  String apSsid;
  int id = cfg.sensorId.toInt();
  
  if (id >= 1 && id <= 6) {
    const char* torres[6] = {"E", "F", "A", "B", "C", "D"};
    apSsid = "Sensor_Torre" + String(torres[id - 1]);
  } else {
    apSsid = "Sensor_ID" + cfg.sensorId;
  }

  WiFi.softAP(apSsid.c_str(), "12345678", 6, 0, 4);
  dnsServer.start(53, "*", apIP);
  
  Serial.print("AP iniciado: ");
  Serial.println(apSsid);
}

void handleWiFiEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Conectado ao AP");
      break;
      
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("IP obtido: ");
      Serial.println(WiFi.localIP());
      StatusInternet = "Conectado: " + WiFi.localIP().toString();
      ledInterval = 250; // Piscar rápido quando conectado
      break;
      
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Desconectado do WiFi");
      StatusInternet = "Sem Wifi...";
      ledInterval = 1000; // Piscar devagar quando desconectado
      
      // Se não estiver em modo AP, tenta reconectar
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
  if (ledInterval == 0) {
    // Modo AP - LED aceso constantemente
    digitalWrite(LED_PIN, HIGH);
    return;
  }
  
  if (millis() - lastLedToggle >= ledInterval) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    lastLedToggle = millis();
  }
}

// ==================== FUNÇÕES DO SENSOR ====================
void sonar() {
  distance = sonar1.read();
  distance = distance - distanciaSonda;
  Serial.print("Sonar -> ");
  Serial.println(distance);
}

void tela() {
  display.clear();
  
  // 1ª Linha: Modo e Status
  display.setFont(ArialMT_Plain_16);
  String header = String(inAPMode ? "A" : "E") + ":";
  header += (WiFi.status() == WL_CONNECTED) ? 
            (inAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) : 
            "Sem Conexão";
  display.drawString(0, 0, header);

  // 2ª Linha: Nome da Rede/AP
  String rede = inAPMode ? "AP: Torre " + String(idSensor) 
                       : "R: " + WiFi.SSID().substring(0, 16);
  display.drawString(0, 18, rede);

  // Valor principal centralizado
  display.setFont(ArialMT_Plain_24);
  String leitura = String(distance) + " cm";
  int textWidth = display.getStringWidth(leitura);
  display.drawString((130 - textWidth) / 2, 30, leitura);

  display.display();
}

void IoT() {
  if ((millis() - lastTime) > timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Enviando dados...");
      //eti(distance); // Descomente se necessário
      getParametrosRemotos();
      lastTime = millis();
    } else {
      Serial.println("Sem conexão para enviar dados");
    }
  }
}

void getParametrosRemotos() {
  HTTPClient http;
  
  http.begin(IntervaloDePush);
  if (http.GET() == HTTP_CODE_OK) {
    timerDelay = http.getString().toInt();
  }
  http.end();
  
  http.begin(nivelAlertaTelegram);
  if (http.GET() == HTTP_CODE_OK) {
    //nivelAlerta = http.getString().toInt(); // Descomente se necessário
  }
  http.end();
  
  http.begin(novoUrlSite);
  if (http.GET() == HTTP_CODE_OK) {
    urlSite = http.getString();
  }
  http.end();
}

// ==================== FUNÇÕES DE CONFIGURAÇÃO ====================
Config loadConfig() {
  Config cfg;
  prefs.begin("wifi-config", true);
  cfg.ssid = prefs.getString("ssid", "TAIFIBRA-BLOCO A");
  cfg.pass = prefs.getString("pass", "taifibratelecom");
  cfg.sensorId = prefs.getString("sensorId", "3");
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

// ==================== SERVIDOR WEB ====================
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/esquema", handleEsquema);
  server.on("/scan-wifi", handleWiFiScan); // Novo endpoint
  server.begin();
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
          width: 100%;
          margin-top: 5px;
          display: none;
        }
        #manual-sensor-id {
          display: none;
          margin-top: 5px;
        }
        .loading {
          display: none;
          text-align: center;
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
          <div id="loading" class="loading">Buscando redes...</div>
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
          <button type="button" onclick="window.location = '/esquema'">Esquema Elétrico</button>
        </div>
      </form>
    </div>

    <script>
      function scanWiFi() {
        const ssidList = document.getElementById('ssid-list');
        const loading = document.getElementById('loading');
        const scanButton = document.getElementById('scan-wifi');
        
        // Mostra loading e desabilita o botão
        loading.style.display = 'block';
        ssidList.style.display = 'none';
        scanButton.disabled = true;
        
        // Faz a requisição para o endpoint de scan
        fetch('/scan-wifi')
          .then(response => response.json())
          .then(data => {
            // Limpa a lista atual
            ssidList.innerHTML = '<option value="">Selecione uma rede...</option>';
            
            // Adiciona cada rede encontrada
            data.networks.forEach(network => {
              const option = document.createElement('option');
              option.value = network.ssid;
              option.textContent = network.ssid + ' (RSSI: ' + network.rssi + ')';
              ssidList.appendChild(option);
            });
            
            // Mostra a lista e esconde o loading
            ssidList.style.display = 'block';
            loading.style.display = 'none';
            scanButton.disabled = false;
          })
          .catch(error => {
            console.error('Erro ao buscar redes:', error);
            loading.textContent = 'Erro ao buscar redes. Tente novamente.';
            scanButton.disabled = false;
          });
      }
    </script>
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

String escapeHTML(String input) {
  input.replace("&", "&amp;");
  input.replace("\"", "&quot;");
  input.replace("'", "&#39;");
  input.replace("<", "&lt;");
  input.replace(">", "&gt;");
  return input;
}
