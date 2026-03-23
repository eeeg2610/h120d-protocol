/*
  H120D Drone Controller — Arduino Nano 33 IoT
  ==============================================
  Autonomous drone control over WiFi. Connects to the drone's AP,
  performs the full protocol handshake, and sends flight commands.

  Serial commands (115200 baud):
    STATUS    — Print connection state
    HANDSHAKE — Run handshake sequence
    HEARTBEAT — Toggle heartbeat (1Hz)
    GPSSTREAM — Toggle GPS status stream (5Hz)
    GPS <N>   — Set GPS accuracy in meters (3 = ready for flight)
    TAKEOFF   — Send takeoff burst
    LAND      — Send land burst
    GOHOME    — Send return-to-home burst
    ESTOP     — Emergency stop, kill all streams
    AUTO      — Full autonomous sequence (handshake → GPS → takeoff)
    TOPEN     — Open telnet to drone (TCP 23)
    TCMD <c>  — Send finsh command via telnet
    TCLOSE    — Close telnet connection
    HELP      — Show all commands

  Board:    Arduino Nano 33 IoT
  Library:  WiFiNINA (install via Library Manager)
  Baud:     115200

  IMPORTANT: The physical RC must arm the motors before WiFi takeoff works.
*/

#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>

// ============================================================
// CONFIGURATION — Change DRONE_SSID to match your drone
// ============================================================
const char* DRONE_SSID     = "HolyStoneFPV-XXXXXX";  // Your drone's SSID
const char* DRONE_PASS     = "";                       // Open network
const IPAddress DRONE_IP(172, 16, 10, 1);
const int CMD_PORT         = 8080;

// Timing (from stock app capture analysis)
const unsigned long HEARTBEAT_INTERVAL_MS  = 1000;   // 1Hz
const unsigned long GPS_STATUS_INTERVAL_MS = 200;    // 5Hz
const unsigned long CONTROL_INTERVAL_MS    = 20;     // 50Hz
const unsigned long PRE_FLIGHT_DURATION_MS = 5000;   // GPS stream before takeoff
const int TAKEOFF_BURST_COUNT              = 75;     // Packets per burst

// Flight control flags
const uint8_t FLAG_TAKEOFF = 0x01;
const uint8_t FLAG_LAND    = 0x02;
const uint8_t FLAG_GOHOME  = 0x04;
const uint8_t FLAG_ESTOP   = 0x80;

// Joystick center values (from native lib disassembly)
const uint8_t AXIS_CENTER_1 = 0x7F;  // pitch
const uint8_t AXIS_CENTER_2 = 0x7F;  // roll
const uint8_t AXIS_CENTER_3 = 0x80;  // throttle
const uint8_t AXIS_CENTER_4 = 0x80;  // yaw
const uint8_t TRIM_DEFAULT  = 0x20;

// GPS accuracy gate — drone requires ≤ 3 meters for takeoff
const uint8_t GPS_ACCURACY_READY = 0x03;

// GPS coordinates for RTH/FollowMe — SET TO YOUR LOCATION
// Format: degrees * 10^7 as int32  (e.g., 40.7128° → 407128000)
int32_t gpsLatitude  = 0;
int32_t gpsLongitude = 0;
int16_t gpsOrientation = 0;

// ============================================================
// STATE
// ============================================================
WiFiUDP udp;
WiFiClient telnetClient;
bool wifiConnected      = false;
bool heartbeatActive    = false;
bool gpsStreamActive    = false;
bool telnetConnected    = false;
uint8_t currentGpsAccuracy = GPS_ACCURACY_READY;
unsigned long lastHeartbeat = 0;
unsigned long lastGpsStatus = 0;
unsigned long packetsSent   = 0;
unsigned long packetsReceived = 0;
char rxBuf[256];
String serialBuffer = "";

// ============================================================
// PACKET BUILDERS
// ============================================================

uint8_t xorChecksum(const uint8_t* data, int start, int end) {
  uint8_t r = 0;
  for (int i = start; i < end; i++) r ^= data[i];
  return r;
}

void sendHeartbeat() {
  IPAddress localIP = WiFi.localIP();
  uint8_t pkt[5] = { 0x09, localIP[0], localIP[1], localIP[2], localIP[3] };
  udp.beginPacket(DRONE_IP, CMD_PORT);
  udp.write(pkt, 5);
  udp.endPacket();
  packetsSent++;
}

void sendGpsStatus(uint8_t accuracy) {
  uint8_t pkt[19];
  pkt[0] = 0x5A; pkt[1] = 0x55; pkt[2] = 0x0F; pkt[3] = 0x01;
  // Latitude (4 bytes big-endian)
  pkt[4]  = (gpsLatitude >> 24) & 0xFF;
  pkt[5]  = (gpsLatitude >> 16) & 0xFF;
  pkt[6]  = (gpsLatitude >> 8)  & 0xFF;
  pkt[7]  =  gpsLatitude        & 0xFF;
  // Longitude (4 bytes big-endian)
  pkt[8]  = (gpsLongitude >> 24) & 0xFF;
  pkt[9]  = (gpsLongitude >> 16) & 0xFF;
  pkt[10] = (gpsLongitude >> 8)  & 0xFF;
  pkt[11] =  gpsLongitude        & 0xFF;
  // Accuracy (2 bytes big-endian, meters)
  pkt[12] = 0x00;
  pkt[13] = accuracy;
  // Orientation (2 bytes big-endian)
  pkt[14] = (gpsOrientation >> 8) & 0xFF;
  pkt[15] =  gpsOrientation       & 0xFF;
  // Follow mode (off)
  pkt[16] = 0x00; pkt[17] = 0x00;
  // Checksum
  pkt[18] = xorChecksum(pkt, 2, 18);
  udp.beginPacket(DRONE_IP, CMD_PORT);
  udp.write(pkt, 19);
  udp.endPacket();
  packetsSent++;
}

void sendFlightControl(uint8_t flags, uint8_t a1, uint8_t a2, uint8_t a3, uint8_t a4) {
  uint8_t pkt[12] = {
    0x5A, 0x55, 0x08, 0x02,
    flags, a1, a2, a3, a4,
    TRIM_DEFAULT, TRIM_DEFAULT, 0x00
  };
  pkt[11] = xorChecksum(pkt, 2, 11);
  udp.beginPacket(DRONE_IP, CMD_PORT);
  udp.write(pkt, 12);
  udp.endPacket();
  packetsSent++;
}

void sendCmd(uint8_t cmd) {
  udp.beginPacket(DRONE_IP, CMD_PORT);
  udp.write(cmd);
  udp.endPacket();
  packetsSent++;
}

void sendTimeSync() {
  uint8_t pkt[29];
  pkt[0] = 0x26;
  uint32_t vals[7] = { 2026, 3, 1, 0, 12, 0, 0 };
  memcpy(&pkt[1], vals, 28);
  udp.beginPacket(DRONE_IP, CMD_PORT);
  udp.write(pkt, 29);
  udp.endPacket();
  packetsSent++;
}

// ============================================================
// DRONE RESPONSE HANDLER
// ============================================================
void checkDroneResponse() {
  int packetSize = udp.parsePacket();
  while (packetSize > 0) {
    int len = udp.read(rxBuf, sizeof(rxBuf) - 1);
    if (len > 0) {
      packetsReceived++;
      rxBuf[len] = '\0';
      Serial.print(F("[DRONE] "));
      bool isAscii = true;
      for (int i = 0; i < len; i++) {
        if (rxBuf[i] < 0x20 && rxBuf[i] != 0x01 && rxBuf[i] != 0x0A && rxBuf[i] != 0x0D) {
          isAscii = false; break;
        }
      }
      if (isAscii && len < 20) {
        Serial.print(F("\"")); Serial.print(rxBuf); Serial.println(F("\""));
      } else {
        Serial.print(F("HEX[")); Serial.print(len); Serial.print(F("] "));
        for (int i = 0; i < len && i < 32; i++) {
          if ((uint8_t)rxBuf[i] < 0x10) Serial.print('0');
          Serial.print((uint8_t)rxBuf[i], HEX); Serial.print(' ');
        }
        Serial.println();
      }
    }
    packetSize = udp.parsePacket();
  }
}

// ============================================================
// HANDSHAKE (from stock app capture)
// ============================================================
void doHandshake() {
  Serial.println(F("[HANDSHAKE] Starting..."));
  sendCmd(0x0F); delay(50); checkDroneResponse();
  sendCmd(0x28); sendCmd(0x42); sendCmd(0x47); sendCmd(0x2C);
  delay(50); checkDroneResponse();
  sendTimeSync(); delay(50); checkDroneResponse();
  sendCmd(0x27); delay(50); checkDroneResponse();
  delay(300);
  sendCmd(0x28); sendCmd(0x42); sendCmd(0x47); sendCmd(0x2C);
  delay(50); checkDroneResponse();
  Serial.println(F("[HANDSHAKE] Complete."));
}

// ============================================================
// FLIGHT COMMANDS
// ============================================================
void sendCommandBurst(uint8_t flags, int count, const char* name) {
  Serial.print(F("[FLIGHT] ")); Serial.print(name);
  Serial.print(F(" burst (")); Serial.print(count); Serial.println(F(" pkts)..."));
  for (int i = 0; i < count; i++) {
    sendFlightControl(flags, AXIS_CENTER_1, AXIS_CENTER_2, AXIS_CENTER_3, AXIS_CENTER_4);
    if (i % 5 == 0) sendGpsStatus(currentGpsAccuracy);
    delay(CONTROL_INTERVAL_MS);
    checkDroneResponse();
  }
  for (int i = 0; i < 20; i++) {
    sendFlightControl(0x00, AXIS_CENTER_1, AXIS_CENTER_2, AXIS_CENTER_3, AXIS_CENTER_4);
    delay(CONTROL_INTERVAL_MS);
  }
  Serial.print(name); Serial.println(F(" complete."));
}

void runAutoSequence() {
  Serial.println(F("\n========================================"));
  Serial.println(F(" AUTONOMOUS TAKEOFF"));
  Serial.println(F(" RC must arm motors first!"));
  Serial.println(F("========================================\n"));

  Serial.println(F("[AUTO] Phase 1: Handshake..."));
  doHandshake(); delay(200);

  heartbeatActive = true;
  gpsStreamActive = true;
  currentGpsAccuracy = GPS_ACCURACY_READY;

  Serial.println(F("[AUTO] Phase 2: GPS stream (accuracy=3m)..."));
  unsigned long phaseStart = millis();
  while (millis() - phaseStart < PRE_FLIGHT_DURATION_MS) {
    if (millis() - lastGpsStatus >= GPS_STATUS_INTERVAL_MS) {
      sendGpsStatus(currentGpsAccuracy); lastGpsStatus = millis();
    }
    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
      sendHeartbeat(); lastHeartbeat = millis();
    }
    checkDroneResponse(); delay(10);
  }

  Serial.println(F("[AUTO] Phase 3: TAKEOFF!"));
  sendCommandBurst(FLAG_TAKEOFF, TAKEOFF_BURST_COUNT, "TAKEOFF");

  Serial.println(F("[AUTO] Hovering. Type LAND or ESTOP."));
}

// ============================================================
// TELNET BRIDGE (bypasses MAC ban via Arduino's MAC)
// ============================================================
void telnetOpen() {
  if (telnetConnected) { Serial.println(F("[TELNET] Already connected.")); return; }
  Serial.println(F("[TELNET] Connecting to 172.16.10.1:23..."));
  if (telnetClient.connect(DRONE_IP, 23)) {
    telnetConnected = true;
    Serial.println(F("[TELNET] Connected!"));
    unsigned long start = millis();
    while (millis() - start < 5000) {
      while (telnetClient.available()) {
        uint8_t b = telnetClient.read();
        if (b == 0xFF && telnetClient.available() >= 2) {
          uint8_t cmd = telnetClient.read(); uint8_t opt = telnetClient.read();
          if (cmd == 0xFD) { uint8_t r[] = {0xFF, 0xFC, opt}; telnetClient.write(r, 3); }
          else if (cmd == 0xFB) { uint8_t r[] = {0xFF, 0xFE, opt}; telnetClient.write(r, 3); }
          continue;
        }
        if (b != 0x00) Serial.write(b);
      }
      delay(10);
    }
    Serial.println(F("\n[TELNET] Use TCMD <command> to send finsh commands."));
  } else {
    Serial.println(F("[TELNET] Connection failed!"));
  }
}

void telnetSendCmd(const char* cmd) {
  if (!telnetConnected || !telnetClient.connected()) {
    Serial.println(F("[TELNET] Not connected. Run TOPEN first."));
    telnetConnected = false; return;
  }
  telnetClient.print(cmd); telnetClient.print("\r\n");
  Serial.print(F("[TELNET] >>> ")); Serial.println(cmd);
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < 15000) {
    while (telnetClient.available()) {
      uint8_t b = telnetClient.read();
      if (b == 0xFF && telnetClient.available() >= 2) {
        uint8_t c = telnetClient.read(); uint8_t o = telnetClient.read();
        if (c == 0xFD) { uint8_t r[] = {0xFF, 0xFC, o}; telnetClient.write(r, 3); }
        else if (c == 0xFB) { uint8_t r[] = {0xFF, 0xFE, o}; telnetClient.write(r, 3); }
        continue;
      }
      if (b == 0x00) continue;
      Serial.write(b);
      resp += (char)b;
      if (resp.endsWith("finsh />")) { Serial.println(); return; }
      if (resp.length() > 512) resp = resp.substring(resp.length() - 32);
    }
    delay(5);
  }
  Serial.println(F("\n[TELNET] <<< timeout"));
}

void telnetClose() {
  if (telnetClient.connected()) telnetClient.stop();
  telnetConnected = false;
  Serial.println(F("[TELNET] Closed."));
}

// ============================================================
// SERIAL COMMAND PARSER
// ============================================================
void processSerialCommand(String rawCmd) {
  rawCmd.trim();
  String cmd = rawCmd;
  cmd.toUpperCase();

  if (cmd.startsWith("TCMD ")) {
    telnetSendCmd(rawCmd.substring(5).c_str());
    return;
  }

  if (cmd == "STATUS") {
    Serial.println(F("\n=== STATUS ==="));
    Serial.print(F("WiFi: ")); Serial.println(wifiConnected ? "CONNECTED" : "DISCONNECTED");
    if (wifiConnected) {
      Serial.print(F("  SSID: ")); Serial.println(WiFi.SSID());
      Serial.print(F("  IP: ")); Serial.println(WiFi.localIP());
      Serial.print(F("  RSSI: ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
    }
    Serial.print(F("Heartbeat: ")); Serial.println(heartbeatActive ? "ON" : "OFF");
    Serial.print(F("GPS stream: ")); Serial.println(gpsStreamActive ? "ON" : "OFF");
    Serial.print(F("GPS accuracy: ")); Serial.print(currentGpsAccuracy); Serial.println(F("m"));
    Serial.print(F("TX: ")); Serial.print(packetsSent);
    Serial.print(F("  RX: ")); Serial.println(packetsReceived);
    Serial.print(F("Telnet: ")); Serial.println(telnetConnected ? "CONNECTED" : "CLOSED");
    Serial.println(F("==============\n"));
  } else if (cmd == "HANDSHAKE") {
    if (!wifiConnected) { Serial.println(F("ERROR: No WiFi")); return; }
    doHandshake();
  } else if (cmd == "HEARTBEAT") {
    heartbeatActive = !heartbeatActive;
    Serial.print(F("Heartbeat: ")); Serial.println(heartbeatActive ? "ON" : "OFF");
  } else if (cmd == "GPSSTREAM") {
    gpsStreamActive = !gpsStreamActive;
    Serial.print(F("GPS stream: ")); Serial.println(gpsStreamActive ? "ON" : "OFF");
  } else if (cmd.startsWith("GPS ")) {
    currentGpsAccuracy = (uint8_t)cmd.substring(4).toInt();
    Serial.print(F("GPS accuracy: ")); Serial.print(currentGpsAccuracy); Serial.println(F("m"));
  } else if (cmd == "TAKEOFF") {
    if (!wifiConnected) { Serial.println(F("ERROR: No WiFi")); return; }
    sendCommandBurst(FLAG_TAKEOFF, TAKEOFF_BURST_COUNT, "TAKEOFF");
  } else if (cmd == "LAND") {
    if (!wifiConnected) { Serial.println(F("ERROR: No WiFi")); return; }
    sendCommandBurst(FLAG_LAND, TAKEOFF_BURST_COUNT, "LAND");
  } else if (cmd == "GOHOME") {
    if (!wifiConnected) { Serial.println(F("ERROR: No WiFi")); return; }
    sendCommandBurst(FLAG_GOHOME, TAKEOFF_BURST_COUNT, "GOHOME");
  } else if (cmd == "ESTOP") {
    if (!wifiConnected) { Serial.println(F("ERROR: No WiFi")); return; }
    sendCommandBurst(FLAG_ESTOP, 100, "ESTOP");
    heartbeatActive = false; gpsStreamActive = false;
    Serial.println(F("*** ALL STOPPED ***"));
  } else if (cmd == "AUTO") {
    if (!wifiConnected) { Serial.println(F("ERROR: No WiFi")); return; }
    runAutoSequence();
  } else if (cmd == "TOPEN") {
    if (!wifiConnected) { Serial.println(F("ERROR: No WiFi")); return; }
    telnetOpen();
  } else if (cmd == "TCLOSE") {
    telnetClose();
  } else if (cmd == "HELP") {
    Serial.println(F("\nCommands:"));
    Serial.println(F("  STATUS    - Connection state"));
    Serial.println(F("  HANDSHAKE - Run handshake"));
    Serial.println(F("  HEARTBEAT - Toggle heartbeat (1Hz)"));
    Serial.println(F("  GPSSTREAM - Toggle GPS stream (5Hz)"));
    Serial.println(F("  GPS <N>   - Set GPS accuracy (3=ready)"));
    Serial.println(F("  TAKEOFF   - Takeoff burst"));
    Serial.println(F("  LAND      - Land burst"));
    Serial.println(F("  GOHOME    - Return to home"));
    Serial.println(F("  ESTOP     - Emergency stop"));
    Serial.println(F("  AUTO      - Full autonomous takeoff"));
    Serial.println(F("  TOPEN     - Open telnet (TCP 23)"));
    Serial.println(F("  TCMD <c>  - Send finsh command"));
    Serial.println(F("  TCLOSE    - Close telnet\n"));
  } else {
    Serial.print(F("Unknown: ")); Serial.println(cmd);
    Serial.println(F("Type HELP for commands."));
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println(F("\n========================================"));
  Serial.println(F("  H120D Drone Controller"));
  Serial.println(F("  Arduino Nano 33 IoT"));
  Serial.println(F("========================================\n"));

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("FATAL: WiFiNINA module not found!"));
    while (true);
  }
  Serial.print(F("WiFiNINA firmware: ")); Serial.println(WiFi.firmwareVersion());

  Serial.print(F("Connecting to ")); Serial.print(DRONE_SSID); Serial.print(F("..."));
  int status = WL_IDLE_STATUS;
  int attempts = 0;
  while (status != WL_CONNECTED && attempts < 20) {
    status = WiFi.begin(DRONE_SSID);
    attempts++;
    Serial.print(F("."));
    delay(1000);
  }

  if (status == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println(F(" OK!"));
    Serial.print(F("  IP: ")); Serial.println(WiFi.localIP());
    Serial.print(F("  RSSI: ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
    udp.begin(CMD_PORT);
  } else {
    Serial.println(F(" FAILED (is drone powered on?)"));
  }

  Serial.println(F("\nType HELP for commands, AUTO for full sequence.\n"));
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // Reconnect WiFi if dropped
  if (!wifiConnected && WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (now - lastReconnect > 5000) {
      WiFi.begin(DRONE_SSID);
      lastReconnect = now;
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        udp.begin(CMD_PORT);
        Serial.print(F("[WiFi] Reconnected: ")); Serial.println(WiFi.localIP());
      }
    }
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  // Heartbeat at 1Hz
  if (heartbeatActive && wifiConnected && now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat(); lastHeartbeat = now;
  }

  // GPS at 5Hz
  if (gpsStreamActive && wifiConnected && now - lastGpsStatus >= GPS_STATUS_INTERVAL_MS) {
    sendGpsStatus(currentGpsAccuracy); lastGpsStatus = now;
  }

  // Check drone responses
  if (wifiConnected) checkDroneResponse();

  // Forward telnet data to serial
  if (telnetConnected && telnetClient.available()) {
    while (telnetClient.available()) {
      uint8_t b = telnetClient.read();
      if (b == 0xFF && telnetClient.available() >= 2) {
        uint8_t c = telnetClient.read(); uint8_t o = telnetClient.read();
        if (c == 0xFD) { uint8_t r[] = {0xFF, 0xFC, o}; telnetClient.write(r, 3); }
        else if (c == 0xFB) { uint8_t r[] = {0xFF, 0xFE, o}; telnetClient.write(r, 3); }
        continue;
      }
      if (b != 0x00) Serial.write(b);
    }
  }
  if (telnetConnected && !telnetClient.connected()) {
    telnetConnected = false;
    Serial.println(F("\n[TELNET] Disconnected."));
  }

  // Process serial input
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        processSerialCommand(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }

  delay(5);
}
