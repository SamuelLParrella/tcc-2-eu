#include <LiquidCrystal_I2C.h>

#include <MFRC522.h>
#include <MFRC522Extended.h>
#include <deprecated.h>
#include <require_cpp11.h>

#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ==========================
// WIFI
// ==========================

const char* ssid = "REIS_2.4G";
const char* password = "13042110";

// ==========================
// API QUICKPASS
// ==========================

// Cartao fisico (por UID)
const char* apiURL =
  "https://tcc-82gl.onrender.com/cartao/verificar";

// Celular com o app BusPasse (por token NFC)
const char* apiURLNfc =
  "https://tcc-82gl.onrender.com/nfc/validar";

// ==========================
// RC522
// ==========================

#define SS_PIN 5
#define RST_PIN 4

MFRC522 rfid(SS_PIN, RST_PIN);

// ==========================
// CELULAR (NFC) — AID do BusPasse
//
// Precisa ser igual ao AID do app
// (modules/buspass-hce): F0 + "BUSPASS".
//
// Comando SELECT: 00 A4 04 00 08 <AID> 00
// ==========================

const byte SELECT_BUSPASS[] = {
  0x00, 0xA4, 0x04, 0x00, 0x08,
  0xF0, 0x42, 0x55, 0x53, 0x50, 0x41, 0x53, 0x53,
  0x00
};

// Numero do bloco ISO-DEP (alterna 0/1 a cada I-block)
byte blocoAtual = 0;

// ==========================
// LCD
// ==========================

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==========================
// LEDs
// ==========================

#define LED_VERMELHO 32
#define LED_BRANCO   33
#define LED_VERDE    25

// ==========================
// ESTADO DOS LEDs
// ==========================

void ledAguardando() {
  digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(LED_BRANCO, HIGH);
  digitalWrite(LED_VERDE, LOW);
}

void ledValido() {
  digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(LED_BRANCO, LOW);
  digitalWrite(LED_VERDE, HIGH);
}

void ledInvalido() {
  digitalWrite(LED_VERMELHO, HIGH);
  digitalWrite(LED_BRANCO, LOW);
  digitalWrite(LED_VERDE, LOW);
}

// ==========================
// TELA INICIAL
// ==========================

void telaInicial() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   QUICKPASS");
  lcd.setCursor(0, 1);
  lcd.print("Aproxime cartao");
}

// ==========================
// VERIFICAR UID NA API
//
// A API responde, por exemplo:
//  {"cadastrado":true,"cartao_bloqueado":false,"autorizado":true,...}
//
// Retorno:
//  1 = autorizado (cadastrado e nao bloqueado)
//  0 = negado (nao cadastrado ou bloqueado)
// -1 = erro de conexao/API
//
// cartaoBloqueado fica true quando o cartao existe mas esta bloqueado.
// ==========================

String extrairValorJSON(const String &json, const char* chave);

bool cartaoBloqueado = false;

int verificarCartaoNaAPI(String uid) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi desconectado.");
    return -1;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure(); // Apenas para prototipo/testes

  HTTPClient https;

  if (!https.begin(cliente, apiURL)) {
    Serial.println("Falha ao iniciar HTTPS.");
    return -1;
  }

  https.setTimeout(15000);
  https.addHeader("Content-Type", "application/json");

  String corpo = "{\"uid_cartao\":\"" + uid + "\"}";

  Serial.println("Consultando API QUICKPASS...");
  Serial.println("UID enviado: " + uid);

  int codigoHTTP = https.POST(corpo);
  String resposta = "";

  if (codigoHTTP > 0) {
    resposta = https.getString();

    Serial.print("HTTP: ");
    Serial.println(codigoHTTP);

    Serial.print("Resposta: ");
    Serial.println(resposta);
  } else {
    Serial.print("Erro HTTP: ");
    Serial.println(https.errorToString(codigoHTTP));
  }

  https.end();

  if (codigoHTTP != 200) {
    return -1;
  }

  // Interpreta a resposta JSON da API
  cartaoBloqueado = (extrairValorJSON(resposta, "cartao_bloqueado") == "true");

  String autorizado = extrairValorJSON(resposta, "autorizado");
  if (autorizado == "true") return 1;
  if (autorizado == "false") return 0;

  // API antiga (so com "cadastrado")
  int posCadastro = resposta.indexOf("\"cadastrado\"");
  int posTrue = resposta.indexOf("true", posCadastro);
  int posFalse = resposta.indexOf("false", posCadastro);

  if (posCadastro >= 0 &&
      posTrue >= 0 &&
      (posFalse < 0 || posTrue < posFalse)) {
    return 1;
  }

  if (posCadastro >= 0 &&
      posFalse >= 0 &&
      (posTrue < 0 || posFalse < posTrue)) {
    return 0;
  }

  Serial.println("Resposta da API inesperada.");
  return -1;
}

// ==========================
// CELULAR: VALIDAR TOKEN NA API
//
// Envia o token lido do celular para /nfc/validar.
// A API confere token, cartao bloqueado e saldo, e
// debita a passagem se estiver tudo certo.
//
// Retorno:
//  1 = aprovado
//  0 = negado
// -1 = erro de conexao/API
//
// motivo e saldo vem da resposta da API:
//  {"cadastrado":true,"motivo":"ok","saldo":4.25,"tarifa":5.75}
// ==========================

// Pega o valor de uma chave simples do JSON (texto ou numero)
String extrairValorJSON(const String &json, const char* chave) {
  String busca = String("\"") + chave + "\"";
  int pos = json.indexOf(busca);
  if (pos < 0) return "";

  pos = json.indexOf(':', pos + busca.length());
  if (pos < 0) return "";
  pos++;

  while (pos < (int)json.length() && json[pos] == ' ') pos++;

  if (pos < (int)json.length() && json[pos] == '"') {
    int fim = json.indexOf('"', pos + 1);
    if (fim < 0) return "";
    return json.substring(pos + 1, fim);
  }

  int fim = pos;
  while (fim < (int)json.length() &&
         json[fim] != ',' && json[fim] != '}' && json[fim] != ' ') {
    fim++;
  }
  return json.substring(pos, fim);
}

int validarTokenNaAPI(String token, String &motivo, String &saldo, String &tarifa) {

  motivo = "";
  saldo = "";
  tarifa = "";

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi desconectado.");
    return -1;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure(); // Apenas para prototipo/testes

  HTTPClient https;

  if (!https.begin(cliente, apiURLNfc)) {
    Serial.println("Falha ao iniciar HTTPS.");
    return -1;
  }

  https.setTimeout(15000);
  https.addHeader("Content-Type", "application/json");

  String corpo = "{\"token_nfc\":\"" + token + "\"}";

  Serial.println("Consultando API QUICKPASS (celular)...");
  Serial.println("Token enviado: " + token);

  int codigoHTTP = https.POST(corpo);
  String resposta = "";

  if (codigoHTTP > 0) {
    resposta = https.getString();

    Serial.print("HTTP: ");
    Serial.println(codigoHTTP);

    Serial.print("Resposta: ");
    Serial.println(resposta);
  } else {
    Serial.print("Erro HTTP: ");
    Serial.println(https.errorToString(codigoHTTP));
  }

  https.end();

  if (codigoHTTP != 200) {
    return -1;
  }

  String aprovado = extrairValorJSON(resposta, "cadastrado");
  motivo = extrairValorJSON(resposta, "motivo");
  saldo = extrairValorJSON(resposta, "saldo");
  tarifa = extrairValorJSON(resposta, "tarifa");

  if (aprovado == "true") return 1;
  if (aprovado == "false") return 0;

  Serial.println("Resposta da API inesperada.");
  return -1;
}

// ==========================
// CELULAR: LEITURA POR NFC (ISO-DEP)
//
// O celular com o app BusPasse se comporta como um
// cartao ISO 14443-4. O fluxo e:
//   1. RATS  -> o celular responde com o ATS
//   2. SELECT do AID do BusPasse (I-block)
//   3. O celular responde "BP1:" + token + 90 00
//   4. DESELECT
//
// Tudo e feito com as funcoes basicas da MFRC522
// (PCD_TransceiveData + CRC calculado a mao).
// ==========================

void imprimirHex(const char* rotulo, const byte* dados, byte tamanho) {
  Serial.print(rotulo);
  for (byte i = 0; i < tamanho; i++) {
    if (dados[i] < 0x10) Serial.print("0");
    Serial.print(dados[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

// Envia um quadro (acrescenta o CRC_A) e recebe a resposta (sem o CRC_A).
// Retorna true se deu certo.
bool trocarQuadro(const byte* envio, byte envioTam, byte* resposta, byte* respostaTam) {

  byte buf[64];
  if (envioTam > sizeof(buf) - 2) return false;

  memcpy(buf, envio, envioTam);

  byte status = rfid.PCD_CalculateCRC(buf, envioTam, &buf[envioTam]);
  if (status != MFRC522::STATUS_OK) return false;

  byte recebidos = *respostaTam;

  status = rfid.PCD_TransceiveData(buf, envioTam + 2, resposta, &recebidos, nullptr, 0, true);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Falha na troca de quadro, status ");
    Serial.println(status);
    return false;
  }

  if (recebidos < 2) return false;

  *respostaTam = recebidos - 2; // tira o CRC_A
  return true;
}

// RATS: pede ao celular para entrar no modo ISO-DEP.
bool enviarRATS() {

  const byte rats[] = { 0xE0, 0x50 }; // FSDI = 5 (64 bytes), CID = 0
  byte ats[32];
  byte atsTam = sizeof(ats);

  if (!trocarQuadro(rats, sizeof(rats), ats, &atsTam)) return false;
  if (atsTam < 1) return false;

  imprimirHex("ATS: ", ats, atsTam);

  // Se o celular pediu um tempo de guarda antes do primeiro comando
  // (SFGI no TB1 do ATS), respeita.
  byte t0 = (atsTam >= 2) ? ats[1] : 0;
  byte indice = 2;
  if (t0 & 0x10) indice++;                       // TA1
  if ((t0 & 0x20) && indice < atsTam) {          // TB1
    byte sfgi = ats[indice] & 0x0F;
    if (sfgi > 0 && sfgi < 15) {
      delay((302UL << sfgi) / 1000UL + 1);
    }
  }

  return true;
}

// Envia um APDU dentro de um I-block e devolve os dados da resposta.
bool trocarAPDU(const byte* apdu, byte apduTam, byte* resposta, byte* respostaTam) {

  byte envio[64];
  if (apduTam > sizeof(envio) - 1) return false;

  envio[0] = 0x02 | (blocoAtual & 0x01); // I-block, sem CID/NAD
  memcpy(&envio[1], apdu, apduTam);

  byte buf[64];
  byte tam = sizeof(buf);

  if (!trocarQuadro(envio, apduTam + 1, buf, &tam)) return false;

  // O celular pode pedir mais tempo (S-block WTX): responde e espera de novo.
  for (byte i = 0; i < 3 && tam >= 2 && (buf[0] & 0xF7) == 0xF2; i++) {
    byte wtx[2] = { buf[0], buf[1] };
    tam = sizeof(buf);
    if (!trocarQuadro(wtx, sizeof(wtx), buf, &tam)) return false;
  }

  // Precisa ser um I-block
  if (tam < 1 || (buf[0] & 0xE2) != 0x02) {
    Serial.println("Resposta nao e um I-block.");
    return false;
  }

  blocoAtual ^= 1;

  byte dados = tam - 1;
  if (dados > *respostaTam) return false;

  memcpy(resposta, &buf[1], dados);
  *respostaTam = dados;
  return true;
}

// Encerra a sessao ISO-DEP (S-block DESELECT).
void deselecionar() {
  const byte deselect[] = { 0xC2 };
  byte resp[8];
  byte tam = sizeof(resp);
  trocarQuadro(deselect, sizeof(deselect), resp, &tam);
}

// Tenta ler o token do celular. Retorna true e preenche `token` se der certo.
bool lerTokenDoCelular(String &token) {

  token = "";
  blocoAtual = 0;

  if (!enviarRATS()) {
    Serial.println("RATS falhou.");
    return false;
  }

  byte resp[48];
  byte respTam = sizeof(resp);

  if (!trocarAPDU(SELECT_BUSPASS, sizeof(SELECT_BUSPASS), resp, &respTam)) {
    Serial.println("SELECT do BusPasse falhou.");
    deselecionar();
    return false;
  }

  deselecionar();

  imprimirHex("Resposta: ", resp, respTam);

  // Esperado: "BP1:" + 16 caracteres + 90 00
  if (respTam != 4 + 16 + 2) {
    Serial.println("Tamanho de resposta inesperado (celular sem token armado?).");
    return false;
  }

  if (resp[respTam - 2] != 0x90 || resp[respTam - 1] != 0x00) {
    Serial.println("Celular recusou (SW diferente de 90 00).");
    return false;
  }

  if (resp[0] != 'B' || resp[1] != 'P' || resp[2] != '1' || resp[3] != ':') {
    Serial.println("Prefixo do token invalido.");
    return false;
  }

  for (byte i = 4; i < 4 + 16; i++) {
    token += (char)resp[i];
  }

  return true;
}

// Reinicia o campo de RF e seleciona o cartao/celular de novo (para tentar outra vez).
bool reselecionar() {
  rfid.PCD_AntennaOff();
  delay(40);
  rfid.PCD_AntennaOn();
  delay(40);

  if (!rfid.PICC_IsNewCardPresent()) return false;
  if (!rfid.PICC_ReadCardSerial()) return false;
  return true;
}

// ==========================
// CELULAR: MOSTRAR RESULTADO NO LCD/LEDS
// ==========================

String saldoParaLCD(String saldo) {
  char texto[16];
  snprintf(texto, sizeof(texto), "%.2f", saldo.toFloat());
  String formatado = String(texto);
  formatado.replace(".", ",");
  return formatado;
}

void mostrarResultadoCelular(int resultado, String motivo, String saldo, String tarifa) {

  if (resultado == 1) {

    Serial.println("CELULAR VALIDO");

    ledValido();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CELULAR VALIDO");
    lcd.setCursor(0, 1);

    if (tarifa.toFloat() > 0 && saldo.length() > 0) {
      lcd.print("Saldo R$ " + saldoParaLCD(saldo));
    } else {
      lcd.print("Acesso liberado");
    }
  }

  else if (resultado == 0) {

    Serial.println("CELULAR NEGADO: " + motivo);

    ledInvalido();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ACESSO NEGADO");
    lcd.setCursor(0, 1);

    if (motivo == "saldo_insuficiente") {
      lcd.print("Saldo insuf.");
    } else if (motivo == "cartao_bloqueado") {
      lcd.print("Cartao bloqueado");
    } else if (motivo == "token_expirado") {
      lcd.print("Codigo expirado");
    } else if (motivo == "token_invalido") {
      lcd.print("Codigo invalido");
    } else {
      lcd.print("Tente novamente");
    }
  }

  else {

    Serial.println("ERRO NA CONSULTA API");

    // Nao libera acesso quando nao consegue validar
    ledInvalido();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro na consulta");
    lcd.setCursor(0, 1);
    lcd.print("Tente novamente");
  }
}

// ==========================
// SETUP
// ==========================

void setup() {

  Serial.begin(115200);

  // LEDs
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(LED_BRANCO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);

  ledAguardando();

  // LCD
  Wire.begin(21, 22);
  lcd.begin(16, 2);
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectando WiFi");

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Conectando ao Wi-Fi");

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - inicio < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi conectado!");
    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.localIP());

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi conectado");
  } else {
    Serial.println("\nFalha ao conectar ao Wi-Fi.");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Falha no WiFi");
  }

  delay(1000);

  // RC522
  SPI.begin();
  rfid.PCD_Init();

  // Se o celular nao for detectado com facilidade, tire as barras da linha
  // abaixo (aumenta a sensibilidade de recepcao do RC522):
  // rfid.PCD_SetAntennaGain(rfid.RxGain_max);

  telaInicial();

  Serial.println("QUICKPASS iniciado.");
}

// ==========================
// LOOP PRINCIPAL
// ==========================

void loop() {

  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // ==========================
  // CELULAR COM O APP BUSPASSE
  //
  // SAK com o bit 0x20 = ISO-DEP (14443-4). Celulares Android que
  // emulam cartao aparecem assim, com UID aleatorio (comeca com 08).
  // Se nao for um celular do BusPasse e nao for UID aleatorio
  // (ex.: outro cartao ISO-DEP), segue para o fluxo normal por UID.
  // ==========================

  if (rfid.uid.sak & 0x20) {

    bool uidAleatorio = (rfid.uid.size == 4 && rfid.uid.uidByte[0] == 0x08);

    // Nada de LCD aqui: o RATS precisa sair logo depois da selecao,
    // antes de qualquer outra coisa demorada.
    Serial.println();
    Serial.println("Dispositivo ISO-DEP detectado (celular?)");

    String token = "";
    bool lido = false;

    for (byte tentativa = 1; tentativa <= 3 && !lido; tentativa++) {

      if (tentativa > 1) {
        Serial.print("Tentativa ");
        Serial.println(tentativa);

        if (!reselecionar()) {
          break;
        }
      }

      lido = lerTokenDoCelular(token);
    }

    if (lido) {

      String motivo = "";
      String saldo = "";
      String tarifa = "";

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Verificando...");
      lcd.setCursor(0, 1);
      lcd.print("Aguarde");

      int resultado = validarTokenNaAPI(token, motivo, saldo, tarifa);

      mostrarResultadoCelular(resultado, motivo, saldo, tarifa);

      delay(3000);

      ledAguardando();
      telaInicial();
      return;
    }

    if (uidAleatorio) {

      // Parece um celular, mas nao entregou o token
      Serial.println("CELULAR NAO LIDO");

      ledInvalido();

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Celular nao lido");
      lcd.setCursor(0, 1);
      lcd.print("Ative no app");

      delay(3000);

      ledAguardando();
      telaInicial();
      return;
    }

    // Nao era celular: segue como cartao fisico. A sessao ISO-DEP pode
    // ter sido desfeita, entao seleciona o cartao de novo.
    if (!reselecionar()) {
      ledAguardando();
      telaInicial();
      return;
    }
  }

  // Monta o UID em hexadecimal, sem separadores
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {

    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    String parte = String(rfid.uid.uidByte[i], HEX);
    parte.toUpperCase();
    uid += parte;
  }

  Serial.println();
  Serial.println("Cartao detectado!");
  Serial.println("UID: " + uid);

  // Finaliza a leitura antes da consulta HTTP
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  // Indica consulta em andamento
  ledAguardando();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Verificando...");
  lcd.setCursor(0, 1);
  lcd.print("Aguarde");

  int resultado = verificarCartaoNaAPI(uid);

  // ==========================
  // CADASTRADO
  // ==========================

  if (resultado == 1) {

    Serial.println("CARTAO CADASTRADO");

    ledValido();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CARTAO VALIDO");
    lcd.setCursor(0, 1);
    lcd.print("Acesso liberado");
  }

  // ==========================
  // NAO CADASTRADO
  // ==========================

  else if (resultado == 0) {

    Serial.println(cartaoBloqueado ? "CARTAO BLOQUEADO" : "CARTAO NAO CADASTRADO");

    ledInvalido();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(cartaoBloqueado ? "CARTAO BLOQUEADO" : "CARTAO INVALIDO");
    lcd.setCursor(0, 1);
    lcd.print("Acesso negado");
  }

  // ==========================
  // ERRO DE CONEXAO/API
  // ==========================

  else {

    Serial.println("ERRO NA CONSULTA API");

    // Nao libera acesso quando nao consegue validar
    ledInvalido();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro na consulta");
    lcd.setCursor(0, 1);
    lcd.print("Tente novamente");
  }

  delay(3000);

  ledAguardando();
  telaInicial();
}
