#include <WiFiManager.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WebServer.h>
#include <TimeLib.h>
#include <Preferences.h>
#include "time.h"
#include "sntp.h"
#include <TinyStepper_28BYJ_48.h>

// Pin Definitions for Stepper Motor
#define MOTOR_IN1_PIN 13
#define MOTOR_IN2_PIN 12
#define MOTOR_IN3_PIN 14
#define MOTOR_IN4_PIN 27

#define WATER_PUMP_PIN 26 



// Pin Definitions for RFID and Buzzer

#define SS_PIN    5    // SDA
#define RST_PIN   4    
#define MISO_PIN  19
#define MOSI_PIN  23  
#define SCK_PIN   18
#define BEEPER_PIN 25

// Stepper Motor Constants
const int STEPS_PER_REVOLUTION = 2048;  // Full rotation steps for 28BYJ-48
const int COMPARTMENTS = 6;             // Total number of compartments
const int STEPS_PER_COMPARTMENT = STEPS_PER_REVOLUTION / COMPARTMENTS;

// Object Instantiation
Preferences preferences;
LiquidCrystal_I2C lcd(0x27, 16, 2);
TinyStepper_28BYJ_48 stepper;
MFRC522 rfid(SS_PIN, RST_PIN);
WebServer server(80);

// NTP Configuration
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 28800;  // GMT+8
const int daylightOffset_sec = 0;

// Medicine Variables
String med1_name = "", med1_time = "", med2_name = "", med2_time = "";
String med3_name = "", med3_time = "", med4_name = "", med4_time = "";
String med5_name = "", med5_time = "";
int med1_interval = 0, med2_interval = 0, med3_interval = 0;
int med4_interval = 0, med5_interval = 0;
int med_quantities[5] = { 0, 0, 0, 0, 0 };
unsigned long lastTakenTime[5] = { 0, 0, 0, 0, 0 };
String lastScannedTag = "";
bool alarmActive = false;
int activeAlarmMed = -1;
int currentCompartment = 1;
bool isWifiConnected = false;

// HTML Template for Web Interface
const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; padding: 20px; }
        .med-div { margin-bottom: 20px; padding: 10px; border: 1px solid #ccc; }
        input { margin: 5px; padding: 5px; }
    </style>
</head>
<body>
    <div>
        <h2>Current Time (GMT+8)</h2>
        <p id="currentTime"></p>
    </div>
    <div>
        <h2>Medicine Schedule</h2>
        <form id="schedule">
            <div class="med-div">
                <h3>Medicine 1</h3>
                <input type="text" id="name1" placeholder="Medicine Name">
                <input type="time" id="time1">
                <input type="number" id="interval1" placeholder="Interval (hours)">
                <input type="number" id="qty1" placeholder="Quantity">
            </div>
            <div class="med-div">
                <h3>Medicine 2</h3>
                <input type="text" id="name2" placeholder="Medicine Name">
                <input type="time" id="time2">
                <input type="number" id="interval2" placeholder="Interval (hours)">
                <input type="number" id="qty2" placeholder="Quantity">
            </div>
            <div class="med-div">
                <h3>Medicine 3</h3>
                <input type="text" id="name3" placeholder="Medicine Name">
                <input type="time" id="time3">
                <input type="number" id="interval3" placeholder="Interval (hours)">
                <input type="number" id="qty3" placeholder="Quantity">
            </div>
            <div class="med-div">
                <h3>Medicine 4</h3>
                <input type="text" id="name4" placeholder="Medicine Name">
                <input type="time" id="time4">
                <input type="number" id="interval4" placeholder="Interval (hours)">
                <input type="number" id="qty4" placeholder="Quantity">
            </div>
            <div class="med-div">
                <h3>Medicine 5</h3>
                <input type="text" id="name5" placeholder="Medicine Name">
                <input type="time" id="time5">
                <input type="number" id="interval5" placeholder="Interval (hours)">
                <input type="number" id="qty5" placeholder="Quantity">
            </div>
            <button type="submit">Save Schedule</button>
            <button type="button" onclick="clearAll()">Clear All</button>
        </form>
    </div>
    <div>
        <h2>Current Schedule</h2>
        <div id="schedule-display">
            Medicine 1: <span id="m1name"></span> | <span id="m1time"></span> | <span id="m1int"></span> | Qty: <span id="m1qty"></span> | Last Taken: <span id="m1last"></span><br>
            Medicine 2: <span id="m2name"></span> | <span id="m2time"></span> | <span id="m2int"></span> | Qty: <span id="m2qty"></span> | Last Taken: <span id="m2last"></span><br>
            Medicine 3: <span id="m3name"></span> | <span id="m3time"></span> | <span id="m3int"></span> | Qty: <span id="m3qty"></span> | Last Taken: <span id="m3last"></span><br>
            Medicine 4: <span id="m4name"></span> | <span id="m4time"></span> | <span id="m4int"></span> | Qty: <span id="m4qty"></span> | Last Taken: <span id="m4last"></span><br>
            Medicine 5: <span id="m5name"></span> | <span id="m5time"></span> | <span id="m5int"></span> | Qty: <span id="m5qty"></span> | Last Taken: <span id="m5last"></span>
        </div>
        <p>Last Tag: <span id="lasttag"></span></p>
    </div>
    <script>
        function clearAll() {
            if(!confirm('Are you sure you want to clear all data? This cannot be undone.')) {
                return;
            }
            for(let i=1; i<=5; i++) {
                document.getElementById('name'+i).value = '';
                document.getElementById('time'+i).value = '';
                document.getElementById('interval'+i).value = '';
                document.getElementById('qty'+i).value = '';
            }
            submitForm();
        }

        document.getElementById('schedule').onsubmit = function(e) {
            e.preventDefault();
            if (!confirm('Are you sure you want to save these changes?')) {
                return;
            }
            let formData = '';
            for(let i=1; i<=5; i++) {
                const name = document.getElementById('name'+i).value || '';
                const time = document.getElementById('time'+i).value || '';
                const interval = document.getElementById('interval'+i).value || '0';
                const qty = document.getElementById('qty'+i).value || '0';
                
                if (name && !time) {
                    alert('Please set a time for ' + name);
                    return;
                }
                
                formData += 'name' + i + '=' + encodeURIComponent(name) + '&';
                formData += 'time' + i + '=' + encodeURIComponent(time) + '&';
                formData += 'interval' + i + '=' + encodeURIComponent(interval) + '&';
                formData += 'qty' + i + '=' + encodeURIComponent(qty) + '&';
            }
            
            fetch('/submit', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: formData.slice(0, -1)
            })
            .then(response => {
                if(response.ok) {
                    loadCurrentData();
                    alert('Schedule saved successfully!');
                } else {
                    alert('Error saving schedule');
                }
            });
        };

        function loadCurrentData() {
            fetch('/data')
                .then(response => response.text())
                .then(data => {
                    let pairs = data.split('&');
                    pairs.forEach(pair => {
                        let [key, value] = pair.split('=');
                        if(key.startsWith('name')) {
                            let num = key.charAt(4);
                            document.getElementById('m'+num+'name').textContent = value;
                        }
                        else if(key.startsWith('time')) {
                            let num = key.charAt(4);
                            document.getElementById('m'+num+'time').textContent = value;
                        }
                        else if(key.startsWith('interval')) {
                            let num = key.charAt(8);
                            document.getElementById('m'+num+'int').textContent = value;
                        }
                        else if(key.startsWith('qty')) {
                            let num = key.charAt(3);
                            document.getElementById('m'+num+'qty').textContent = value;
                        }
                        else if(key.startsWith('last')) {
                            let num = key.charAt(4);
                            let timestamp = parseInt(value);
                            let date = new Date(timestamp * 1000);
                            document.getElementById('m'+num+'last').textContent = 
                                timestamp > 0 ? date.toLocaleTimeString() : 'Not taken';
                        }
                        else if(key === 'tag') {
                            document.getElementById('lasttag').textContent = value;
                        }
                    });
                });

            fetch('/time')
                .then(response => response.text())
                .then(time => {
                    document.getElementById('currentTime').textContent = time;
                });
        }
        loadCurrentData();
        setInterval(loadCurrentData, 1000);
    </script>
</body>
</html>
)rawliteral";


// Function to save medicine data to preferences
void saveData() {
  preferences.begin("medbox", false);
  
  // Save medicine names
  preferences.putString("med1_name", med1_name);
  preferences.putString("med2_name", med2_name);
  preferences.putString("med3_name", med3_name);
  preferences.putString("med4_name", med4_name);
  preferences.putString("med5_name", med5_name);

  // Save times
  preferences.putString("med1_time", med1_time);
  preferences.putString("med2_time", med2_time);
  preferences.putString("med3_time", med3_time);
  preferences.putString("med4_time", med4_time);
  preferences.putString("med5_time", med5_time);

  // Save intervals
  preferences.putInt("med1_int", med1_interval);
  preferences.putInt("med2_int", med2_interval);
  preferences.putInt("med3_int", med3_interval);
  preferences.putInt("med4_int", med4_interval);
  preferences.putInt("med5_int", med5_interval);

  // Save quantities and last taken times
  for(int i = 0; i < 5; i++) {
    preferences.putInt(("qty" + String(i+1)).c_str(), med_quantities[i]);
    preferences.putULong(("last" + String(i+1)).c_str(), lastTakenTime[i]);
  }

  preferences.end();
}

// Function to load medicine data from preferences
void loadData() {
  preferences.begin("medbox", true);
  
  // Load medicine names
  med1_name = preferences.getString("med1_name", "");
  med2_name = preferences.getString("med2_name", "");
  med3_name = preferences.getString("med3_name", "");
  med4_name = preferences.getString("med4_name", "");
  med5_name = preferences.getString("med5_name", "");

  // Load times
  med1_time = preferences.getString("med1_time", "");
  med2_time = preferences.getString("med2_time", "");
  med3_time = preferences.getString("med3_time", "");
  med4_time = preferences.getString("med4_time", "");
  med5_time = preferences.getString("med5_time", "");

  // Load intervals
  med1_interval = preferences.getInt("med1_int", 0);
  med2_interval = preferences.getInt("med2_int", 0);
  med3_interval = preferences.getInt("med3_int", 0);
  med4_interval = preferences.getInt("med4_int", 0);
  med5_interval = preferences.getInt("med5_int", 0);

  // Load quantities and last taken times
  for(int i = 0; i < 5; i++) {
    med_quantities[i] = preferences.getInt(("qty" + String(i+1)).c_str(), 0);
    lastTakenTime[i] = preferences.getULong(("last" + String(i+1)).c_str(), 0);
  }

  preferences.end();
}

// Function to move stepper motor to specific compartment
// Function to move stepper motor to specific compartment
void moveToCompartment(int compartmentNumber) {
  if (compartmentNumber < 1 || compartmentNumber > COMPARTMENTS) return;
  
  int targetPosition = (compartmentNumber - 1) * STEPS_PER_COMPARTMENT;
  stepper.moveToPositionInSteps(targetPosition);
  currentCompartment = compartmentNumber;
  delay(500); // Settling time
  
 
}

// Function to print local time to Serial
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("No time available (yet)");
    return;
  }
  Serial.printf("Time: %02d-%02d-%04d %02d:%02d:%02d\n",
                timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

// Web server handlers
void handleTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    server.send(200, "text/plain", "Time not available");
    return;
  }
  char timeStr[64];
  sprintf(timeStr, "%02d-%02d-%04d %02d:%02d:%02d",
          timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  server.send(200, "text/plain", timeStr);
}

void handleRoot() {
  server.send(200, "text/html", html);
}

void handleSubmit() {
  // Sanitize and process time inputs
  med1_name = server.arg("name1");
  med2_name = server.arg("name2");
  med3_name = server.arg("name3");
  med4_name = server.arg("name4");
  med5_name = server.arg("name5");

  // Ensure time format is HH:MM
  med1_time = sanitizeTime(server.arg("time1"));
  med2_time = sanitizeTime(server.arg("time2"));
  med3_time = sanitizeTime(server.arg("time3")); 
  med4_time = sanitizeTime(server.arg("time4"));
  med5_time = sanitizeTime(server.arg("time5"));

  med1_interval = server.arg("interval1").toInt();
  med2_interval = server.arg("interval2").toInt();
  med3_interval = server.arg("interval3").toInt();
  med4_interval = server.arg("interval4").toInt();
  med5_interval = server.arg("interval5").toInt();

  med_quantities[0] = server.arg("qty1").toInt();
  med_quantities[1] = server.arg("qty2").toInt();
  med_quantities[2] = server.arg("qty3").toInt();
  med_quantities[3] = server.arg("qty4").toInt();
  med_quantities[4] = server.arg("qty5").toInt();

  saveData();
  server.send(200, "text/plain", "OK");
  lcd.clear();
  lcd.print("Schedule Updated!");
  delay(2000);
  lcd.clear();
  displayIP();
}

String sanitizeTime(String timeStr) {
  if (timeStr.length() < 5) return "";
  // Return HH:MM format
  return timeStr.substring(0, 5);
}
void handleData() {
  String data = "name1=" + med1_name + "&time1=" + med1_time + "&interval1=" + med1_interval + "&qty1=" + med_quantities[0] + "&last1=" + lastTakenTime[0];
  data += "&name2=" + med2_name + "&time2=" + med2_time + "&interval2=" + med2_interval + "&qty2=" + med_quantities[1] + "&last2=" + lastTakenTime[1];
  data += "&name3=" + med3_name + "&time3=" + med3_time + "&interval3=" + med3_interval + "&qty3=" + med_quantities[2] + "&last3=" + lastTakenTime[2];
  data += "&name4=" + med4_name + "&time4=" + med4_time + "&interval4=" + med4_interval + "&qty4=" + med_quantities[3] + "&last4=" + lastTakenTime[3];
  data += "&name5=" + med5_name + "&time5=" + med5_time + "&interval5=" + med5_interval + "&qty5=" + med_quantities[4] + "&last5=" + lastTakenTime[4];
  data += "&tag=" + lastScannedTag;

  server.send(200, "text/plain", data);
}

// Function to display IP address on LCD
void displayIP() {
  String ip = WiFi.localIP().toString();
  lcd.setCursor(0, 1);
  lcd.print("                ");
  if (ip.length() > 16) {
    lcd.setCursor(0, 1);
    lcd.print(ip.substring(0, 16));
  } else {
    lcd.setCursor(0, 1);
    lcd.print(ip);
  }
}

// Function to check if it's time for a medicine
bool checkMedicineTime(String schedTime, int interval, int medIndex) {
  // Validate time format
  if (schedTime.length() != 5) return false;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;

  int curr_hour = timeinfo.tm_hour;
  int curr_min = timeinfo.tm_min;
  time_t currentTime = mktime(&timeinfo);

  int sched_hour = schedTime.substring(0, 2).toInt();
  int sched_min = schedTime.substring(3, 5).toInt();

  // Check cooldown period
  if (lastTakenTime[medIndex] > 0) {
    time_t elapsedSeconds = currentTime - lastTakenTime[medIndex];
    if (elapsedSeconds < 180) return false; // 3-minute cooldown
  }

  // Direct time match (with 3-minute window)
  if (curr_hour == sched_hour && curr_min >= sched_min && curr_min < sched_min + 3) {
    Serial.printf("Medicine %d: Time match at %02d:%02d\n", medIndex+1, curr_hour, curr_min);
    return true;
  }

  // Interval check
  if (interval > 0) {
    int mins_since_sched = ((curr_hour - sched_hour) * 60 + (curr_min - sched_min));
    if (mins_since_sched < 0) mins_since_sched += 1440; // Add 24 hours
    
    if (mins_since_sched > 0 && (mins_since_sched % (interval * 60)) < 3) {
      Serial.printf("Medicine %d: Interval match at %02d:%02d\n", medIndex+1, curr_hour, curr_min);
      return true;
    }
  }

  return false;
}
// Function to handle medicine dispensing logic
void handleMedicine() {
  String med_times[] = { med1_time, med2_time, med3_time, med4_time, med5_time };
  int intervals[] = { med1_interval, med2_interval, med3_interval, med4_interval, med5_interval };
  String names[] = { med1_name, med2_name, med3_name, med4_name, med5_name };

  if (alarmActive) return;

  bool medicinesDue[5] = {false};
  int dueCount = 0;
  String dueMedicines = "";

  // Check each medicine
  for (int i = 0; i < 5; i++) {
    if (med_quantities[i] <= 0) continue;
    
    if (checkMedicineTime(med_times[i], intervals[i], i)) {
      medicinesDue[i] = true;
      dueCount++;
      dueMedicines += names[i] + " ";
    }
  }

  // If medicines are due, activate alarm
  if (dueCount > 0) {
    for (int i = 0; i < 5; i++) {
      if (medicinesDue[i]) {
        alarmActive = true;
        activeAlarmMed = i;
        
        // Display on LCD
        lcd.clear();
        lcd.print("Due: " + dueMedicines);
        lcd.setCursor(0, 1);
        lcd.print("Take: " + names[i]);
        
        // Alert pattern: three short beeps
        beep(200);
        delay(200);
        beep(200);
        delay(200);
        beep(200);
        
        return;
      }
    }
  }
}
// Function to handle RFID tag scanning and medicine dispensing
void handleRFID() {
  static unsigned long lastScanTime = 0;
  if (millis() - lastScanTime < 2000) return;
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  lastScanTime = millis();

  String tag = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    tag += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    tag += String(rfid.uid.uidByte[i], HEX);
  }
  tag.toUpperCase();
  lastScannedTag = tag;
 // Validate authorized RFID tags
  if (tag != "1D9CC701" && tag != "67114002") {
    lcd.clear();
    lcd.print("Invalid Card!");
    delay(2000);
    lcd.clear();
    return;
  }
  if (!alarmActive) {
    lcd.clear();
    lcd.print("No medicine due");
    delay(2000);
    lcd.clear();
    return;
  }

  if (activeAlarmMed >= 0) {
    String med_times[] = { med1_time, med2_time, med3_time, med4_time, med5_time };
    int intervals[] = { med1_interval, med2_interval, med3_interval, med4_interval, med5_interval };
    String names[] = { med1_name, med2_name, med3_name, med4_name, med5_name };

    int targetCompartment = activeAlarmMed + 2;
    moveToCompartment(targetCompartment);
    med_quantities[activeAlarmMed]--;
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      lastTakenTime[activeAlarmMed] = mktime(&timeinfo);
      preferences.begin("medbox", false);
      preferences.putInt(("qty" + String(activeAlarmMed+1)).c_str(), med_quantities[activeAlarmMed]);
      preferences.putULong(("last" + String(activeAlarmMed+1)).c_str(), lastTakenTime[activeAlarmMed]);
      preferences.end();
    }

    lcd.clear();
    lcd.print("Medicine Taken!");
    delay(2000);

    if (med_quantities[activeAlarmMed] == 1) {
      lcd.setCursor(0, 1);
      lcd.print("Low Medicine!");
      beep(200);
      delay(500);
      beep(200);
    }

    bool moreMedicinesDue = false;
    for (int i = activeAlarmMed + 1; i < 5; i++) {
      if (med_quantities[i] > 0 && checkMedicineTime(med_times[i], intervals[i], i)) {
        activeAlarmMed = i;
        moreMedicinesDue = true;
        lcd.clear();
        lcd.print("Next: " + names[i]);
        lcd.setCursor(0, 1);
        lcd.print("Scan RFID");
        delay(2000);
        break;
      }
    }

   if (!moreMedicinesDue) {
      lcd.clear();
      lcd.print("Closing in:");
      for(int i = 10; i > 0; i--) {
        lcd.setCursor(0, 1);
        lcd.print("     " + String(i) + "s     ");
        delay(1000);
      }

      moveToCompartment(1);  // Close the compartment first
      
      // Now dispense water
      lcd.clear();
      lcd.print("Dispensing Water");
      lcd.setCursor(0, 1);
      lcd.print("Please Wait...");
      
      digitalWrite(WATER_PUMP_PIN, HIGH);  // Turn on the water pump
      delay(5000);  // Wait for 5 seconds
      digitalWrite(WATER_PUMP_PIN, LOW);   // Turn off the water pump
      
      alarmActive = false;
      activeAlarmMed = -1;
      lcd.clear();
      displayIP();
    }
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
void beep(int duration) {
  digitalWrite(BEEPER_PIN, HIGH);
  delay(duration);
  digitalWrite(BEEPER_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  
  // Initialize pins and peripherals
  pinMode(BEEPER_PIN, OUTPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  digitalWrite(WATER_PUMP_PIN, LOW);  // Ensure the water pump is off initially
  
  Wire.begin();
  lcd.init();
  lcd.backlight();
  
  // Initialize stepper motor
  stepper.connectToPins(MOTOR_IN1_PIN, MOTOR_IN2_PIN, MOTOR_IN3_PIN, MOTOR_IN4_PIN);
  stepper.setSpeedInStepsPerSecond(300);
  stepper.setAccelerationInStepsPerSecondPerSecond(500);
  moveToCompartment(1); // Move to closed position
  
  // Initialize RFID
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();

  lcd.clear();
  lcd.print("Connecting WiFi..");

  // WiFi setup
  WiFiManager wm;
  wm.setConnectTimeout(30);
  bool res = wm.autoConnect("AMEBO AP ESP32", "password");

  if (!res) {
    Serial.println("Failed to connect");
    lcd.clear();
    lcd.print("WiFi Failed!");
    isWifiConnected = false;
  } else {
    isWifiConnected = true;
    lcd.clear();
    lcd.print("WiFi Connected!");

    // Configure time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
    printLocalTime();

    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/submit", HTTP_POST, handleSubmit);
    server.on("/time", handleTime);
    server.begin();

    // Load saved medicine data
    loadData();

    displayIP();
    delay(2000);
    lcd.clear();
    displayIP();
  }
}

void loop() {
  if (isWifiConnected) {
    server.handleClient();
    
    Serial.printf("Alarm state: active=%d, activeMed=%d\n", alarmActive, activeAlarmMed);
    handleMedicine();

    if (!alarmActive) {
      // Display time and IP
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        lcd.setCursor(0, 0);
        int hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;

        char timeStr[9];
        sprintf(timeStr, "%2d%c%02d%s",
                hour12,
                (timeinfo.tm_sec % 2) ? ':' : ' ',
                timeinfo.tm_min,
                timeinfo.tm_hour >= 12 ? "PM" : "AM");
        lcd.print(timeStr);

        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP().toString());
      }
    } else {
      // Show medicine prompt
      String names[] = { med1_name, med2_name, med3_name, med4_name, med5_name };
      lcd.clear();
      lcd.print(names[activeAlarmMed]);
      lcd.setCursor(0, 1);
      lcd.print("Scan RFID");
      beep(200);
      delay(500);
    }

    handleRFID();
  }
  delay(1000);
}
