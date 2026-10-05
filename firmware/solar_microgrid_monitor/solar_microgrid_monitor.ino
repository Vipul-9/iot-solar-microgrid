#include <Wire.h>
#include <Adafruit_INA219.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ═════════════════════════════════════════════════════════════════════════
// CONFIGURATION - UPDATE THESE VALUES
// ═════════════════════════════════════════════════════════════════════════
// ===== WiFi Configuration =====
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ===== Firebase Configuration =====
const char* firebaseHost = "YOUR_PROJECT-default-rtdb.firebaseio.com";   // no https://
const char* firebaseAuth = "YOUR_DATABASE_SECRET";

// ===== System Configuration =====
#define SOLAR_PANEL_MAX_VOLTAGE 7.0   // Maximum solar panel voltage
#define BATTERY_CAPACITY_MAH 2000     // Battery capacity in mAh
#define CHARGING_CURRENT_MA 1000      // CN3791 charging current setting

// ═════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ═════════════════════════════════════════════════════════════════════════
// ===== OLED Display Configuration =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===== INA219 Sensors =====
Adafruit_INA219 inaSolar(0x40);    // Solar panel sensor
Adafruit_INA219 inaBattery(0x41);  // Battery discharge sensor

// ===== Web Server =====
WebServer server(80);

// ═════════════════════════════════════════════════════════════════════════
// DATA STRUCTURES
// ═════════════════════════════════════════════════════════════════════════
// ===== Real-time Sensor Data =====
struct SensorData {
  // Solar Panel Measurements
  float solarVoltage = 0;
  float solarCurrent = 0;
  float solarPower = 0;
  float solarEnergyTotal = 0;  // Wh
  float peakSolarPower = 0;

  // Battery Measurements
  float battVoltage = 0;
  float battCurrent = 0;
  float battPower = 0;
  float battSOC = 0;       // State of Charge (%)
  float battSOH = 100.0;   // State of Health (%)

  // MPPT Performance
  float mpptEfficiency = 0;
  float mpptVoltage = 0;   // Estimated MPP voltage
  bool mpptTracking = false;

  // System Metrics
  float battEfficiency = 0;
  float sysEfficiency = 0;
  float totalAh = 0;
  float cycleCount = 0;

  // Charging Status
  bool isCharging = false;
  float chargingPower = 0;
  float chargeTimeRemaining = 0;  // minutes
} currentData;

// ===== Battery Health Tracking =====
struct BatteryHealth {
  float voltageMin = 4.2;
  float voltageMax = 3.0;
  float lastSOC = -1;              // -1 until the first reading
  float totalChargeAh = 0;
  float totalDischargeAh = 0;
  unsigned long lastUpdate = 0;
  unsigned long cycleStartTime = 0;
  int fullCharges = 0;
} battHealth;

// ===== MPPT Tracking =====
struct MPPTData {
  float lastSolarVoltage = 0;
  float lastSolarPower = 0;
  float maxPowerVoltage = 0;
  float maxPower = 0;
  unsigned long lastScanTime = 0;
  float efficiencyHistory[10] = {0};
  int historyIndex = 0;
} mpptData;

// ═════════════════════════════════════════════════════════════════════════
// TIMING VARIABLES
// ═════════════════════════════════════════════════════════════════════════
unsigned long lastFirebaseUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastMPPTUpdate = 0;
unsigned long lastSerialUpdate = 0;
unsigned long dataPointCount = 0;
unsigned long systemStartTime = 0;

const unsigned long firebaseInterval = 5000;  // 5 seconds
const unsigned long displayInterval = 1000;   // 1 second
const unsigned long mpptInterval = 2000;      // 2 seconds
const unsigned long serialInterval = 2000;    // 2 seconds

// ═════════════════════════════════════════════════════════════════════════
// HTML WEB DASHBOARD
// ═════════════════════════════════════════════════════════════════════════
const char HTML_PAGE[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Solar Microgrid - CN3791 MPPT</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .header {
      background: white;
      border-radius: 15px;
      padding: 30px;
      margin-bottom: 30px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.2);
      text-align: center;
    }
    .logo {
      width: 100px;
      height: 100px;
      border-radius: 50%;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      display: flex;
      align-items: center;
      justify-content: center;
      margin: 0 auto 15px;
      color: white;
      font-size: 40px;
      font-weight: bold;
    }
    .course-title {
      font-size: 16px;
      color: #666;
      margin-bottom: 8px;
      text-transform: uppercase;
      letter-spacing: 2px;
    }
    .project-title {
      font-size: 28px;
      color: #333;
      font-weight: bold;
      margin-bottom: 8px;
    }
    .subtitle {
      font-size: 14px;
      color: #888;
      margin-bottom: 10px;
    }
    .mppt-badge {
      display: inline-block;
      background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
      color: white;
      padding: 5px 15px;
      border-radius: 20px;
      font-size: 12px;
      font-weight: bold;
      margin-top: 10px;
    }
    .status {
      margin-top: 10px;
      font-weight: bold;
    }
    .status-dot {
      display: inline-block;
      width: 10px;
      height: 10px;
      border-radius: 50%;
      margin-right: 8px;
      animation: pulse 2s infinite;
    }
    .status-online { background: #4CAF50; color: #4CAF50; }
    .status-charging { background: #FF9800; color: #FF9800; }
    .status-discharging { background: #2196F3; color: #2196F3; }
    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.5; }
    }
    .container {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 20px;
      margin-bottom: 20px;
    }
    .card {
      background: white;
      border-radius: 15px;
      padding: 20px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.2);
    }
    .card-header {
      display: flex;
      align-items: center;
      margin-bottom: 15px;
      justify-content: space-between;
    }
    .card-header-left {
      display: flex;
      align-items: center;
    }
    .icon { font-size: 28px; margin-right: 12px; }
    .card-title { font-size: 16px; color: #333; font-weight: bold; }
    .badge {
      background: #4CAF50;
      color: white;
      padding: 3px 10px;
      border-radius: 12px;
      font-size: 11px;
      font-weight: bold;
    }
    .badge-mppt { background: #FF9800; }
    .metric-value {
      font-size: 32px;
      font-weight: bold;
      color: #667eea;
      margin: 10px 0;
    }
    .metric-unit { font-size: 18px; color: #888; margin-left: 5px; }
    .grid-2 {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 10px;
      margin-top: 15px;
    }
    .small-metric {
      padding: 12px;
      background: #f5f5f5;
      border-radius: 8px;
    }
    .small-label { font-size: 11px; color: #888; margin-bottom: 5px; }
    .small-value { font-size: 18px; font-weight: bold; color: #333; }
    .progress-bar {
      width: 100%;
      height: 25px;
      background: #e0e0e0;
      border-radius: 12px;
      overflow: hidden;
      margin-top: 10px;
    }
    .progress-fill {
      height: 100%;
      background: linear-gradient(90deg, #4CAF50, #8BC34A);
      transition: width 0.5s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      color: white;
      font-weight: bold;
      font-size: 14px;
    }
    .progress-fill-charging {
      background: linear-gradient(90deg, #FF9800, #FFC107);
    }
    .info-text {
      font-size: 12px;
      color: #666;
      margin-top: 10px;
      font-style: italic;
    }
  </style>
</head>
<body>
  <div class="header">
    <div class="logo">🎓</div>
    <div class="course-title">MPMC Course Based Project</div>
    <div class="project-title">Solar Microgrid Monitoring System</div>
    <div class="subtitle">with CN3791 MPPT Solar Charge Controller</div>
    <div class="mppt-badge">⚡ MPPT ENABLED</div>
    <div class="status">
      <span class="status-dot status-online"></span>
      <span id="statusText">System Online</span>
    </div>
  </div>

  <div class="container">
    <!-- Solar Panel Card -->
    <div class="card">
      <div class="card-header">
        <div class="card-header-left">
          <div class="icon">☀</div>
          <div class="card-title">Solar Panel</div>
        </div>
        <span class="badge badge-mppt" id="mpptBadge">MPPT</span>
      </div>
      <div class="metric-value" id="solarPower">0.00<span class="metric-unit">W</span></div>
      <div class="grid-2">
        <div class="small-metric">
          <div class="small-label">Voltage</div>
          <div class="small-value" id="solarVoltage">0.00 V</div>
        </div>
        <div class="small-metric">
          <div class="small-label">Current</div>
          <div class="small-value" id="solarCurrent">0 mA</div>
        </div>
        <div class="small-metric">
          <div class="small-label">Peak Power</div>
          <div class="small-value" id="peakSolar">0.00 W</div>
        </div>
        <div class="small-metric">
          <div class="small-label">MPPT Eff.</div>
          <div class="small-value" id="mpptEff">0%</div>
        </div>
      </div>
      <div class="info-text">CN3791 MPPT Tracking Active</div>
    </div>

    <!-- Battery Status Card -->
    <div class="card">
      <div class="card-header">
        <div class="card-header-left">
          <div class="icon">🔋</div>
          <div class="card-title">Battery Status</div>
        </div>
        <span class="badge" id="chargeBadge">Ready</span>
      </div>
      <div class="metric-value" id="batterySOC">0<span class="metric-unit">%</span></div>
      <div class="progress-bar">
        <div class="progress-fill" id="batteryBar" style="width: 0%;">0%</div>
      </div>
      <div class="grid-2">
        <div class="small-metric">
          <div class="small-label">Voltage</div>
          <div class="small-value" id="battVoltage">0.00 V</div>
        </div>
        <div class="small-metric">
          <div class="small-label">Current</div>
          <div class="small-value" id="battCurrent">0 mA</div>
        </div>
        <div class="small-metric">
          <div class="small-label">Power</div>
          <div class="small-value" id="battPower">0.00 W</div>
        </div>
        <div class="small-metric">
          <div class="small-label">SOH</div>
          <div class="small-value" id="battSOH">100%</div>
        </div>
      </div>
    </div>

    <!-- System Efficiency Card -->
    <div class="card">
      <div class="card-header">
        <div class="card-header-left">
          <div class="icon">📊</div>
          <div class="card-title">System Efficiency</div>
        </div>
      </div>
      <div class="metric-value" id="sysEfficiency">0<span class="metric-unit">%</span></div>
      <div class="grid-2">
        <div class="small-metric">
          <div class="small-label">Battery Eff.</div>
          <div class="small-value" id="battEfficiency">0%</div>
        </div>
        <div class="small-metric">
          <div class="small-label">Energy Harvested</div>
          <div class="small-value" id="energy">0 Wh</div>
        </div>
      </div>
      <div class="info-text">Includes MPPT conversion efficiency</div>
    </div>

    <!-- Battery Health Card -->
    <div class="card">
      <div class="card-header">
        <div class="card-header-left">
          <div class="icon">💚</div>
          <div class="card-title">Battery Health</div>
        </div>
      </div>
      <div class="metric-value" id="cycleCount">0<span class="metric-unit">cycles</span></div>
      <div class="grid-2">
        <div class="small-metric">
          <div class="small-label">Total Ah</div>
          <div class="small-value" id="totalAh">0 Ah</div>
        </div>
        <div class="small-metric">
          <div class="small-label">Health</div>
          <div class="small-value" id="health" style="color: #4CAF50;">Good</div>
        </div>
      </div>
    </div>
  </div>

  <script>
    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          // Solar Panel
          document.getElementById('solarPower').innerHTML = data.solarPower.toFixed(2) + '<span class="metric-unit">W</span>';
          document.getElementById('solarVoltage').textContent = data.solarVoltage.toFixed(2) + ' V';
          document.getElementById('solarCurrent').textContent = data.solarCurrent.toFixed(0) + ' mA';
          document.getElementById('peakSolar').textContent = data.peakSolar.toFixed(2) + ' W';
          document.getElementById('mpptEff').textContent = data.mpptEfficiency.toFixed(1) + '%';

          // MPPT Badge
          const mpptBadge = document.getElementById('mpptBadge');
          if (data.mpptTracking) {
            mpptBadge.textContent = 'MPPT ⚡';
            mpptBadge.style.background = '#4CAF50';
          } else {
            mpptBadge.textContent = 'MPPT';
            mpptBadge.style.background = '#999';
          }

          // Battery
          const soc = data.battSOC.toFixed(0);
          document.getElementById('batterySOC').innerHTML = soc + '<span class="metric-unit">%</span>';
          const bar = document.getElementById('batteryBar');
          bar.style.width = soc + '%';
          bar.textContent = soc + '%';

          document.getElementById('battVoltage').textContent = data.battVoltage.toFixed(2) + ' V';
          document.getElementById('battCurrent').textContent = Math.abs(data.battCurrent).toFixed(0) + ' mA';
          document.getElementById('battPower').textContent = Math.abs(data.battPower).toFixed(2) + ' W';
          document.getElementById('battSOH').textContent = data.battSOH.toFixed(0) + '%';

          // Charging Status
          const chargeBadge = document.getElementById('chargeBadge');
          const statusDot = document.querySelector('.status-dot');
          const statusText = document.getElementById('statusText');
          if (data.isCharging) {
            chargeBadge.textContent = '⚡ Charging';
            chargeBadge.style.background = '#FF9800';
            bar.classList.add('progress-fill-charging');
            statusDot.className = 'status-dot status-charging';
            statusText.textContent = 'Charging from Solar';
          } else if (data.battCurrent < 0) {
            chargeBadge.textContent = '📤 Discharging';
            chargeBadge.style.background = '#2196F3';
            bar.classList.remove('progress-fill-charging');
            statusDot.className = 'status-dot status-discharging';
            statusText.textContent = 'Battery Discharging';
          } else {
            chargeBadge.textContent = 'Ready';
            chargeBadge.style.background = '#4CAF50';
            bar.classList.remove('progress-fill-charging');
            statusDot.className = 'status-dot status-online';
            statusText.textContent = 'System Online';
          }

          // Efficiency
          document.getElementById('sysEfficiency').innerHTML = data.sysEfficiency.toFixed(1) + '<span class="metric-unit">%</span>';
          document.getElementById('battEfficiency').textContent = data.battEfficiency.toFixed(1) + '%';
          document.getElementById('energy').textContent = data.totalEnergy.toFixed(2) + ' Wh';

          // Health
          document.getElementById('cycleCount').innerHTML = data.cycleCount.toFixed(1) + '<span class="metric-unit">cycles</span>';
          document.getElementById('totalAh').textContent = data.totalAh.toFixed(2) + ' Ah';

          const health = data.cycleCount < 100 ? 'Excellent' : data.cycleCount < 300 ? 'Good' : 'Fair';
          const healthColor = data.cycleCount < 100 ? '#4CAF50' : data.cycleCount < 300 ? '#8BC34A' : '#FF9800';
          document.getElementById('health').textContent = health;
          document.getElementById('health').style.color = healthColor;
        })
        .catch(error => {
          console.error('Error:', error);
          document.getElementById('statusText').textContent = 'Connection Error';
        });
    }

    updateData();
    setInterval(updateData, 2000);
  </script>
</body>
</html>
)=====";

// ═════════════════════════════════════════════════════════════════════════
// FUNCTION DECLARATIONS
// ═════════════════════════════════════════════════════════════════════════
void setupWiFi();
void setupSensors();
void setupWebServer();
void readSensors();
void updateBatteryHealth();
void updateMPPTTracking();
void updateOLED();
void sendToFirebase();
void handleRoot();
void handleData();
float calculateSOC(float voltage);
float calculateSOH();
float calculateBatteryEfficiency(float current);
float calculateSystemEfficiency();
void printSerialData();

// ═════════════════════════════════════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n");
  Serial.println("═══════════════════════════════════════════════════════");
  Serial.println("⚡ Solar Microgrid Monitor with CN3791 MPPT Controller");
  Serial.println("   MPMC Course Based Project");
  Serial.println("═══════════════════════════════════════════════════════\n");

  systemStartTime = millis();

  // Initialize I2C
  Wire.begin(21, 22);  // SDA=21, SCL=22
  Serial.println("✅ I2C Bus initialized (GPIO 21=SDA, 22=SCL)");

  // Setup OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("❌ OLED init failed! Check wiring.");
  } else {
    Serial.println("✅ OLED Display initialized");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(" MICROGRID MONITOR");
    display.println("   CN3791 MPPT");
    display.println();
    display.println("  Initializing...");
    display.display();
  }

  // Setup Sensors
  setupSensors();

  // Setup WiFi
  setupWiFi();

  // Setup Web Server
  setupWebServer();

  Serial.println("\n✅ System Ready!");
  Serial.println("═══════════════════════════════════════════════════════\n");

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("  SYSTEM READY!");
  display.println();
  display.println(" Dashboard:");
  display.print(" ");
  display.println(WiFi.localIP());
  display.println();
  display.println(" CN3791 MPPT: ON");
  display.display();
  delay(3000);
}

// ═════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ═════════════════════════════════════════════════════════════════════════
void loop() {
  // Handle web server requests
  server.handleClient();

  // Read sensors
  readSensors();

  // Update battery health metrics
  updateBatteryHealth();

  // Update MPPT tracking analysis
  if (millis() - lastMPPTUpdate >= mpptInterval) {
    updateMPPTTracking();
    lastMPPTUpdate = millis();
  }

  // Update OLED display
  if (millis() - lastDisplayUpdate >= displayInterval) {
    updateOLED();
    lastDisplayUpdate = millis();
  }

  // Send data to Firebase
  if (millis() - lastFirebaseUpdate >= firebaseInterval) {
    sendToFirebase();
    lastFirebaseUpdate = millis();
    dataPointCount++;
  }

  // Print to Serial
  if (millis() - lastSerialUpdate >= serialInterval) {
    printSerialData();
    lastSerialUpdate = millis();
  }

  delay(500);
}

// ═════════════════════════════════════════════════════════════════════════
// SENSOR INITIALIZATION
// ═════════════════════════════════════════════════════════════════════════
void setupSensors() {
  Serial.print("Initializing INA219 sensors... ");

  if (!inaSolar.begin()) {
    Serial.println("\n❌ INA219 #1 (Solar - 0x40) not found!");
    Serial.println("   Check:");
    Serial.println("   - I2C wiring (SDA=GPIO21, SCL=GPIO22)");
    Serial.println("   - VCC=3.3V, GND connected");
    Serial.println("   - Address is 0x40 (default)");
    while (1) delay(10);
  }

  if (!inaBattery.begin()) {
    Serial.println("\n❌ INA219 #2 (Battery - 0x41) not found!");
    Serial.println("   Check:");
    Serial.println("   - I2C wiring (SDA=GPIO21, SCL=GPIO22)");
    Serial.println("   - VCC=3.3V, GND connected");
    Serial.println("   - Address changed to 0x41 (A0 bridged)");
    while (1) delay(10);
  }

  Serial.println("✅ Both INA219 sensors initialized");
  Serial.println("   - INA219 #1 (0x40): Solar Panel Monitor");
  Serial.println("   - INA219 #2 (0x41): Battery Discharge Monitor");
}

// ═════════════════════════════════════════════════════════════════════════
// WiFi SETUP
// ═════════════════════════════════════════════════════════════════════════
void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(" Connecting to");
  display.print(" ");
  display.println(ssid);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("   IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("   Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n❌ WiFi Connection Failed!");
    Serial.println("   Check SSID and password in code");
    Serial.println("   System will continue without WiFi");
  }
}

// ═════════════════════════════════════════════════════════════════════════
// WEB SERVER SETUP
// ═════════════════════════════════════════════════════════════════════════
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("✅ Web server started");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("   Access dashboard at: http://");
    Serial.println(WiFi.localIP());
  }
}

// ═════════════════════════════════════════════════════════════════════════
// SENSOR READING
// ═════════════════════════════════════════════════════════════════════════
void readSensors() {
  // ===== Solar Panel Readings (INA219 #1) =====
  currentData.solarVoltage = inaSolar.getBusVoltage_V();
  currentData.solarCurrent = inaSolar.getCurrent_mA();
  currentData.solarPower = (currentData.solarVoltage * currentData.solarCurrent) / 1000.0;

  // Validate solar readings (remove negative values)
  if (currentData.solarCurrent < 0) currentData.solarCurrent = 0;
  if (currentData.solarPower < 0) currentData.solarPower = 0;

  // ===== Battery Readings (INA219 #2) =====
  currentData.battVoltage = inaBattery.getBusVoltage_V();
  currentData.battCurrent = inaBattery.getCurrent_mA();
  currentData.battPower = (currentData.battVoltage * currentData.battCurrent) / 1000.0;

  // ===== Battery State Calculations =====
  currentData.battSOC = calculateSOC(currentData.battVoltage);
  currentData.battSOH = calculateSOH();
  // INA219 #2 sits between battery and load, so positive current = discharge
  currentData.battEfficiency = calculateBatteryEfficiency(-currentData.battCurrent);

  // ===== Charging Detection =====
  // CN3791 is charging if solar power > 0 and battery not full
  currentData.isCharging = (currentData.solarPower > 0.1 && currentData.battSOC < 99.0);

  if (currentData.isCharging) {
    // Estimate charging power (solar power minus losses)
    currentData.chargingPower = currentData.solarPower * 0.95;  // 95% efficiency

    // Estimate time to full charge from the battery-side charge current
    float remainingCapacity = (100 - currentData.battSOC) / 100.0 * BATTERY_CAPACITY_MAH;  // mAh
    float chargeCurrent = (currentData.battVoltage > 3.0)
                            ? currentData.chargingPower / currentData.battVoltage * 1000.0  // mA
                            : 0;
    if (chargeCurrent > CHARGING_CURRENT_MA) chargeCurrent = CHARGING_CURRENT_MA;            // CN3791 limit
    currentData.chargeTimeRemaining = (chargeCurrent > 0) ? remainingCapacity / chargeCurrent * 60 : 0;  // minutes
  } else {
    currentData.chargingPower = 0;
    currentData.chargeTimeRemaining = 0;
  }

  // ===== System Efficiency =====
  currentData.sysEfficiency = calculateSystemEfficiency();

  // ===== Update Peak Values =====
  if (currentData.solarPower > currentData.peakSolarPower) {
    currentData.peakSolarPower = currentData.solarPower;
  }

  // ===== Energy Integration =====
  // Integrate solar energy (Wh = W * hours)
  static unsigned long lastEnergyUpdate = 0;
  unsigned long now = millis();
  if (lastEnergyUpdate > 0) {
    float hours = (now - lastEnergyUpdate) / 3600000.0;
    currentData.solarEnergyTotal += currentData.solarPower * hours;
  }
  lastEnergyUpdate = now;
}

// ═════════════════════════════════════════════════════════════════════════
// BATTERY SOC CALCULATION (Voltage-based with LiPo curve)
// ═════════════════════════════════════════════════════════════════════════
float calculateSOC(float voltage) {
  // LiPo discharge curve for single cell
  if (voltage >= 4.2)  return 100.0;
  if (voltage >= 4.15) return 95.0 + (voltage - 4.15) * 100;
  if (voltage >= 4.11) return 90.0 + (voltage - 4.11) * 125;
  if (voltage >= 4.08) return 85.0 + (voltage - 4.08) * 167;
  if (voltage >= 4.02) return 80.0 + (voltage - 4.02) * 83;
  if (voltage >= 3.98) return 75.0 + (voltage - 3.98) * 125;
  if (voltage >= 3.95) return 70.0 + (voltage - 3.95) * 167;
  if (voltage >= 3.91) return 65.0 + (voltage - 3.91) * 125;
  if (voltage >= 3.87) return 60.0 + (voltage - 3.87) * 125;
  if (voltage >= 3.85) return 55.0 + (voltage - 3.85) * 250;
  if (voltage >= 3.84) return 50.0 + (voltage - 3.84) * 500;
  if (voltage >= 3.82) return 42.0 + (voltage - 3.82) * 400;
  if (voltage >= 3.80) return 35.0 + (voltage - 3.80) * 350;
  if (voltage >= 3.79) return 30.0 + (voltage - 3.79) * 500;
  if (voltage >= 3.77) return 25.0 + (voltage - 3.77) * 250;
  if (voltage >= 3.75) return 20.0 + (voltage - 3.75) * 250;
  if (voltage >= 3.73) return 16.0 + (voltage - 3.73) * 200;
  if (voltage >= 3.71) return 13.0 + (voltage - 3.71) * 150;
  if (voltage >= 3.69) return 10.0 + (voltage - 3.69) * 150;
  if (voltage >= 3.61) return 5.0 + (voltage - 3.61) * 62.5;
  if (voltage >= 3.27) return 1.0 + (voltage - 3.27) * 11.76;
  if (voltage >= 3.0)  return (voltage - 3.0) * 3.7;
  return 0.0;
}

// ═════════════════════════════════════════════════════════════════════════
// BATTERY STATE OF HEALTH CALCULATION
// ═════════════════════════════════════════════════════════════════════════
float calculateSOH() {
  // SOH based on cycle count and capacity fade
  float cycleDegradation = (currentData.cycleCount / 500.0) * 20.0;  // 20% loss at 500 cycles

  // Voltage-based health indicator
  float voltageHealth = 100.0;
  if (battHealth.voltageMax < 4.15) {
    voltageHealth = 90.0;  // Capacity fade detected
  }

  // Combined SOH
  float soh = 100.0 - cycleDegradation;
  soh = (soh + voltageHealth) / 2.0;

  return constrain(soh, 0, 100);
}

// ═════════════════════════════════════════════════════════════════════════
// BATTERY EFFICIENCY CALCULATION
// ═════════════════════════════════════════════════════════════════════════
float calculateBatteryEfficiency(float current) {
  // Coulombic efficiency estimation based on current
  if (current > 0) {  // Charging
    // Higher current = slightly lower efficiency
    return constrain(95.0 + (1000 - abs(current)) / 500.0, 90.0, 98.0);
  } else {            // Discharging
    // Discharge efficiency usually higher
    return constrain(98.0 - abs(current) / 2000.0, 95.0, 99.0);
  }
}

// ═════════════════════════════════════════════════════════════════════════
// SYSTEM EFFICIENCY CALCULATION (Including MPPT)
// ═════════════════════════════════════════════════════════════════════════
float calculateSystemEfficiency() {
  if (currentData.solarPower > 0.1) {
    // System efficiency = (Battery power / Solar power) * 100
    // For charging: positive efficiency
    // For no load: efficiency approaches MPPT efficiency
    if (currentData.isCharging) {
      // During charging, estimate based on power flow
      float efficiency = (currentData.chargingPower / currentData.solarPower) * 100.0;
      return constrain(efficiency, 0, 100);
    } else {
      // No charging, return MPPT efficiency
      return currentData.mpptEfficiency;
    }
  }
  return 0.0;
}

// ═════════════════════════════════════════════════════════════════════════
// BATTERY HEALTH TRACKING
// ═════════════════════════════════════════════════════════════════════════
void updateBatteryHealth() {
  // Track voltage range
  if (currentData.battVoltage < battHealth.voltageMin && currentData.battVoltage > 3.0) {
    battHealth.voltageMin = currentData.battVoltage;
  }
  if (currentData.battVoltage > battHealth.voltageMax) {
    battHealth.voltageMax = currentData.battVoltage;
  }

  // Ampere-hour integration
  unsigned long now = millis();
  if (battHealth.lastUpdate > 0) {
    float hours = (now - battHealth.lastUpdate) / 3600000.0;

    // Discharge: measured by INA219 #2 (positive current = battery -> load)
    float dischargeAh = (currentData.battCurrent > 0) ? currentData.battCurrent / 1000.0 * hours : 0;
    // Charge: estimated from CN3791 output power (no sensor on the charge path)
    float chargeAh = (currentData.isCharging && currentData.battVoltage > 3.0)
                       ? currentData.chargingPower / currentData.battVoltage * hours : 0;

    battHealth.totalDischargeAh += dischargeAh;
    battHealth.totalChargeAh += chargeAh;
    currentData.totalAh += dischargeAh + chargeAh;
  }
  battHealth.lastUpdate = now;

  // Cycle counting from SOC throughput: a 100% swing down and back up = 1 cycle.
  // Counted in 2% increments so voltage noise does not inflate the count.
  if (battHealth.lastSOC < 0) battHealth.lastSOC = currentData.battSOC;   // first reading
  float socChange = fabs(currentData.battSOC - battHealth.lastSOC);
  if (socChange >= 2.0) {
    currentData.cycleCount += socChange / 200.0;
    if (currentData.battSOC >= 99.0 && battHealth.lastSOC < 99.0) battHealth.fullCharges++;
    battHealth.lastSOC = currentData.battSOC;
  }
}

// ═════════════════════════════════════════════════════════════════════════
// MPPT TRACKING ANALYSIS
// ═════════════════════════════════════════════════════════════════════════
void updateMPPTTracking() {
  // Analyze MPPT performance
  // CN3791 automatically tracks MPP, we just monitor its effectiveness

  // Check if MPPT is actively tracking
  if (currentData.solarVoltage > 1.0 && currentData.solarCurrent > 10) {
    currentData.mpptTracking = true;

    // Calculate MPPT efficiency
    // Theoretical maximum power at ~80% of Voc for typical solar panels
    float theoreticalMPPVoltage = SOLAR_PANEL_MAX_VOLTAGE * 0.80;
    currentData.mpptVoltage = theoreticalMPPVoltage;

    // MPPT efficiency = actual power / (Voc * Isc * fill factor)
    // Simplified: compare to theoretical MPP
    float voltageRatio = currentData.solarVoltage / theoreticalMPPVoltage;
    currentData.mpptEfficiency = voltageRatio * 100.0;

    // Clamp to realistic range
    currentData.mpptEfficiency = constrain(currentData.mpptEfficiency, 70, 98);

    // Update history for averaging
    mpptData.efficiencyHistory[mpptData.historyIndex] = currentData.mpptEfficiency;
    mpptData.historyIndex = (mpptData.historyIndex + 1) % 10;

    // Track maximum power point
    if (currentData.solarPower > mpptData.maxPower) {
      mpptData.maxPower = currentData.solarPower;
      mpptData.maxPowerVoltage = currentData.solarVoltage;
    }
  } else {
    currentData.mpptTracking = false;
    currentData.mpptEfficiency = 0;
  }

  // Calculate average MPPT efficiency
  float avgEfficiency = 0;
  int count = 0;
  for (int i = 0; i < 10; i++) {
    if (mpptData.efficiencyHistory[i] > 0) {
      avgEfficiency += mpptData.efficiencyHistory[i];
      count++;
    }
  }
  if (count > 0) {
    currentData.mpptEfficiency = avgEfficiency / count;
  }

  mpptData.lastSolarVoltage = currentData.solarVoltage;
  mpptData.lastSolarPower = currentData.solarPower;
}

// ═════════════════════════════════════════════════════════════════════════
// OLED DISPLAY UPDATE
// ═════════════════════════════════════════════════════════════════════════
void updateOLED() {
  display.clearDisplay();

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MICROGRID CN3791");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Solar
  display.setCursor(0, 14);
  display.print("Sol:");
  display.print(currentData.solarPower, 2);
  display.print("W");

  // MPPT Indicator
  if (currentData.mpptTracking) {
    display.setCursor(80, 14);
    display.print("MPPT");
  }

  // Battery
  display.setCursor(0, 24);
  display.print("Bat:");
  display.print(currentData.battVoltage, 2);
  display.print("V ");
  display.print(currentData.battSOC, 0);
  display.print("%");

  // Progress bar
  display.drawRect(0, 35, 128, 12, SSD1306_WHITE);
  int barWidth = (currentData.battSOC / 100.0) * 124;
  display.fillRect(2, 37, barWidth, 8, SSD1306_WHITE);

  // Status
  display.setCursor(0, 50);
  if (currentData.isCharging) {
    display.print("Charging ");
    display.print(currentData.chargingPower, 1);
    display.print("W");
  } else if (currentData.battCurrent < -10) {
    display.print("Discharge ");
    display.print(abs(currentData.battCurrent), 0);
    display.print("mA");
  } else {
    display.print("Ready SOH:");
    display.print(currentData.battSOH, 0);
    display.print("%");
  }

  display.display();
}

// ═════════════════════════════════════════════════════════════════════════
// FIREBASE DATA UPLOAD
// ═════════════════════════════════════════════════════════════════════════
void sendToFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ WiFi not connected - skipping Firebase upload");
    return;
  }

  HTTPClient http;

  // Create JSON document
  JsonDocument doc;
  doc["timestamp"] = millis();
  doc["uptime"] = (millis() - systemStartTime) / 1000;  // seconds

  JsonObject solar = doc["solar"].to<JsonObject>();
  solar["voltage"] = currentData.solarVoltage;
  solar["current"] = currentData.solarCurrent;
  solar["power"] = currentData.solarPower;

  JsonObject battery = doc["battery"].to<JsonObject>();
  battery["voltage"] = currentData.battVoltage;
  battery["current"] = currentData.battCurrent;
  battery["power"] = currentData.battPower;
  battery["soc"] = currentData.battSOC;
  battery["soh"] = currentData.battSOH;
  battery["efficiency"] = currentData.battEfficiency;
  battery["isCharging"] = currentData.isCharging;

  JsonObject mppt = doc["mppt"].to<JsonObject>();
  mppt["efficiency"] = currentData.mpptEfficiency;
  mppt["tracking"] = currentData.mpptTracking;
  mppt["mppVoltage"] = currentData.mpptVoltage;
  mppt["chargingPower"] = currentData.chargingPower;

  JsonObject system = doc["system"].to<JsonObject>();
  system["efficiency"] = currentData.sysEfficiency;

  JsonObject health = doc["health"].to<JsonObject>();
  health["cycles"] = currentData.cycleCount;
  health["totalAh"] = currentData.totalAh;
  health["chargeAh"] = battHealth.totalChargeAh;
  health["dischargeAh"] = battHealth.totalDischargeAh;

  JsonObject metrics = doc["metrics"].to<JsonObject>();
  metrics["solarEnergy"] = currentData.solarEnergyTotal;
  metrics["peakSolar"] = currentData.peakSolarPower;
  metrics["maxMPPPower"] = mpptData.maxPower;

  String jsonString;
  serializeJson(doc, jsonString);

  // Send to Firebase
  String url = "https://" + String(firebaseHost) + "/microgrid/data/" + String(dataPointCount) +
               ".json?auth=" + String(firebaseAuth);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.PUT(jsonString);

  if (httpCode == 200) {
    Serial.println("✅ Data sent to Firebase (#" + String(dataPointCount) + ")");
  } else {
    Serial.print("❌ Firebase upload failed - HTTP ");
    Serial.println(httpCode);
  }

  http.end();
}

// ═════════════════════════════════════════════════════════════════════════
// WEB SERVER HANDLERS
// ═════════════════════════════════════════════════════════════════════════
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleData() {
  JsonDocument doc;

  doc["solarVoltage"] = currentData.solarVoltage;
  doc["solarCurrent"] = currentData.solarCurrent;
  doc["solarPower"] = currentData.solarPower;
  doc["battVoltage"] = currentData.battVoltage;
  doc["battCurrent"] = currentData.battCurrent;
  doc["battPower"] = currentData.battPower;
  doc["battSOC"] = currentData.battSOC;
  doc["battSOH"] = currentData.battSOH;
  doc["battEfficiency"] = currentData.battEfficiency;
  doc["sysEfficiency"] = currentData.sysEfficiency;
  doc["peakSolar"] = currentData.peakSolarPower;
  doc["totalEnergy"] = currentData.solarEnergyTotal;
  doc["cycleCount"] = currentData.cycleCount;
  doc["totalAh"] = currentData.totalAh;
  doc["mpptEfficiency"] = currentData.mpptEfficiency;
  doc["mpptTracking"] = currentData.mpptTracking;
  doc["isCharging"] = currentData.isCharging;
  doc["chargingPower"] = currentData.chargingPower;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ═════════════════════════════════════════════════════════════════════════
// SERIAL DEBUG OUTPUT
// ═════════════════════════════════════════════════════════════════════════
void printSerialData() {
  Serial.println("\n╔════════════════════════════════════════════════════════╗");
  Serial.println("║         MICROGRID SYSTEM STATUS (CN3791 MPPT)          ║");
  Serial.println("╚════════════════════════════════════════════════════════╝");

  Serial.println("\n☀ SOLAR PANEL:");
  Serial.printf("   Voltage:      %.3f V\n", currentData.solarVoltage);
  Serial.printf("   Current:      %.1f mA\n", currentData.solarCurrent);
  Serial.printf("   Power:        %.3f W\n", currentData.solarPower);
  Serial.printf("   Peak Power:   %.3f W\n", currentData.peakSolarPower);
  Serial.printf("   Total Energy: %.3f Wh\n", currentData.solarEnergyTotal);

  Serial.println("\n⚡ CN3791 MPPT CONTROLLER:");
  Serial.printf("   Status:       %s\n", currentData.mpptTracking ? "TRACKING" : "IDLE");
  Serial.printf("   Efficiency:   %.1f %%\n", currentData.mpptEfficiency);
  Serial.printf("   MPP Voltage:  %.2f V\n", currentData.mpptVoltage);
  Serial.printf("   Max Power:    %.3f W @ %.2f V\n", mpptData.maxPower, mpptData.maxPowerVoltage);

  Serial.println("\n🔋 BATTERY STATUS:");
  Serial.printf("   Voltage:      %.3f V\n", currentData.battVoltage);
  Serial.printf("   Current:      %.1f mA (%s)\n",
                currentData.battCurrent,
                currentData.isCharging ? "CHARGING" : (currentData.battCurrent < 0 ? "DISCHARGING" : "IDLE"));
  Serial.printf("   Power:        %.3f W\n", abs(currentData.battPower));
  Serial.printf("   SOC:          %.1f %%\n", currentData.battSOC);
  Serial.printf("   SOH:          %.1f %%\n", currentData.battSOH);
  Serial.printf("   Efficiency:   %.1f %%\n", currentData.battEfficiency);

  if (currentData.isCharging) {
    Serial.printf("   Charging Pwr: %.2f W\n", currentData.chargingPower);
    if (currentData.chargeTimeRemaining > 0) {
      Serial.printf("   Time to Full: %.0f min\n", currentData.chargeTimeRemaining);
    }
  }

  Serial.println("\n📊 BATTERY HEALTH:");
  Serial.printf("   Voltage Range: %.3f - %.3f V\n", battHealth.voltageMin, battHealth.voltageMax);
  Serial.printf("   Cycle Count:   %.2f\n", currentData.cycleCount);
  Serial.printf("   Total Ah:      %.3f Ah\n", currentData.totalAh);
  Serial.printf("   Charged:       %.3f Ah\n", battHealth.totalChargeAh);
  Serial.printf("   Discharged:    %.3f Ah\n", battHealth.totalDischargeAh);

  Serial.println("\n⚙ SYSTEM METRICS:");
  Serial.printf("   Efficiency:    %.1f %%\n", currentData.sysEfficiency);
  Serial.printf("   Uptime:        %lu seconds\n", (millis() - systemStartTime) / 1000);
  Serial.printf("   Data Points:   %lu\n", dataPointCount);
  Serial.printf("   WiFi RSSI:     %d dBm\n", WiFi.RSSI());

  Serial.println("\n════════════════════════════════════════════════════════\n");
}
