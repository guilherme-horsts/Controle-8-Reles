#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

// CONFIG WiFi
const char* ssid     = "VIVOFIBRA-8501";
const char* password = "fmZXQ78rNG";
//const char* ssid = "SEU_SSID";
//const char* password = "SUA_SENHA";

// RELÉS conectados aos GPIOs:
const int relePins[8] = {5, 4, 14, 12, 13, 15, 0, 2}; // D1, D2, D5, D6, D7, D8, D3, D4
bool releStates[8] = {false, false, false, false, false, false, false, false};

// ESTRUTURA DOS AGENDAMENTOS
struct Agendamento {
  uint8_t hora;
  uint8_t minuto;
  uint8_t rele;   // 0 a 7
  bool ligar;     // true = ligar, false = desligar
};

#define MAX_AGENDAMENTOS 32
Agendamento agendamentos[MAX_AGENDAMENTOS];
int totalAgendamentos = 0;

// EEPROM
#define EEPROM_SIZE 1024
#define EEPROM_START 0

// NTP e WebServer
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600); // UTC-3 Brasil
ESP8266WebServer server(80);

// ====================== SETUP ==========================
void setup() {
  Serial.begin(115200);

  // Inicializa relés
  for (int i = 0; i < 8; i++) {
    pinMode(relePins[i], OUTPUT);
    digitalWrite(relePins[i], HIGH); // Desligado (ativo em LOW)
  }

  // Conecta WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.println(".");
  }
  Serial.println("\nWiFi conectado: " + WiFi.localIP().toString());

  // Inicia NTP
  timeClient.begin();

  // Carrega agendamentos da EEPROM
  carregarAgendamentosEEPROM();

  // Rotas HTTP
  server.on("/", handleRoot);

  for (int i = 0; i < 8; i++) {
    server.on(("/on" + String(i)).c_str(), [i]() {
      setRele(i, true);
      server.sendHeader("Location", "/"); server.send(303);
    });
    server.on(("/off" + String(i)).c_str(), [i]() {
      setRele(i, false);
      server.sendHeader("Location", "/"); server.send(303);
    });
  }

  server.on("/on", HTTP_GET, []() {
    for (int i = 0; i < 8; i++) setRele(i, true);
    server.sendHeader("Location", "/"); server.send(303);
  });

  server.on("/off", HTTP_GET, []() {
    for (int i = 0; i < 8; i++) setRele(i, false);
    server.sendHeader("Location", "/"); server.send(303);
  });

  server.on("/add", HTTP_POST, handleAdd);
  server.begin();
}

// ====================== LOOP ==========================
void loop() {
  server.handleClient();
  timeClient.update();

  static int lastMinute = -1;
  int hora = timeClient.getHours();
  int minuto = timeClient.getMinutes();

  if (minuto != lastMinute) {
    lastMinute = minuto;
    verificarAgendamentos(hora, minuto);
  }
}

// ====================== RELÉS ==========================
void setRele(int index, bool ligar) {
  releStates[index] = ligar;
  digitalWrite(relePins[index], ligar ? LOW : HIGH); // ativo em LOW
}

// =================== AGENDAMENTOS ======================
void verificarAgendamentos(int hora, int minuto) {
  for (int i = 0; i < totalAgendamentos; i++) {
    if (agendamentos[i].hora == hora && agendamentos[i].minuto == minuto) {
      setRele(agendamentos[i].rele, agendamentos[i].ligar);
    }
  }
}

// =================== EEPROM ============================
void salvarAgendamentosEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(EEPROM_START, totalAgendamentos);
  for (int i = 0; i < totalAgendamentos; i++) {
    EEPROM.put(EEPROM_START + sizeof(int) + i * sizeof(Agendamento), agendamentos[i]);
  }
  EEPROM.commit();
}

void carregarAgendamentosEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(EEPROM_START, totalAgendamentos);
  if (totalAgendamentos > MAX_AGENDAMENTOS) totalAgendamentos = 0;
  for (int i = 0; i < totalAgendamentos; i++) {
    EEPROM.get(EEPROM_START + sizeof(int) + i * sizeof(Agendamento), agendamentos[i]);
  }
}

// =================== INTERFACE =========================
void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><title>Control_Rele_8</title></head><body>";
  html += "<h2>Control_Rele_8 - " + timeClient.getFormattedTime() + "</h2>";

  for (int i = 0; i < 8; i++) {
    html += "Relé " + String(i + 1) + ": <b>" + (releStates[i] ? "Ligado" : "Desligado") + "</b> ";
    html += "<a href='/on" + String(i) + "'>Ligar</a> ";
    html += "<a href='/off" + String(i) + "'>Desligar</a><br>";
  }

  html += "<br><a href='/on'>Ligar Todos</a> | <a href='/off'>Desligar Todos</a><br><br>";

  html += "<h3>Agendamentos</h3><ul>";
  for (int i = 0; i < totalAgendamentos; i++) {
    html += "<li>" + formatarHoraMinuto(agendamentos[i].hora, agendamentos[i].minuto) +
            " - Relé " + String(agendamentos[i].rele + 1) +
            " - " + (agendamentos[i].ligar ? "Ligar" : "Desligar") + "</li>";
  }
  html += "</ul>";

  html += R"rawliteral(
    <h3>Adicionar Agendamento</h3>
    <form method='POST' action='/add'>
      Hora: <input type='number' name='hora' min='0' max='23' required><br>
      Minuto: <input type='number' name='minuto' min='0' max='59' required><br>
      Relé (1-8): <input type='number' name='rele' min='1' max='8' required><br>
      Ação:
      <select name='acao'>
        <option value='1'>Ligar</option>
        <option value='0'>Desligar</option>
      </select><br><br>
      <input type='submit' value='Adicionar'>
    </form>
  )rawliteral";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleAdd() {
  if (totalAgendamentos >= MAX_AGENDAMENTOS) {
    server.send(400, "text/plain", "Limite de agendamentos atingido.");
    return;
  }

  int h = server.arg("hora").toInt();
  int m = server.arg("minuto").toInt();
  int r = server.arg("rele").toInt() - 1;
  bool acao = (server.arg("acao") == "1");

  if (h < 0 || h > 23 || m < 0 || m > 59 || r < 0 || r > 7) {
    server.send(400, "text/plain", "Parâmetros inválidos.");
    return;
  }

  agendamentos[totalAgendamentos++] = { (uint8_t)h, (uint8_t)m, (uint8_t)r, acao };
  salvarAgendamentosEEPROM();

  server.sendHeader("Location", "/");
  server.send(303);
}

// =================== FORMATADOR ========================
String formatarHoraMinuto(int h, int m) {
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", h, m);
  return String(buffer);
}
