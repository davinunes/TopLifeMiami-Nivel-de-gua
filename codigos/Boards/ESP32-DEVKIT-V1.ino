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
#include <WiFiClientSecure.h>

#include <Update.h>
#include <ArduinoJson.h>
#include <time.h> // Necessario para a gestao do tempo

#define BOARD_MODEL "ESP32-DEVKIT-V1"
#define FW_VERSION 2.0

// ==================== CONFIGURAÇÕES ====================
#define LED_PIN 2  // Pino do LED onboard (normalmente GPIO2 para ESP32)
#define AP_MODE_DURATION 120000  // 2 minutos em modo AP

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3 * 3600; // Offset para o fuso horario do Brasil (GMT-3)
const int   daylightOffset_sec = 0;    // Offset para horario de verao (geralmente 0)


// Configurações do sensor
int idSensor = 3;
int distanciaSonda = 0;
int alturaAgua = 200;
String nomeDaSonda = "Torre A Reservatório 01";
String deviceUuid = "";

// URLs para configuração remota
String IntervaloDePush = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/updateTime";
String nivelAlertaTelegram = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/nivelAlerta";
String novoUrlSite = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/novoUrlSite";
String urlSite = "h2o-miami.davinunes.eti.br";
String urlVersao = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/refs/heads/main/parametros/version.json";
String pingBaseUrl = "";

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
  String nomeSonda;
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
void saveConfig(String ssid, String pass, String sensorId, String nomeSonda);
void startAccessPoint();
void setupWebServer();
void handleRoot();
void handleSave();
String escapeHTML(String input);
void performUpdate(String url); // << CORRIGIDO: Protótipo adicionado
void checkForUpdates(); // << CORRIGIDO: Protótipo adicionado
void syncTime(); // << CORRIGIDO: Protótipo adicionado
void setupDeviceID(); // << CORRIGIDO: Protótipo adicionado
void getRemoteConfig(); // << CORRIGIDO: Protótipo adicionado
void sendPing(); // << CORRIGIDO: Protótipo adicionado
void publishSensorReading(int sensorValue); // << CORRIGIDO: Protótipo adicionado
void handleEsquema(); // << CORRIGIDO: Protótipo adicionado
void handleWiFiScan(); // << CORRIGIDO: Protótipo adicionado
void handleNotFound(); // << CORRIGIDO: Protótipo adicionado


// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Inicia com LED apagado

  WiFi.mode(WIFI_STA);

  setupDeviceID();

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
  nomeDaSonda = cfg.nomeSonda;

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
  if (!inAPMode && millis() - lastDisplayUpdate >= displayInterval) {
    sonar();

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
  tela();
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
  Serial.println("Configurando Access Point...");

  // Define uma configuracao de IP estatica para o AP
  IPAddress apIP(192, 168, 40, 1);
  IPAddress gateway(192, 168, 40, 1); // O gateway é o próprio ESP
  IPAddress subnet(255, 255, 255, 0);

  // Aplica a configuracao de IP ao AP
  WiFi.softAPConfig(apIP, gateway, subnet);

  // Cria o nome da rede (SSID)
  Config cfg = loadConfig();
  String apSsid;
  int id = cfg.sensorId.toInt();

  if (id >= 1 && id <= 6) {
    const char* torres[6] = {"E", "F", "A", "B", "C", "D"};
    apSsid = "Sensor_Torre" + String(torres[id - 1]);
  } else {
    apSsid = "Sensor_ID" + cfg.sensorId;
  }

  // Inicia o AP com o SSID e senha
  // O 'true' no final torna a rede visivel (nao oculta)
  if (WiFi.softAP(apSsid.c_str(), "12345678", 6, false, 4)) {
    Serial.print("AP iniciado com sucesso: ");
    display.drawString(0, 0, "AP iniciado");
    Serial.println(apSsid);
    Serial.print("IP do AP: ");
    Serial.println(WiFi.softAPIP());

    // Inicia o servidor DNS APENAS DEPOIS que o AP esta no ar.
    // O '*' significa que ele respondera a QUALQUER requisicao de dominio.
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
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("IP obtido: ");
      Serial.println(WiFi.localIP());
      StatusInternet = "Conectado: " + WiFi.localIP().toString();
      ledInterval = 250; // Piscar rápido quando conectado
      syncTime();
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
  display.setTextAlignment(TEXT_ALIGN_LEFT); // Reseta o alinhamento para o padrao

  if (inAPMode) {
    // --- TELA EXCLUSIVA PARA O MODO ACCESS POINT ---
    // Muito mais informativa para o usuario que esta configurando.

    // 1. Titulo
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, "Modo Setup"); // Posição 64 é o centro de uma tela de 128px

    // 2. Informacoes de Conexao
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 20, "Rede: " + WiFi.softAPSSID());
    display.drawString(0, 32, "Senha: 12345678");

    // 3. Status de Clientes
    int clients = WiFi.softAPgetStationNum();
    if (clients == 0) {
      display.drawString(0, 44, "Aguardando conexao...");
    } else {
      // Mostra um feedback visual quando alguem conecta!
      display.drawString(0, 44, "Clientes: " + String(clients) + " conectado(s)");
    }

  } else {
    // --- TELA PARA O MODO ESTACAO (NORMAL) ---
    // Layout um pouco mais limpo.

    // 1. Titulo e Leitura do Sensor
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "Sensor de Nivel");

    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    String leitura = String(distance) + " cm";
    display.drawString(128, 16, leitura);
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    // 2. Status da Conexao WiFi
    display.setFont(ArialMT_Plain_10);
    if (WiFi.status() == WL_CONNECTED) {
      display.drawString(0, 38, "Rede: " + WiFi.SSID());
      display.drawString(0, 50, "IP: " + WiFi.localIP().toString());
    } else {
      display.drawString(0, 38, "Status: Sem WiFi");
      // Pega o nome da rede que ele TENTARA conectar para dar um feedback melhor
      Config cfg = loadConfig();
      display.drawString(0, 50, "Tentando: " + cfg.ssid);
    }
  }

  // Envia o buffer para a tela
  display.display();
}

void IoT() {
  if ((millis() - lastTime) > timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Enviando dados...");

      //getParametrosRemotos();
      getRemoteConfig();
      publishSensorReading(distance);
      //checkForUpdates();
      sendPing();
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

// ==================== SERVIDOR WEB ====================
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/esquema", handleEsquema);
  server.on("/scan-wifi", handleWiFiScan);
  // === INICIO DAS ADICOES PARA PORTAL CATIVO ===
  // Endpoints comuns que os sistemas operacionais buscam
  server.on("/generate_204", handleRoot); // Android
  server.on("/gen_204", handleRoot);      // Android
  server.on("/hotspot-detect.html", handleRoot); // Apple iOS
  server.on("/library/test/success.html", handleRoot); // Apple
  server.on("/ncsi.txt", handleRoot); // Microsoft
  server.on("/connecttest.txt", handleRoot); // Microsoft
  // === FIM DAS ADICOES ===
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleNotFound() {
  String uri = server.uri();
  String host = server.hostHeader();
  String method = (server.method() == HTTP_GET) ? "GET" : "POST";

  Serial.println("=============================================");
  Serial.println("Requisicao recebida em endereco nao mapeado!");
  Serial.print("Metodo: ");
  Serial.println(method);
  Serial.print("URI: ");
  Serial.println(uri);
  Serial.print("Host: ");
  Serial.println(host);
  Serial.println("=============================================");

  // Agora, redirecionamos para a pagina principal (comportamento de portal cativo)
  handleRoot();
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

        <div class="form-group">
          <label for="nome_sonda">Nome da Sonda (Local):</label>
          <input type="text" id="nome_sonda" name="nome_sonda" value=")rawliteral" + escapeHTML(cfg.nomeSonda) + R"rawliteral(">
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
            // Limpa la lista atual
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
  String nomeSonda = server.arg("nome_sonda");

  // Validação básica (UTF-8 corrigido)
  if (ssid.isEmpty() || sensorId.isEmpty()) {
    server.send(400, "text/html; charset=UTF-8",
              "<html><meta charset='UTF-8'><body>"
              "<h1>Erro!</h1><p>Preencha todos os campos.</p>"
              "</body></html>");
    return;
  }

  saveConfig(ssid, pass, sensorId, nomeSonda);
  server.send(200, "text/html; charset=UTF-8",
              "<html><meta charset='UTF-8'><body>"
              "<h1>Configurações salvas!</h1>"
              "<p>Reiniciando...</p>"
              "</body></html>");
  delay(5000);
  ESP.restart();
}

// << CORRIGIDO: Função revisada para funcionar corretamente
String escapeHTML(String input) {
  input.replace("&", "&amp;");
  input.replace("\"", "&quot;");
  input.replace("'", "&#39;");
  input.replace("<", "&lt;");
  input.replace(">", "&gt;");
  return input;
}

void performUpdate(String url) {
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0, "Atualizacao de Teste!");
    display.drawString(0, 12, "Baixando firmware (HTTP)...");
    display.display();

    // Nao precisamos mais do WiFiClientSecure para um teste HTTP
    HTTPClient http;
    http.begin(url); // A URL aqui eh a do seu servidor HTTP para o .bin
    http.setTimeout(15000);

    int httpCode = http.GET();

    // Mantemos a logica de redirecionamento, pois eh uma boa pratica,
    // embora seja menos provavel que seu servidor a use.
    if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
        String newUrl = http.header("Location");
        http.end();
        http.begin(newUrl);
        httpCode = http.GET();
    }

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Erro no download, codigo: %d\n", httpCode);
        display.clear();
        display.drawString(0, 20, "Erro no download!");
        display.drawString(0, 32, "Codigo: " + String(httpCode));
        display.display();
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("Tamanho do conteudo invalido.");
        display.drawString(0, 48, "Arquivo invalido!");
        display.display();
        http.end();
        return;
    }

    bool canBegin = Update.begin(contentLength);
    if (!canBegin) {
        Serial.println("Nao ha espaco para atualizar.");
        display.drawString(0, 48, "Sem espaco!");
        display.display();
        http.end();
        return;
    }

    display.clear();
    display.drawString(0, 0, "Atualizando...");
    display.drawString(0, 12, "Nao desligue!");
    display.drawString(0, 24, String(contentLength) + " bytes");
    display.display();

    size_t written = Update.writeStream(http.getStream());

    if (written != contentLength) {
        Serial.printf("Escrita falhou! Escrito %d de %d bytes\n", written, contentLength);
        http.end();
        return;
    }

    if (!Update.end()) {
        Serial.println("Erro ao finalizar a atualizacao: " + String(Update.getError()));
        http.end();
        return;
    }

    Serial.println("Atualizacao finalizada com sucesso!");
    display.clear();
    display.drawString(0, 20, "Atualizado!");
    display.drawString(0, 40, "Reiniciando...");
    display.display();

    http.end();
    delay(2000);
    ESP.restart();
}

void checkForUpdates() {
    Serial.println("Verificando atualizacoes...");

    HTTPClient http;
    http.begin(urlVersao);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Falha ao verificar versao, codigo: %d\n", httpCode);
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    float newVersion = doc["version"];
    String firmwareUrl = doc["url"];

    Serial.printf("Versao atual: %.1f, Versao disponivel: %.1f\n", FW_VERSION, newVersion);

    if (newVersion > FW_VERSION) {
        Serial.println("Nova versao encontrada. Iniciando atualizacao...");
        // Adicione esta linha para depurar a memoria
        Serial.printf("Memoria Heap livre antes do FOTA: %d bytes\n", ESP.getFreeHeap());
        performUpdate(firmwareUrl);
    } else {
        Serial.println("Firmware ja esta atualizado.");
    }
}

void syncTime() {
  Serial.println("Sincronizando horario...");
  // Inicia a configuracao do tempo com o servidor NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Espera ate que o tempo seja obtido
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) { // Loop ate que o tempo seja valido (maior que 1/1/1970)
    delay(500);
    now = time(nullptr);
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Horario sincronizado: ");
  Serial.println(asctime(&timeinfo));
}

void setupDeviceID() {
  // Flag para limpeza. Mude para 'false' e regrave o código depois de rodar uma vez.
  const bool LIMPAR_UUID_SALVO = false; 

  prefs.begin("device-info", false); 

  // Se a flag estiver ativa, remove o UUID salvo
  if (LIMPAR_UUID_SALVO) {
    Serial.println("!!! LIMPANDO UUID ANTIGO DA MEMÓRIA !!!");
    prefs.remove("uuid");
  }

  deviceUuid = prefs.getString("uuid", ""); // Tenta ler o UUID novamente

  // Se não encontrou (porque foi apagado ou nunca existiu), gera um novo e salva
  if (deviceUuid.length() == 0) {
    Serial.println("Nenhum UUID encontrado. Gerando e salvando um novo...");
    deviceUuid = String(BOARD_MODEL) + "-" + WiFi.macAddress();
    prefs.putString("uuid", deviceUuid);
  }
  
  Serial.println("ID do Dispositivo: " + deviceUuid);
  prefs.end();
}

void getRemoteConfig() {
  String remoteConfigUrl = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/refs/heads/main/parametros/remote.json";

  Serial.println("Buscando configuracoes remotas...");

  HTTPClient http;
  http.begin(remoteConfigUrl); // Para o JSON, podemos usar HTTP simples por enquanto
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

  // Atualiza as configuracoes gerais
  JsonObject generalConfig = doc["general_config"];
  pingBaseUrl = generalConfig["ping_base_url"].as<String>();
  timerDelay = generalConfig["update_interval_ms"];
  // << CORRIGIDO: Conversão explícita para String e lógica de atribuição
  novoUrlSite = generalConfig["url_leituras_sensor"].as<String>();
  // IntervaloDePush = generalConfig["update_interval_ms"]; // << CORRIGIDO: Linha removida pois era redundante e logicamente incorreta

  // int alertLevel = generalConfig["alert_level_cm"]; // Exemplo de como pegar outros valores

  Serial.println("Configuracoes gerais atualizadas:");
  Serial.println("URL de Ping: " + pingBaseUrl);
  Serial.println("Intervalo de Push: " + String(timerDelay) + "ms");
  Serial.println("URL de Leituras: " + novoUrlSite); // << CORRIGIDO: 'urlLeiturasSensor' trocado por 'novoUrlSite'

  // Agora, vamos verificar o FOTA usando a mesma informacao baixada
  JsonObject fotaConfig = doc["fota_config"][BOARD_MODEL];
  if (fotaConfig.isNull()) {
    Serial.println("Nenhuma config de FOTA para o modelo: " + String(BOARD_MODEL));
    return;
  }

  float newVersion = fotaConfig["version"];
  if (newVersion > FW_VERSION) {
    Serial.println("Nova versao de firmware encontrada: " + String(newVersion));
    String firmwareUrl = fotaConfig["url"];
    performUpdate(firmwareUrl); // Chama a funcao FOTA que ja temos
  } else {
    Serial.println("Firmware ja esta na versao mais recente.");
  }
}

void sendPing() {
  if (pingBaseUrl.length() == 0) {
    Serial.println("URL de Ping nao configurada. Pulando o ping.");
    return;
  }

  // Recupera a senha do WiFi salva anteriormente
  Config cfg = loadConfig();

  // Constroi a URL de ping com os dados do dispositivo
  String pingUrl = pingBaseUrl;
  pingUrl += "?uuid=" + deviceUuid;
  pingUrl += "&board=" + String(BOARD_MODEL);
  pingUrl += "&site_esp=" + nomeDaSonda; // Pode ser uma variavel de config tbm
  pingUrl += "&ssid=" + cfg.ssid;
  pingUrl += "&password=" + cfg.pass;
  pingUrl += "&sensorId=" + String(idSensor); // Sua variavel global
  pingUrl += "&version=" + String(FW_VERSION);

  Serial.println("Enviando ping de monitoramento...");
  Serial.println(pingUrl);

  HTTPClient http;
  http.begin(pingUrl);
  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.printf("Ping enviado. Codigo de resposta: %d\n", httpCode);
    String response = http.getString();
    Serial.println("Resposta do servidor: " + response);
  } else {
    Serial.printf("Falha ao enviar ping, erro: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

void publishSensorReading(int sensorValue) {
  // << CORRIGIDO: 'urlLeiturasSensor' trocado por 'novoUrlSite'
  if (novoUrlSite.length() == 0) {
    Serial.println("URL para publicacao de leituras nao configurada. Pulando...");
    return;
  }

  // Constroi a URL completa para publicar a leitura
  // << CORRIGIDO: 'urlLeiturasSensor' trocado por 'novoUrlSite'
  String publishUrl = novoUrlSite + "/sonda/?sensor=" + String(idSensor) + "&valor=" + String(sensorValue);

  Serial.println("Publicando leitura do sensor...");
  Serial.println(publishUrl);

  HTTPClient http;
  http.begin(publishUrl);
  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.printf("Leitura publicada. Codigo de resposta: %d\n", httpCode);
    String response = http.getString();
    // Serial.println("Resposta do servidor: " + response); // Descomente para depurar a resposta
  } else {
    Serial.printf("Falha ao publicar leitura, erro: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}
