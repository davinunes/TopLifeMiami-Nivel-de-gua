/*
  SKETCH ADAPTADO PARA A PLACA HELTEC WIFI LORA 32 (V3)
  - Modelo da Placa: ESP32-S3-HELTEC-WIFI-LORA-32-V3
  - Versão: 1.0
  - Display OLED integrado: SDA(17), SCL(18), RST(21), Vext Power(2)
  - Sensor Sonar: TRIGGER(33), ECHO(34)
  - Pinout: https://resource.heltec.cn/download/WiFi_LoRa_32_V3/HTIT-WB32LA_V3(Rev1.1).pdf
  - Pinout: https://www.espboards.dev/img/qS1QpyeC_N-1000.png
  - Dataseet: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
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
#include <time.h>

// ==================== NOVAS CONFIGURAÇÕES DA PLACA ====================
#define BOARD_MODEL "ESP32-S3-HELTEC-WIFI-LORA-32-V3"
#define FW_VERSION 1.0
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define OLED_VEXT_POWER 2 // PINO que controla a alimentação do OLED e outros periféricos

// ==================== CONFIGURAÇÕES ====================
#define LED_PIN 35 // LED onboard na Heltec V3 é o pino 35
#define AP_MODE_DURATION 120000

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600;
const int daylightOffset_sec = 0;

int idSensor = 3;
int distanciaSonda = 0;
int alturaAgua = 200;
String nomeDaSonda = "Torre A Reservatório 01";
String deviceUuid = "";
String IntervaloDePush = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/updateTime";
String nivelAlertaTelegram = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/nivelAlerta";
String novoUrlSite = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/novoUrlSite";
String urlSite = "h2o-miami.davinunes.eti.br";
String urlVersao = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/refs/heads/main/parametros/version.json";
String pingBaseUrl = "";

// ==================== VARIÁVEIS GLOBAIS ====================
// Display - Pinos atualizados para o OLED integrado da Heltec V3
SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

// Sonar - Pinos atualizados para melhor conexão na Heltec V3
#define TRIGGER_PIN  19
#define ECHO_PIN1    20
Ultrasonic sonar1(TRIGGER_PIN, ECHO_PIN1, 40000UL);
int distance = 0;

// Rede
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
String StatusInternet = "Sem Wifi...";

// Temporizadores
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 500;
unsigned long lastTime = 0;
unsigned long timerDelay = 15000;
unsigned long bootTime = 0;
bool inAPMode = false;

// LED
unsigned long lastLedToggle = 0;
bool ledState = false;
unsigned long ledInterval = 1000;

// ==================== ESTRUTURAS E PROTÓTIPOS ====================
struct Config {
  String ssid;
  String pass;
  String sensorId;
  String nomeSonda;
};

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
void performUpdate(String url);
void checkForUpdates();
void syncTime();
void setupDeviceID();
void getRemoteConfig();
void sendPing();
void publishSensorReading(int sensorValue);
void handleEsquema();
void handleWiFiScan();
void handleNotFound();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  
  // --- INICIALIZAÇÃO ESPECÍFICA DA HELTEC V3 ---
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Liga a alimentação (Vext) para o display OLED
  pinMode(OLED_VEXT_POWER, OUTPUT);
  digitalWrite(OLED_VEXT_POWER, LOW); // LOW ativa a alimentação
  delay(100);

  // Inicializa e reseta o display
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, HIGH); // Tira o display do modo reset
  delay(100);

  display.init();
  display.flipScreenVertically();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "Iniciando...");
  display.display();
  // --- FIM DA INICIALIZAÇÃO DA HELTEC ---

  WiFi.mode(WIFI_STA);
  setupDeviceID();

  Config cfg = loadConfig();
  idSensor = cfg.sensorId.toInt();
  nomeDaSonda = cfg.nomeSonda;
  
  WiFi.onEvent(handleWiFiEvent);

  bootTime = millis();
  switchToAPMode();
  setupWebServer();
  Serial.println("Setup completo");
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  if (!inAPMode && millis() - lastDisplayUpdate >= displayInterval) {
    sonar();
    IoT();
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
        <title>ESP32-S3 Heltec V3 - Esquema de Ligação</title>
        <style>
            body { display: flex; justify-content: center; align-items: center; min-height: 100vh; background-color: #f0f0f0; margin: 0; font-family: Arial, sans-serif; }
            .board-container { display: flex; gap: 50px; align-items: center; }
            .components-container { display: flex; flex-direction: column; gap: 30px; }
            .esp32-board, .sensor-board { background-color: #000; border: 2px solid #555; border-radius: 8px; position: relative; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3); display: flex; justify-content: space-between; padding: 0 10px; box-sizing: border-box; }
            .esp32-board { width: 120px; height: 500px; }
            .sensor-board { width: 160px; height: 40px; align-items: flex-end; padding: 5px; }
            .board-label { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); color: #eee; font-size: 1.2em; font-weight: bold; text-shadow: 1px 1px 2px rgba(0,0,0,0.5); pointer-events: none; white-space: nowrap; }
            .sensor-label { color: #eee; font-weight: bold; margin: 0 auto 8px; }
            .usb-c-port { width: 30px; height: 12px; background-color: #888; border-radius: 3px; position: absolute; top: -7px; left: 50%; transform: translateX(-50%); z-index: 10; border: 1px solid #666; }
            .pin-column { display: flex; flex-direction: column; justify-content: space-around; height: 100%; position: absolute; }
            .pin-column.left { left: -10px; }
            .pin-column.right { right: -10px; }
            .pin-row-bottom { display: flex; justify-content: space-around; width: 100%; position: absolute; bottom: -10px; left: 0; padding: 0 5px; box-sizing: border-box; }
            .pin { width: 22px; height: 22px; background-color: #bbb; border-radius: 50%; border: 1px solid #888; box-sizing: border-box; display: flex; justify-content: center; align-items: center; font-size: 8px; color: #333; font-weight: bold; margin: 2px 0; flex-shrink: 0; }
            .pin-row-bottom .pin { margin: 0 2px; }
            .pin.vcc5, .pin.vcc3, .pin.gnd, .pin.trig, .pin.echo { color: white; border-color: #fff; }
            .pin.vcc5 { background-color: red; }
            .pin.vcc3 { background-color: orange; }
            .pin.gnd { background-color: black; }
            .pin.trig { background-color: #007bff; }
            .pin.echo { background-color: #ffc107; color: black; }
        </style>
    </head>
    <body>
        <div class="board-container">
            <div class="esp32-board">
                <div class="board-label">Heltec WiFi LoRa 32 (V3)</div>
                <div class="usb-c-port"></div>
                <div class="pin-column left" style="flex-direction: column-reverse;">
                    <div class="pin " title="GND">GND</div> 
					<div class="pin " title="3V3">3V3</div> 
					<div class="pin " title="3V3">3V3</div> 
					<div class="pin" title="IO37">37</div> 
					<div class="pin" title="IO46">46</div> 
					<div class="pin" title="IO45">45</div> 
					<div class="pin" title="IO42">42</div> 
					<div class="pin" title="IO41">41</div> 
					<div class="pin" title="IO40">40</div> 
					<div class="pin" title="IO39">39</div> 
					<div class="pin" title="IO38">38</div> 
					<div class="pin" title="IO1">1</div> 
					<div class="pin" title="IO2">2</div> 
					<div class="pin" title="IO3">3</div> 
					<div class="pin" title="IO4">4</div> 
					<div class="pin" title="IO5">5</div> 
					<div class="pin" title="IO6">6</div> 
					<div class="pin" title="IO7">7</div>
                </div>
                <div class="pin-column right" style="flex-direction: column-reverse;">
                    <div class="pin gnd" title="GND">GND</div> 
					<div class="pin vcc5" title="5V">5V</div> 
					<div class="pin" title="VE">VE</div> 
					<div class="pin" title="VE">VE</div> 
					<div class="pin" title="RX">RX</div> 
					<div class="pin" title="TX">TX</div> 
					<div class="pin" title="RST">RST</div> 
					<div class="pin" title="IO0">0</div> 
					<div class="pin" title="IO36">36</div> 
					<div class="pin" title="IO35">35</div> 
					<div class="pin" title="IO34" id="echo_pin">34</div> 
					<div class="pin" id="trig_pin">33</div> 
					<div class="pin" title="IO47">47</div> 
					<div class="pin" title="IO48">48</div> 
					<div class="pin" title="IO26">26</div> 
					<div class="pin" title="IO21">21</div> 
					<div class="pin trig" title="IO20">20</div> 
					<div class="pin echo" title="IO19">19</div>
                </div>
            </div>
            <div class="components-container">
                <div class="sensor-board">
                    <div class="sensor-label">Sensor HC-SR04</div>
                    <div class="pin-row-bottom">
                        <div class="pin vcc5" title="VCC -> 5V">VCC</div>
                        <div class="pin trig" title="TRIG -> D33">TRI</div>
                        <div class="pin echo" title="ECHO -> D34">ECH</div>
                        <div class="pin gnd" title="GND -> GND">GND</div>
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
  prefs.begin("device-info", false); 
  deviceUuid = prefs.getString("uuid", "");

  String invalidMacSuffix = "00:00:00:00:00:00";
  bool needsRegeneration = false;

  if (deviceUuid.length() == 0) {
    Serial.println("Nenhum UUID encontrado na memoria.");
    needsRegeneration = true;
  } else if (deviceUuid.endsWith(invalidMacSuffix)) {
    Serial.println("UUID com MAC invalido detectado.");
    needsRegeneration = true;
  }

  if (needsRegeneration) {
    Serial.println("Iniciando processo para gerar um novo ID de dispositivo...");
    String macAddress = "";
    int retries = 0;
    const int maxRetries = 20; // Tenta obter por 2 segundos

    // Tenta obter um MAC address válido, pois o hardware pode não estar pronto imediatamente
    while(retries < maxRetries) {
        macAddress = WiFi.macAddress();
        if (macAddress.length() > 0 && macAddress != invalidMacSuffix) {
            Serial.print("MAC Address obtido com sucesso: ");
            Serial.println(macAddress);
            break; // Sai do loop se o MAC for válido
        }
        Serial.printf("Tentativa %d/%d: MAC invalido, tentando novamente...\n", retries + 1, maxRetries);
        delay(100);
        retries++;
    }

    // Se o MAC ainda for inválido após as tentativas, informa o erro.
    if(macAddress.length() == 0 || macAddress == invalidMacSuffix) {
        Serial.println("ERRO: Nao foi possivel obter um MAC address valido apos varias tentativas.");
    }

    // Cria o novo UUID (mesmo que o MAC seja inválido, para tentar novamente no próximo boot)
    // e salva na memória persistente
    deviceUuid = String(BOARD_MODEL) + "-" + macAddress;
    prefs.putString("uuid", deviceUuid);
    Serial.println("Novo UUID salvo na memoria.");
  }

  Serial.println("ID do Dispositivo final: " + deviceUuid);
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
