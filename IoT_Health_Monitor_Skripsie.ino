#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <ESPAsyncWebServer.h>
#include <SparkFun_Bio_Sensor_Hub_Library.h>
#include <Ticker.h>
#include <Preferences.h>  // Include Preferences library for NVS
#include <SPI.h>
#include "Adafruit_GFX.h"
#include "Adafruit_HX8357.h"
#include <NTPClient.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <vector>



Preferences preferences; // Create an instance of Preferences


// NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000);  // UTC + 1 hour (3600 sec) (South Africa Time Zone)


#define DEF_ADDR 0x55
#define D6T_ADDR 0x0A  // for I2C 7bit address
#define D6T_CMD 0x4C  // For D6T-1A-01
#define N_ROW 1
#define N_PIXEL 1
#define N_READ ((N_PIXEL + 1) * 2 + 1)


// These are 'flexible' lines that can be changed
#define TFT_CS 0
#define TFT_DC 1
#define TFT_RST 2 // RST can be set to -1 if you tie it to Arduino's reset

// Hardware SPI pins
#define MOSI 3    // Master Out Slave In
#define MISO 10    // Master In Slave Out
#define SCK 20     // Serial Clock

// SD Card pin
#define SD_CS 21   // Chip Select for the SD card






// Initialize the display
Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, MOSI, SCK, TFT_RST, MISO);

// color made up for grey

// Declaration of functions for display setup and updates
void displayStaticElements();
void displayBackgroundSections();
void displaySectionLabel(String label, int yPosition);
void displayDividers();
void updateMeasurements(float temperature, int heartRate, int oxygenLevel);
void updateValue(float value, float safeMin, float safeMax, int x, int y, String unit, int width, int height);
void displayTimestamp(String time);


#define HX8357_GREY 0x8410  // RGB (128, 128, 128)
#define HX8357_TEAL tft.color565(0, 128, 128)
#define HX8357_DARKGREY tft.color565(128, 128, 128)  // RGB for Light grey (It was dark grey, but I changed the rgb to light grey for the screen)


/* Buffers and Variables for Temperature sensor - D6T-1A-01 */
uint8_t rbuf[N_READ];
double ptat;
double pix_data[N_PIXEL];

uint8_t calc_crc(uint8_t data) {
    int index;
    uint8_t temp;
    for (index = 0; index < 8; index++) {
        temp = data;
        data <<= 1;
        if (temp & 0x80) {data ^= 0x07;}
    }
    return data;
}

bool D6T_checkPEC(uint8_t buf[], int n) {
    int i;
    uint8_t crc = calc_crc((D6T_ADDR << 1) | 1);  // I2C Read address (8bit)
    for (i = 0; i < n; i++) {
        crc = calc_crc(buf[i] ^ crc);
    }
    bool ret = crc != buf[n];
    if (ret) {
       // Serial.print("PEC check failed:");
       // Serial.print(crc, HEX);
       // Serial.print("(cal) vs ");
       // Serial.print(buf[n], HEX);
       // Serial.println("(get)");
    }
    return ret;
}

int16_t conv8us_s16_le(uint8_t* buf, int n) {
    uint16_t ret;
    ret = (uint16_t)buf[n];
    ret += ((uint16_t)buf[n + 1]) << 8;
    return (int16_t)ret;   // and convert negative.
}

// Wi-Fi Server - Details
const char* ssid = “Example_name”;  // Example name for ESP Wi-FI
const char* password = “12345678”; // Example password for ESP Wi-FI

// Connecting to Router - Put details in
const char* ssid2 = “Example”_Router;  //  I removed the actual router ssid
const char* password2 = “Example”_Password; //  I removed the actual router password



// Firebase credentials

#define FIREBASE_API_KEY ""  // I removed this, since this key is private, and used to access Google Firebase

#define FIREBASE_Database_URL ""  // I removed this, since this key is private, and because the database is private


FirebaseData fbdo;
FirebaseData fbdoGet;   // For retrieving data
FirebaseAuth auth;
FirebaseConfig config; 

FirebaseJsonData jsonData;  // Create a FirebaseJsonData object for retrieval


unsigned long sendDataPrevMills = 0;

bool signupOk = false;

// Define structures to store the sensor data along with a timestamp
struct TemperatureData {
  float temperature;
  String timestamp;
};

struct HeartRateData {
  float heartRate;
  String timestamp;
};

// Structure for oxygen level data
struct OxygenLevelData {
  float oxygen_level;
  String timestamp;
};





// Add similar vectors for heart rate and oxygen level
std::vector<TemperatureData> lastTenTemperatures;
std::vector<HeartRateData> lastTenHeartRates;
std::vector<OxygenLevelData> lastTenOxygenLevels;





// Reset pin, MFIO pin for the Sen-15219
const int resPin = 18; // Reset pin on ESP32
const int mfioPin = 19; // MFIO pin on ESP32

// Variables for the Heart Rate, Oximeter and Temp sensors
int Heartrate_value; 
int Oximeter_value; 
float temp_IF_sensor; 
float calibration_offset = 3.34;
float lastTemp = -1000.0;    // Track the last displayed temperature
int lastHeartRate = -1;
int lastOximeter = -1;

// Validity flags for data
bool tempDataValid = false;
bool heartRateDataValid = false;
bool oxygenDataValid = false;




// Historical Data Averages:


      float avgTemp = 0; 
      float avgHeartRate = 0;
      float avgOxygen = 0;



// Store the last displayed values as floats
float lastTemperature_2 = -999;
float lastHeartRate_2 = -999;
float lastOxygenLevel_2 = -999;

// Store the last displayed timestamp
String lastTime = "";


SparkFun_Bio_Sensor_Hub bioHub(resPin, mfioPin); 
bioData body;

// Create an instance of the web server on port 80
AsyncWebServer server(80);

// Buffer to store temperature data
uint8_t buffer_temp[5]; 

void setup() {
  // Initialize serial communication
  Serial.begin(115200);

  // Set up connection to router
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid2, password2);
  
  // Wait for the ESP32 to connect to Wi-Fi
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  // Once connected, print the IP address
  Serial.println("");
  Serial.println("WiFi connected.");

  // Initialize Wi-Fi
  if (!WiFi.softAP(ssid, password)) {
    Serial.println("Could not create Wi-Fi network. Check SSID and password for validity.");
  }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  // Firebase configuration
  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_Database_URL;

    // Sign up anonymously or use a token, depending on Firebase setup
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("SignUp succeeded");
    signupOk = true;
  } else {
    Serial.printf("SignUp failed, reason: %s\n", config.signer.signupError.message.c_str());
  }


// Set the token status callback
 // config.token_status_callback = tokenStatusCallback;

  //config.token_status_callback = tokenStatusCallBack;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);


lastTenTemperatures.clear();
  lastTenHeartRates.clear();
  lastTenOxygenLevels.clear();


  timeClient.begin();
  timeClient.update();

  // Initialize the first I2C bus (for HR, SPO2 and Temp)
  Wire.begin(5, 6);    // SDA on pin 8, SCL on pin 10

  tft.begin();
  tft.setRotation(1);  // Set display to landscape mode
  tft.fillScreen(HX8357_BLACK);
  displayStaticElements();
  




 
  // Initialize heart rate sensor
  int result = bioHub.begin();
  if (!result)
    Serial.println("Sensor started!");
  else
    Serial.println("Could not communicate with the sensor!!!");

  Serial.println("Configuring Sensor...."); 
  int error = bioHub.configBpm(MODE_ONE); // Configuring just the BPM settings 
  if(!error){
    Serial.println("Sensor configured.");
  } else {
    Serial.println("Error configuring sensor.");
    Serial.print("Error: "); 
    Serial.println(error); 
  }




  




  // Delay to allow sensor data to catch up
  delay(2500); 

  // Initialize Preferences library
  preferences.begin("userprefs", false);

  // Load the saved password
String savedPassword = preferences.getString("password", "");

if (savedPassword == "") {
  // No password set, show registration page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<!DOCTYPE html>";
    html += "<html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Register</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #6fa8dc; margin: 0; padding: 0; text-align: center; }";
    html += ".container { max-width: 450px; margin: auto; background: #cfe2f3; padding: 20px; border-radius: 10px; box-shadow: 0px 0px 10px 0px rgba(0, 0, 0, 0.1); margin-top: 100px; }";
    html += "input[type='password'] { width: 80%; padding: 10px; margin: 10px 0; border: 1px solid #ccc; border-radius: 5px; }";
    html += "button { background-color: #6aa84f; color: white; padding: 10px 20px; border: none; border-radius: 5px; font-size: 1em; cursor: pointer; }";
    html += "button:hover { background-color: #004d40; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>Register</h1>";
    html += "<form action='/register' method='post'>";
    html += "<input type='password' name='password' placeholder='Set Password' required>";
    html += "<button type='submit'>Register</button>";
    html += "</form>";
    html += "</div>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Handle the registration
  server.on("/register", HTTP_POST, [](AsyncWebServerRequest *request){  
    String newPassword;
    if (request->hasParam("password", true)) {
      newPassword = request->getParam("password", true)->value();
      preferences.putString("password", newPassword);
      request->send(200, "text/plain", "Registration successful! Please restart the ESP32 to log in with your new password.");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });
} else {
  // Password exists, show login page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<!DOCTYPE html>";
  html += "<html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Login</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #6fa8dc; margin: 0; padding: 0; text-align: center; }";
  html += ".container { max-width: 350px; margin: auto; background: #cfe2f3; padding: 20px; border-radius: 10px; box-shadow: 0px 0px 10px 0px rgba(0, 0, 0, 0.1); margin-top: 100px; }";
  html += "input[type='password'] { width: 80%; padding: 10px; margin: 10px 0; border: 1px solid #ccc; border-radius: 5px; }";
  html += "button { background-color: #6aa84f; color: white; padding: 10px 20px; border: none; border-radius: 5px; font-size: 1em; cursor: pointer; }";
  html += "button:hover { background-color: #004d40; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>Login</h1>";
  html += "<form action='/login' method='post'>";  // Changed action to '/login'
  html += "<input type='password' name='password' placeholder='Enter Password' required>";
  html += "<button type='submit'>Login</button>";
  html += "</form>";
  html += "</div>";
  html += "</body></html>";
  request->send(200, "text/html", html);
  });

   // Handle the password submission
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request){  
    String inputPassword;
    if (request->hasParam("password", true)) {
      inputPassword = request->getParam("password", true)->value();
    }

    String savedPassword = preferences.getString("password", "");

    if (inputPassword == savedPassword) {
      // Redirect to the dashboard page
      request->redirect("/start");
    } else {
      // Incorrect password
      request->send(403, "text/plain", "Forbidden: Incorrect Password");
    }

});

// Serve the start page
server.on("/start", HTTP_GET, [](AsyncWebServerRequest *request){
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Wellness Dashboard</title>

  <style>
    /* Base Styles */
    body {
      font-family: Arial, sans-serif;
      background-color: #f5f5f5; /* Light Gray for main background */
      margin: 0;
      padding: 0;
      transition: background-color 0.5s, color 0.5s;
      color: #2c3e50; /* Dark Slate Blue for primary text */
    }

    .container {
      display: flex;
      min-height: 100vh;
      padding-left: 200px; /* Adjust padding-left to prevent sidebar overlap */
      transition: padding-left 0.5s;
    }

    .main-content {
      flex-grow: 1;
      padding: 20px;
      background-color: #f5f5f5; /* Light Gray for main content */
      transition: background-color 0.5s;
    }

    .sidebar {
      position: fixed;
      top: 0;
      left: 0;
      height: 100vh;
      width: 200px;
      background-color: #34495e; /* Dark Blue-Gray for sidebar */
      padding-top: 20px;
      transition: background-color 0.5s;
      z-index: 10;
    }

    .sidebar button {
      display: block;
      background-color: transparent;
      color: white;
      padding: 10px 20px;
      margin: 10px;
      border: none;
      text-align: left;
      width: 80%;
      font-size: 1em;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .sidebar button:hover {
      background-color: #1abc9c; /* Teal for hover effect */
    }

    h1 {
      font-size: 48px; /* Reduced font size for better responsiveness */
      color: #2c3e50; /* Dark Slate Blue for headers */
      transition: color 0.5s;
      margin-top: 0;
    }

    p {
      font-size: 20px; /* Reduced font size for better readability */
      color: #7f8c8d; /* Gray for regular paragraphs */
      margin: 0;
      transition: color 0.5s;
    }

    .data {
      font-size: 24px; /* Increased font size for data points */
      color: #2980b9; /* Bright Blue for highlighted data */
      transition: color 0.5s;
    }

    button {
      background-color: #1abc9c; /* Teal for primary buttons */
      color: white;
      padding: 10px 20px;
      border: none;
      border-radius: 5px;
      font-size: 1em;
      cursor: pointer;
      margin-top: 20px;
      transition: background-color 0.3s;
    }

    button:hover {
      background-color: #16a085; /* Darker Teal for hover effect */
    }

    .theme-toggle {
      position: fixed;
      top: 10px;
      right: 10px;
      background-color: #3498db; /* Bright Blue for theme toggle */
      color: white;
      padding: 8px 12px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .theme-toggle:hover {
      background-color: #2980b9; /* Darker Blue for hover effect */
    }

    .dashboard-container {
      display: flex;
      justify-content: space-around;
      align-items: center;
      background-color: #f5f5f5; /* Light Gray to match main background */
      margin-top: 20px;
      width: 100%;
      gap: 20px;
    }

    .sensor-card {
      width: 300px;
      padding: 20px;
      margin: 10px;
      background-color: white; /* White background for sensor cards */
      border-radius: 15px;
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); /* Subtle shadow */
      text-align: center;
      transition: background-color 0.3s, color 0.3s;
    }

    .sensor-card:hover {
      background-color: #ecf0f1; /* Slightly darker on hover */
    }

    .sensor-value {
      font-size: 48px;
      color: #2980b9; /* Bright Blue for sensor values */
    }

    /* Dark mode adjustments */
    .dark-mode body {
      background-color: #555555; /* Neutral Grey for body background */
      color: #ecf0f1; /* Light gray for text */
    }

    .dark-mode .container {
      background-color: #555555; /* Match body background */
    }

    .dark-mode .main-content {
      background-color: #444444; /* Darker Grey for main content */
    }

    .dark-mode .sidebar {
      background-color: #444444; /* Dark Blue-Gray for sidebar in dark mode */
    }

    .dark-mode .sidebar button {
      background-color: #3c4f6c; /* Muted Navy for sidebar buttons */
      color: #ecf0f1; /* Light gray for button text */
    }

    .dark-mode .sidebar button:hover {
      background-color: #1abc9c; /* Teal on hover */
    }

    .dark-mode h1 {
      color: #ecf0f1; /* Light gray for headers */
    }

    .dark-mode p {
      color: #bdc3c7; /* Lighter gray for paragraphs */
    }

    .dark-mode .data {
      color: #ffffff; /* White for highlighted data */
    }

    .dark-mode button {
      background-color: #1abc9c; /* Teal for buttons in dark mode */
      color: white;
    }

    .dark-mode button:hover {
      background-color: #16a085; /* Darker Teal on hover */
    }

    .dark-mode .dashboard-container {
      background-color: #444444; /* Darker Grey to match main content */
    }

    .dark-mode .sensor-card {
      background-color: #3c4f6c; /* Muted Navy for sensor cards in dark mode */
      color: #ecf0f1; /* Light gray for text */
    }

    .dark-mode .sensor-value {
      color: #ffffff; /* White for sensor values */
    }

    /* Print Styles */
    @media print {
      body {
        background-color: #ffffff; /* White background for printing */
        color: #000000; /* Black text for better contrast */
        font-size: 12pt; /* Standardize font size for print */
      }

      /* Hide interactive and non-essential elements */
      .sidebar, .theme-toggle, button, .sensor-card, .dashboard-container, canvas {
        display: none;
      }

      /* Adjust main content layout */
      .container {
        flex-direction: column;
        padding-left: 0;
      }

      .main-content {
        background-color: #ffffff; /* Ensure main content has white background */
        padding: 20px;
      }

      /* Adjust headings and text */
      h1 {
        font-size: 24pt;
        color: #000000;
        margin-bottom: 10px;
      }

      p, .data {
        font-size: 14pt;
        color: #000000;
      }

      /* Ensure links are visible with URLs */
      a::after {
        content: " (" attr(href) ")";
        color: #000000;
        font-size: 10pt;
      }

      /* Remove box shadows and borders for a cleaner print */
      .sensor-card {
        box-shadow: none;
        border: none;
        background-color: transparent;
        color: #000000;
      }

      /* Page breaks for better pagination */
      h1, h2, h3 {
        page-break-after: avoid;
      }

      /* Ensure images are appropriately sized or hidden if decorative */
      img {
        max-width: 100%;
        height: auto;
      }
    }
  </style>

</head>
<body>

  <!-- Light/Dark mode toggle button on the right -->
  <button class="theme-toggle" onclick="toggleTheme()">Toggle Light/Dark Mode</button>

  <!-- Container that holds both the sidebar and main content -->
  <div class="container">

    <!-- Sidebar -->
    <div class="sidebar">
      <button onclick="window.location.href='/dashboard'">Detail View</button>
      <button onclick="window.location.href='/historical'">Historical Mode</button>
      <button onclick="window.location.href='/healthinfo'">Health Information</button>
    </div>

    <!-- Main content area -->
    <div class="main-content">
      <h1>Welcome to the Wellness Monitor</h1>

      <!-- Dashboard Section -->
      <div id="dashboardSection" class="dashboard-container">
        <div class="sensor-card">
          <h3>Temperature</h3>
          <p id="tempDataDashboard" class="sensor-value">Loading...</p>
        </div>
        <div class="sensor-card">
          <h3>Heart Rate</h3>
          <p id="hrDataDashboard" class="sensor-value">Loading...</p>
        </div>
        <div class="sensor-card">
          <h3>Oxygen Level</h3>
          <p id="oxyDataDashboard" class="sensor-value">Loading...</p>
        </div>
      </div> <!-- Close dashboardSection -->

      <!-- Add Single Histogram Canvas for all measurements -->
      <canvas id="measurementChart"></canvas>

    </div> <!-- Close main-content -->
  </div> <!-- Close container -->

  <!-- Include Chart.js library -->
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

  <!-- JavaScript for setting up the histogram with three datasets -->
  <script>
    let measurementChart;

    // Create a histogram with three datasets
    function createMeasurementChart(ctx) {
      measurementChart = new Chart(ctx, {
        type: 'bar',
        data: {
          labels: [], // Empty labels for real-time updates
          datasets: [{
            label: 'Temperature (°C)',
            data: [], // Temperature data
            backgroundColor: 'rgba(52, 152, 219, 0.6)', /* Bright Blue */
          }, {
            label: 'Heart Rate (BPM)',
            data: [],   // Heart rate data
            backgroundColor: 'rgba(26, 188, 156, 0.6)', /* Teal */
          }, {
            label: 'Oxygen Level (%)',
            data: [],  // Oxygen level data
            backgroundColor: 'rgba(231, 76, 60, 0.6)', /* Red */
          }],
        },
        options: {
          scales: {
            x: { 
              title: { 
                display: true, 
                text: 'Time',
                font: {
                  size: 18, /* Increased font size for axis title */
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                },
                color: '#000000', /* White for axis title in dark mode */
              },
              ticks: {
                color: '#000000', /* White for tick labels in dark mode */
                font: {
                  size: 14, /* Increased font size for tick labels */
                  family: 'Arial, sans-serif'
                }
              }
            },
            y: { 
              beginAtZero: true, 
              title: { 
                display: true, 
                text: 'Measurements',
                font: {
                  size: 18, /* Increased font size for axis title */
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                },
                color: '#000000', /* White for axis title in dark mode */
              },
              ticks: {
                color: '#000000', /* White for tick labels in dark mode */
                font: {
                  size: 14, /* Increased font size for tick labels */
                  family: 'Arial, sans-serif'
                }
              }
            },
          },
          plugins: {
            legend: {
              labels: {
                color: '#000000', /* White for legend text in dark mode */
                font: {
                  size: 16, /* Increased font size for legend labels */
                  family: 'Arial, sans-serif'
                }
              }
            }
          }
        },
      });
    }

    // Initialize the chart on page load
    window.onload = function() {
      createMeasurementChart(document.getElementById('measurementChart'));

      // Set theme based on localStorage
      var theme = localStorage.getItem('theme');
      if (theme === 'dark') { document.body.classList.add('dark-mode'); }

      // Show dashboard section
      showSection('dashboard');
    };

    // Function to update the histogram with new data
    function updateMeasurementChart(temp, hr, oxy) {
      const now = new Date().toLocaleTimeString(); // Use current time as label
      // Add data points for each measurement
      measurementChart.data.labels.push(now);
      measurementChart.data.datasets[0].data.push(temp); // Temperature
      measurementChart.data.datasets[1].data.push(hr);   // Heart Rate
      measurementChart.data.datasets[2].data.push(oxy);  // Oxygen Level

      // Maintain only the last 20 data points to keep the chart manageable
      if (measurementChart.data.labels.length > 20) {
        measurementChart.data.labels.shift();
        measurementChart.data.datasets[0].data.shift();
        measurementChart.data.datasets[1].data.shift();
        measurementChart.data.datasets[2].data.shift();
      }
      measurementChart.update();
    }

    // Function to toggle light/dark theme
    function toggleTheme() {
      document.body.classList.toggle('dark-mode');
      if (document.body.classList.contains('dark-mode')) {
        localStorage.setItem('theme', 'dark');
      } else {
        localStorage.setItem('theme', 'light');
      }
    }

    // Function to show sections based on selection (only dashboard in this case)
    function showSection(section) {
      document.getElementById('dashboardSection').style.display = 'none';
      if(section === 'dashboard') {
        document.getElementById('dashboardSection').style.display = 'flex';
      }
    }

    // Fetch and update data every second
    setInterval(function() {
      fetch('/data').then(response => response.json()).then(data => {
        document.getElementById('tempDataDashboard').innerHTML = data.temp_IF_sensor + ' &#8451;';
        document.getElementById('hrDataDashboard').innerHTML = data.heartrate + ' BPM';
        document.getElementById('oxyDataDashboard').innerHTML = data.oxygen + ' %';

        // Update histogram with real-time data
        updateMeasurementChart(data.temp_IF_sensor, data.heartrate, data.oxygen);
      }).catch(error => {
        console.error('Error fetching data:', error);
      });
    }, 1000);  // Update every second
  </script>

</body>
</html>
)rawliteral";

  request->send(200, "text/html", html);
});



// Serve the health information page
server.on("/healthinfo", HTTP_GET, [](AsyncWebServerRequest *request) {
  String html = "<!DOCTYPE html>\n";
  html += "<html lang='en'>\n";
  html += "<head>\n";
  html += "  <meta charset='UTF-8'>\n";
  html += "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
  html += "  <title>Health Information</title>\n";
  
  // Include CSS styles matching the existing layout and look
  html += "  <style>\n";
  
  // Base Styles
  html += "    body {\n";
  html += "      font-family: Arial, sans-serif;\n";
  html += "      background-color: #f5f5f5; /* Light Gray for main background */\n";
  html += "      color: #2c3e50; /* Dark Slate Blue for primary text */\n";
  html += "      margin: 0;\n";
  html += "      padding: 0;\n";
  html += "      transition: background-color 0.5s, color 0.5s;\n";
  html += "    }\n\n";
  
  html += "    .container {\n";
  html += "      display: flex;\n";
  html += "      min-height: 100vh;\n";
  html += "      padding-left: 200px; /* Adjust padding-left to prevent sidebar overlap */\n";
  html += "      transition: padding-left 0.5s;\n";
  html += "    }\n\n";
  
  html += "    .main-content {\n";
  html += "      flex-grow: 1;\n";
  html += "      padding: 20px;\n";
  html += "      background-color: #f5f5f5; /* Light Gray for main content */\n";
  html += "      transition: background-color 0.5s;\n";
  html += "    }\n\n";
  
  html += "    .sidebar {\n";
  html += "      position: fixed;\n";
  html += "      top: 0;\n";
  html += "      left: 0;\n";
  html += "      height: 100vh;\n";
  html += "      width: 200px;\n";
  html += "      background-color: #34495e; /* Dark Blue-Gray for sidebar */\n";
  html += "      padding-top: 20px;\n";
  html += "      z-index: 10;\n";
  html += "      transition: background-color 0.5s;\n";
  html += "    }\n\n";
  
  html += "    .sidebar button {\n";
  html += "      display: block;\n";
  html += "      background-color: transparent;\n";
  html += "      color: white;\n";
  html += "      padding: 10px 20px;\n";
  html += "      margin: 10px;\n";
  html += "      border: none;\n";
  html += "      text-align: left;\n";
  html += "      width: 80%;\n";
  html += "      font-size: 1em;\n";
  html += "      cursor: pointer;\n";
  html += "      transition: background-color 0.3s;\n";
  html += "      border-radius: 4px;\n";
  html += "    }\n\n";
  
  html += "    .sidebar button:hover {\n";
  html += "      background-color: #1abc9c; /* Teal for hover effect */\n";
  html += "    }\n\n";
  
  html += "    h1 {\n";
  html += "      font-size: 48px; /* Adjusted for better responsiveness */\n";
  html += "      color: #2c3e50; /* Dark Slate Blue for headers */\n";
  html += "      transition: color 0.5s;\n";
  html += "      margin-top: 0;\n";
  html += "      text-align: center;\n";
  html += "    }\n\n";
  
  html += "    h2 {\n";
  html += "      font-size: 24px;\n";
  html += "      color: #2c3e50; /* Dark Slate Blue for sub-headers */\n";
  html += "      transition: color 0.5s;\n";
  html += "      margin-bottom: 10px;\n";
  html += "    }\n\n";
  
  html += "    p {\n";
  html += "      font-size: 18px;\n";
  html += "      color: #666; /* Gray for regular paragraphs */\n";
  html += "      margin: 10px 0;\n";
  html += "      line-height: 1.6;\n";
  html += "    }\n\n";
  
  html += "    .health-section {\n";
  html += "      background-color: #ffffff; /* White background for sections */\n";
  html += "      padding: 20px;\n";
  html += "      margin-bottom: 20px;\n";
  html += "      border-radius: 8px;\n";
  html += "      box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);\n";
  html += "      transition: background-color 0.5s, color 0.5s;\n";
  html += "    }\n\n";
  
  html += "    .range {\n";
  html += "      margin: 10px 0;\n";
  html += "      padding: 10px;\n";
  html += "      border-radius: 5px;\n";
  html += "      font-weight: bold;\n";
  html += "    }\n\n";
  
  html += "    .normal {\n";
  html += "      background-color: #c8e6c9; /* Green for normal range */\n";
  html += "      color: #2c3e50;\n";
  html += "    }\n\n";
  
  html += "    .warning {\n";
  html += "      background-color: #ffecb3; /* Yellow for warning range */\n";
  html += "      color: #2c3e50;\n";
  html += "    }\n\n";
  
  html += "    .danger {\n";
  html += "      background-color: #ffcdd2; /* Red for danger range */\n";
  html += "      color: #2c3e50;\n";
  html += "    }\n\n";
  
  html += "    .theme-toggle {\n";
  html += "      position: fixed;\n";
  html += "      top: 10px;\n";
  html += "      right: 10px;\n";
  html += "      background-color: #3498db; /* Bright Blue for theme toggle */\n";
  html += "      color: white;\n";
  html += "      padding: 8px 12px;\n";
  html += "      border: none;\n";
  html += "      border-radius: 5px;\n";
  html += "      cursor: pointer;\n";
  html += "      transition: background-color 0.3s;\n";
  html += "    }\n\n";
  
  html += "    .theme-toggle:hover {\n";
  html += "      background-color: #2980b9; /* Darker Blue for hover effect */\n";
  html += "    }\n\n";
  
  // Dark mode styles
  html += "    .dark-mode body {\n";
  html += "      background-color: #555555; /* Neutral Grey for body background */\n";
  html += "      color: #ecf0f1; /* Light gray for text */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .container {\n";
  html += "      background-color: #555555; /* Match body background */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .main-content {\n";
  html += "      background-color: #444444; /* Darker Grey for main content */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .sidebar {\n";
  html += "      background-color: #34495e; /* Maintain dark sidebar */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .sidebar button {\n";
  html += "      background-color: #3c4f6c; /* Muted Navy for sidebar buttons */\n";
  html += "      color: #ecf0f1; /* Light gray for button text */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .sidebar button:hover {\n";
  html += "      background-color: #1abc9c; /* Teal on hover */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode h1, .dark-mode h2 {\n";
  html += "      color: #ecf0f1; /* Light gray for headers */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode p {\n";
  html += "      color: #bdc3c7; /* Lighter gray for paragraphs */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .health-section {\n";
  html += "      background-color: #1e1e1e; /* Very Dark Gray for sections */\n";
  html += "      color: #ecf0f1; /* Light gray for text */\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .range.normal {\n";
  html += "      background-color: #388e3c; /* Dark green for normal */\n";
  html += "      color: white;\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .range.warning {\n";
  html += "      background-color: #ffb74d; /* Darker yellow for warning */\n";
  html += "      color: white;\n";
  html += "    }\n\n";
  
  html += "    .dark-mode .range.danger {\n";
  html += "      background-color: #e57373; /* Dark red for danger */\n";
  html += "      color: white;\n";
  html += "    }\n\n";
  
  // Print Styles
  html += "    @media print {\n";
  html += "      body {\n";
  html += "        background-color: #ffffff; /* White background for printing */\n";
  html += "        color: #000000; /* Black text for better contrast */\n";
  html += "        font-size: 12pt; /* Standardize font size for print */\n";
  html += "      }\n\n";
  
  html += "      /* Hide interactive and non-essential elements */\n";
  html += "      .sidebar, .theme-toggle, button, .sensor-card, .dashboard-container, canvas, table, .chart-container {\n";
  html += "        display: none;\n";
  html += "      }\n\n";
  
  html += "      /* Adjust main content layout */\n";
  html += "      .container {\n";
  html += "        flex-direction: column;\n";
  html += "        padding-left: 0;\n";
  html += "      }\n\n";
  
  html += "      .main-content {\n";
  html += "        background-color: #ffffff; /* Ensure main content has white background */\n";
  html += "        padding: 20px;\n";
  html += "      }\n\n";
  
  html += "      /* Adjust headings and text */\n";
  html += "      h1 {\n";
  html += "        font-size: 24pt;\n";
  html += "        color: #000000;\n";
  html += "        margin-bottom: 10px;\n";
  html += "        text-align: center;\n";
  html += "      }\n\n";
  
  html += "      h2 {\n";
  html += "        font-size: 18pt;\n";
  html += "        color: #000000;\n";
  html += "      }\n\n";
  
  html += "      p, .data {\n";
  html += "        font-size: 14pt;\n";
  html += "        color: #000000;\n";
  html += "      }\n\n";
  
  html += "      /* Ensure links are visible with URLs */\n";
  html += "      a::after {\n";
  html += "        content: ' (' attr(href) ')';\n";
  html += "        color: #000000;\n";
  html += "        font-size: 10pt;\n";
  html += "      }\n\n";
  
  html += "      /* Remove box shadows and borders for a cleaner print */\n";
  html += "      table {\n";
  html += "        border: none;\n";
  html += "      }\n\n";
  
  html += "      th, td {\n";
  html += "        border-bottom: 1px solid #ddd;\n";
  html += "      }\n\n";
  
  html += "      /* Page breaks for better pagination */\n";
  html += "      h1, h2, h3 {\n";
  html += "        page-break-after: avoid;\n";
  html += "      }\n\n";
  
  html += "      /* Ensure images are appropriately sized or hidden if decorative */\n";
  html += "      img {\n";
  html += "        max-width: 100%;\n";
  html += "        height: auto;\n";
  html += "      }\n";
  
  html += "    }\n";
  
  html += "  </style>\n";
  
  html += "</head>\n";
  html += "<body>\n";
  
  // Light/Dark mode toggle button on the right
  html += "  <button class='theme-toggle' onclick='toggleTheme()'>Toggle Light/Dark Mode</button>\n";
  
  // Container that holds both the sidebar and main content
  html += "  <div class='container'>\n";
  
  // Sidebar with navigation buttons
  html += "    <div class='sidebar'>\n";
  html += "      <button onclick=\"window.location.href='/start'\">Dashboard</button>\n";
  html += "      <button onclick=\"window.location.href='/dashboard'\">Detail View</button>\n";
  html += "      <button onclick=\"window.location.href='/historical'\">Historical Mode</button>\n";
  html += "      <button onclick=\"window.location.href='/historical-temperature'\">Historical Temperature</button>\n";
  html += "      <button onclick=\"window.location.href='/historical-oxygen'\">Historical Oxygen Level</button>\n";
  html += "      <button onclick=\"window.location.href='/historical-heartrate'\">Historical Heart Rate</button>\n";
  html += "    </div>\n";
  
  // Main content area
  html += "    <div class='main-content'>\n";
  html += "      <h1>Health Information</h1>\n\n";
  
  // Temperature Section
  html += "      <div class='health-section'>\n";
  html += "        <h2>Temperature (°C)</h2>\n";
  html += "        <div class='range normal'>Normal Range: 36.1°C to 37.2°C</div>\n";
  html += "        <div class='range warning'>High (Fever): Above 38°C</div>\n";
  html += "        <div class='range danger'>Low (Hypothermia): Below 35°C</div>\n";
  html += "        <p><strong>Tips:</strong> Stay hydrated, rest, and monitor symptoms if you have a fever.</p>\n";
  html += "      </div>\n\n";
  
  // Heart Rate Section
  html += "      <div class='health-section'>\n";
  html += "        <h2>Heart Rate (BPM)</h2>\n";
  html += "        <div class='range normal'>Normal Range: 60 to 100 BPM</div>\n";
  html += "        <div class='range warning'>High (Tachycardia): Above 100 BPM</div>\n";
  html += "        <div class='range danger'>Low (Bradycardia): Below 60 BPM</div>\n";
  html += "        <p><strong>Tips:</strong> Exercise regularly, manage stress, and avoid excessive caffeine.</p>\n";
  html += "      </div>\n\n";
  
  // Oxygen Level Section
  html += "      <div class='health-section'>\n";
  html += "        <h2>Oxygen Level (SpO2 %)</h2>\n";
  html += "        <div class='range normal'>Normal Range: 95% to 100%</div>\n";
  html += "        <div class='range warning'>Low (Hypoxemia): Below 95%</div>\n";
  html += "        <div class='range danger'>Critical: Below 90% (Seek immediate help)</div>\n";
  html += "        <p><strong>Tips:</strong> Practice deep breathing, ensure good air quality, and avoid smoking.</p>\n";
  html += "      </div>\n";
  
  html += "    </div> <!-- Close main-content -->\n";
  html += "  </div> <!-- Close container -->\n\n";
  
  // JavaScript code
  html += "  <script>\n";
  
  // Function to toggle light/dark theme
  html += "    function toggleTheme() {\n";
  html += "      document.body.classList.toggle('dark-mode');\n";
  html += "      if (document.body.classList.contains('dark-mode')) {\n";
  html += "        localStorage.setItem('theme', 'dark');\n";
  html += "      } else {\n";
  html += "        localStorage.setItem('theme', 'light');\n";
  html += "      }\n";
  html += "    }\n\n";
  
  // Apply saved theme on load
  html += "    window.onload = function() {\n";
  html += "      var theme = localStorage.getItem('theme');\n";
  html += "      if (theme === 'dark') {\n";
  html += "        document.body.classList.add('dark-mode');\n";
  html += "      }\n";
  html += "    };\n";
  
  html += "  </script>\n";
  
  html += "</body>\n";
  html += "</html>";
  
  request->send(200, "text/html", html);
});


// Serve the historical health information page
server.on("/historical", HTTP_GET, [](AsyncWebServerRequest *request) {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Historical Health Information</title>

  <!-- Include Chart.js for pie and radar charts -->
  <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>

  <!-- Include CSS styles matching the existing layout and look -->
  <style>
    /* Base Styles */
    body {
      font-family: Arial, sans-serif;
      background-color: #f5f5f5; /* Light Gray for main background */
      color: #2c3e50; /* Dark Slate Blue for primary text */
      margin: 0;
      padding: 0;
      transition: background-color 0.5s, color 0.5s;
    }

    .container {
      display: flex;
      min-height: 100vh;
      padding-left: 200px; /* Prevent sidebar overlap */
      transition: padding-left 0.5s;
    }

    .main-content {
      flex-grow: 1;
      padding: 20px;
      background-color: inherit;
      transition: background-color 0.5s;
    }

    .sidebar {
      position: fixed;
      top: 0;
      left: 0;
      height: 100vh;
      width: 200px;
      background-color: #34495e; /* Dark Blue-Gray for sidebar */
      padding-top: 20px;
      transition: background-color 0.5s;
      z-index: 10;
    }

    .sidebar button {
      display: block;
      background-color: transparent;
      color: white;
      padding: 10px 20px;
      margin: 10px;
      border: none;
      text-align: left;
      width: 80%;
      font-size: 1em;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .sidebar button:hover {
      background-color: #1abc9c; /* Teal on hover */
    }

    h1 {
      font-size: 36px;
      color: #2c3e50; /* Dark Slate Blue for headers */
      transition: color 0.5s;
      margin-top: 0;
    }

    h2 {
      font-size: 24px;
      color: #2c3e50; /* Dark Slate Blue for sub-headers */
      margin-bottom: 10px;
    }

    p {
      font-size: 18px;
      color: #666; /* Gray for paragraphs */
      margin: 10px 0;
      transition: color 0.5s;
    }

    .theme-toggle {
      position: fixed;
      top: 10px;
      right: 10px;
      background-color: #34495e; /* Dark Blue-Gray for toggle button */
      color: white;
      padding: 10px 15px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      font-size: 16px;
      transition: background-color 0.3s;
    }

    .theme-toggle:hover {
      background-color: #1abc9c; /* Teal on hover */
    }

    .charts-container {
      display: flex;
      flex-wrap: wrap;
      gap: 20px;
    }

    .chart-box {
      flex: 1 1 300px;
      max-width: 500px;
    }

    .chart-box canvas {
      width: 100% !important;
      height: 300px !important;
    }

    .avg-container {
      background-color: #f5f5f5; /* Light Gray */
      color: #2c3e50;
      padding: 10px;
      border-radius: 8px;
      margin: 10px;
      text-align: center;
      transition: background-color 0.5s, color 0.5s;
    }

    .avg-container h2 {
      margin: 0;
      font-size: 24px;
      color: #2c3e50; /* Dark Slate Blue for average headers */
    }

    .avg-container p {
      margin: 0;
      font-size: 20px;
      color: #666; /* Gray for average values */
    }

    /* Dark mode styles */
    .dark-mode {
      background-color: #555555; /* Dark Gray */
      color: #ecf0f1; /* Light Gray for text */
    }

    .dark-mode .container {
      background-color: #555555; /* Match body background */
    }

    .dark-mode .main-content {
      background-color: #444444; /* Darker Gray */
    }

    .dark-mode .sidebar {
      background-color: #444444; /* Darker Gray for sidebar in dark mode */
    }

    .dark-mode .sidebar button {
      color: #ecf0f1; /* Light Gray for button text */
    }

    .dark-mode .sidebar button:hover {
      background-color: #16a085; /* Darker Teal on hover */
    }

    .dark-mode h1, .dark-mode h2 {
      color: #ecf0f1; /* Light Gray for headers */
    }

    .dark-mode p {
      color: #bdc3c7; /* Lighter Gray for paragraphs */
    }

    .dark-mode .theme-toggle {
      background-color: #333333; /* Dark Gray for toggle button */
    }

    .dark-mode .theme-toggle:hover {
      background-color: #16a085; /* Darker Teal on hover */
    }

    .dark-mode .avg-container {
      background-color: #333333; /* Dark Gray for average containers */
      color: #ecf0f1; /* Light Gray for text */
    }

    .dark-mode .avg-container h2 {
      color: #ecf0f1; /* Light Gray for average headers */
    }

    .dark-mode .avg-container p {
      color: #bbbbbb; /* Light Gray for average values */
    }

    /* Comparison Chart Container */
    .comparison-chart-container {
      width: 100%;
      max-width: 600px;
      margin: 40px auto;
    }

    .comparison-chart-container canvas {
      width: 100% !important;
      height: 400px !important;
    }

    /* Responsive Design */
    @media (max-width: 768px) {
      .container {
        flex-direction: column;
        padding-left: 0;
      }

      .sidebar {
        width: 100%;
        height: auto;
        position: relative;
      }
    }

    /* Print Styles */
    @media print {
      body {
        background-color: #ffffff; /* White background for printing */
        color: #000000; /* Black text for better contrast */
        font-size: 12pt; /* Standardize font size for print */
      }

      /* Hide interactive and non-essential elements */
      .sidebar, .theme-toggle, button, .avg-container, .charts-container, canvas {
        display: none;
      }

      /* Adjust main content layout */
      .container {
        flex-direction: column;
        padding-left: 0;
      }

      .main-content {
        background-color: #ffffff; /* Ensure main content has white background */
        padding: 20px;
      }

      /* Adjust headings and text */
      h1 {
        font-size: 24pt;
        color: #000000;
        margin-bottom: 10px;
      }

      h2 {
        font-size: 18pt;
        color: #000000;
      }

      p, .data {
        font-size: 14pt;
        color: #000000;
      }

      /* Ensure links are visible with URLs */
      a::after {
        content: " (" attr(href) ")";
        color: #000000;
        font-size: 10pt;
      }

      /* Remove box shadows and borders for a cleaner print */
      .avg-container {
        box-shadow: none;
        border: none;
        background-color: transparent;
        color: #000000;
      }

      /* Page breaks for better pagination */
      h1, h2, h3 {
        page-break-after: avoid;
      }

      /* Ensure images are appropriately sized or hidden if decorative */
      img {
        max-width: 100%;
        height: auto;
      }
    }
  </style>

</head>
<body>

  <!-- Light/Dark mode toggle button on the right -->
  <button class='theme-toggle' onclick='toggleTheme()'>Toggle Light/Dark Mode</button>

  <!-- Container that holds both the sidebar and main content -->
  <div class='container'>

    <!-- Sidebar with navigation buttons -->
    <div class='sidebar'>
      <button onclick="window.location.href='/start'">Dashboard</button>
      <button onclick="window.location.href='/dashboard'">Detail View</button>
      <button onclick="window.location.href='/healthinfo'">Health Information</button>
      <button onclick="window.location.href='/historical-temperature'">Historical Temperature</button>
      <button onclick="window.location.href='/historical-oxygen'">Historical Oxygen</button>
      <button onclick="window.location.href='/historical-heartrate'">Historical Heart Rate</button>
    </div>

    <!-- Main content area -->
    <div class='main-content'>
      <h1>Historical Health Information</h1>

      <!-- Averages Section -->
      <h2>Averages</h2>
      <div class='charts-container'>
        <div class='avg-container'>
          <h2>Temperature</h2>
          <p id='avg-temperature'>--</p>
        </div>
        <div class='avg-container'>
          <h2>Heart Rate</h2>
          <p id='avg-heart-rate'>--</p>
        </div>
        <div class='avg-container'>
          <h2>Oxygen Level</h2>
          <p id='avg-oxygen-level'>--</p>
        </div>
      </div>  <!-- Close charts-container -->

      <!-- Charts Section -->
      <h2>Average Health Overview</h2>
      <div class='charts-container'>
        
        <!-- Pie Chart -->
        <div class='chart-box'>
          <canvas id='healthPieChart'></canvas>
        </div>

        <!-- Radar Chart -->
        <div class='chart-box'>
          <canvas id='healthRadarChart'></canvas>
        </div>

      </div>  <!-- Close charts-container -->

      <!-- Comparison Chart Section -->
      <h2>Average vs Normal Health Levels</h2>
      <div class='comparison-chart-container'>
        <canvas id='comparisonChart'></canvas>
      </div>

    </div>  <!-- Close main-content -->
  </div>  <!-- Close container -->

  <!-- JavaScript for dynamic functionality and Chart.js integration -->
  <script>
    // Global Chart Variables
    let pieChart;
    let radarChart;
    let comparisonChart;

    // Function to determine color based on theme
    function getColor(isDarkMode) {
      return isDarkMode ? '#ffffff' : '#000000';
    }

    // Function to determine legend color based on theme
    function getLegendColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#000000';
    }

    // Function to determine grid lines and labels colors for Radar Chart
    function getRadarGridColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#cccccc';
    }

    // Function to determine Radar Chart point labels color
    function getRadarPointLabelsColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#000000';
    }

    // Function to determine Comparison Chart axis colors
    function getComparisonAxisColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#000000';
    }

    // Function to toggle light/dark theme
    function toggleTheme() {
      document.body.classList.toggle('dark-mode');
      if (document.body.classList.contains('dark-mode')) {
        localStorage.setItem('theme', 'dark');
      } else {
        localStorage.setItem('theme', 'light');
      }
      updateChartColors(); // Update chart colors when theme changes
    }

    // Apply saved theme on load
    document.addEventListener('DOMContentLoaded', (event) => {
      const theme = localStorage.getItem('theme');
      if (theme === 'dark') {
        document.body.classList.add('dark-mode');
      }
      initializeCharts(); // Initialize charts after theme is set
    });

    // Function to initialize all charts
    function initializeCharts() {
      fetch('/getAverages')
        .then(response => response.json())
        .then(data => {
          // Display Averages
          document.getElementById('avg-temperature').textContent = data.temperature.toFixed(2) + '°C';
          document.getElementById('avg-heart-rate').textContent = data.heart_rate.toFixed(2) + ' BPM';
          document.getElementById('avg-oxygen-level').textContent = data.oxygen_level.toFixed(2) + '%';

          // Initialize Pie Chart
          updatePieChart(data.temperature, data.heart_rate, data.oxygen_level);

          // Initialize Radar Chart
          updateRadarChart(data.temperature, data.heart_rate, data.oxygen_level);

          // Initialize Comparison Chart
          updateComparisonChart(data.temperature, data.heart_rate, data.oxygen_level);
        })
        .catch(error => console.log('Error fetching data:', error));
    }

    // Function to update the Pie Chart with new data
    function updatePieChart(temp, heartRate, oxygenLevel) {
      const ctx = document.getElementById('healthPieChart').getContext('2d');
      pieChart = new Chart(ctx, {
        type: 'pie',
        data: {
          labels: ['Temperature (°C)', 'Heart Rate (BPM)', 'Oxygen Level (%)'],
          datasets: [{
            data: [temp, heartRate, oxygenLevel],
            backgroundColor: ['#4caf50', '#ff9800', '#f44336'],
          }],
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          plugins: {
            legend: {
              position: 'bottom',
              labels: {
                color: getLegendColor(),
                font: {
                  size: 16,
                  family: 'Arial, sans-serif'
                }
              }
            },
          },
        },
      });
    }

    // Function to update the Radar Chart with new data
    function updateRadarChart(temp, heartRate, oxygenLevel) {
      const ctx = document.getElementById('healthRadarChart').getContext('2d');
      radarChart = new Chart(ctx, {
        type: 'radar',
        data: {
          labels: ['Temperature (°C)', 'Heart Rate (BPM)', 'Oxygen Level (%)'],
          datasets: [{
            label: 'Averages',
            data: [temp, heartRate, oxygenLevel],
            backgroundColor: 'rgba(76, 175, 80, 0.2)',
            borderColor: 'rgba(76, 175, 80, 1)',
            pointBackgroundColor: 'rgba(76, 175, 80, 1)',
          }],
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          scales: {
            r: {
              beginAtZero: true,
              angleLines: { color: getColor(document.body.classList.contains('dark-mode')) },
              grid: { color: getRadarGridColor() },
              pointLabels: { color: getRadarPointLabelsColor() }
            }
          },
          plugins: {
            legend: {
              labels: {
                color: getLegendColor(),
                font: {
                  size: 16,
                  family: 'Arial, sans-serif'
                }
              }
            },
          },
        },
      });
    }

    // Function to update the Comparison Chart with averages and normal ranges
    function updateComparisonChart(avgTemp, avgHeartRate, avgOxygenLevel) {
      // Define Normal Ranges
      const normalRanges = {
        temperature: { min: 36.1, max: 37.2 },
        heartRate: { min: 60, max: 100 },
        oxygenLevel: { min: 95, max: 100 }
      };

      // Calculate average vs normal (using mid-point of normal range)
      const normalValues = {
        temperature: (normalRanges.temperature.min + normalRanges.temperature.max) / 2,
        heartRate: (normalRanges.heartRate.min + normalRanges.heartRate.max) / 2,
        oxygenLevel: (normalRanges.oxygenLevel.min + normalRanges.oxygenLevel.max) / 2
      };

      const ctx = document.getElementById('comparisonChart').getContext('2d');
      comparisonChart = new Chart(ctx, {
        type: 'bar',
        data: {
          labels: ['Temperature (°C)', 'Heart Rate (BPM)', 'Oxygen Level (%)'],
          datasets: [
            {
              label: 'Average',
              data: [avgTemp, avgHeartRate, avgOxygenLevel],
              backgroundColor: 'rgba(76, 175, 80, 0.6)', // Green for average
            },
            {
              label: 'Normal',
              data: [normalValues.temperature, normalValues.heartRate, normalValues.oxygenLevel],
              backgroundColor: 'rgba(33, 150, 243, 0.6)', // Blue for normal
            }
          ]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          scales: {
            x: { 
              title: { 
                display: true, 
                text: 'Measurements',
                color: getComparisonAxisColor(),
                font: {
                  size: 18,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                }
              },
              ticks: { color: getComparisonAxisColor() }
            },
            y: { 
              beginAtZero: true, 
              title: { 
                display: true, 
                text: 'Values',
                color: getComparisonAxisColor(),
                font: {
                  size: 18,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                }
              },
              ticks: { color: getComparisonAxisColor() }
            }
          },
          plugins: {
            legend: {
              labels: {
                color: getLegendColor(),
                font: {
                  size: 16,
                  family: 'Arial, sans-serif'
                }
              }
            },
          },
        },
      });
    }

    // Function to update chart colors based on theme
    function updateChartColors() {
      // Update Pie Chart Legend
      if (pieChart) {
        pieChart.options.plugins.legend.labels.color = getLegendColor();
        pieChart.update();
      }

      // Update Radar Chart
      if (radarChart) {
        radarChart.options.scales.r.angleLines.color = getColor(document.body.classList.contains('dark-mode'));
        radarChart.options.scales.r.grid.color = getRadarGridColor();
        radarChart.options.scales.r.pointLabels.color = getRadarPointLabelsColor();
        radarChart.options.plugins.legend.labels.color = getLegendColor();
        radarChart.update();
      }

      // Update Comparison Chart
      if (comparisonChart) {
        comparisonChart.options.scales.x.title.color = getComparisonAxisColor();
        comparisonChart.options.scales.x.ticks.color = getComparisonAxisColor();
        comparisonChart.options.scales.y.title.color = getComparisonAxisColor();
        comparisonChart.options.scales.y.ticks.color = getComparisonAxisColor();
        comparisonChart.options.plugins.legend.labels.color = getLegendColor();
        comparisonChart.update();
      }
    }
  </script>

</body>
</html>
)rawliteral";

  request->send(200, "text/html", html);
});




// Serve the historical data for temperature page
server.on("/historical-temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang='en'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Historical Temperature Data</title>

  <!-- Include Chart.js for graphing -->
  <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>

  <!-- Include CSS styles matching the existing layout and look -->
  <style>
    /* Base Styles */
    body {
      font-family: Arial, sans-serif;
      background-color: #f5f5f5; /* Light Gray for main background */
      color: #2c3e50; /* Dark Slate Blue for primary text */
      margin: 0;
      padding: 0;
      transition: background-color 0.5s, color 0.5s;
    }

    .container {
      display: flex;
      min-height: 100vh;
      padding-left: 200px; /* Adjust padding-left to prevent sidebar overlap */
      transition: padding-left 0.5s;
    }

    .main-content {
      flex-grow: 1;
      padding: 20px;
      background-color: #f5f5f5; /* Light Gray for main content */
      transition: background-color 0.5s;
    }

    .sidebar {
      position: fixed;
      top: 0;
      left: 0;
      height: 100vh;
      width: 200px;
      background-color: #34495e; /* Dark Blue-Gray for sidebar */
      padding-top: 20px;
      z-index: 10;
      transition: background-color 0.5s;
    }

    .sidebar button {
      display: block;
      background-color: transparent;
      color: white;
      padding: 10px 20px;
      margin: 10px;
      border: none;
      text-align: left;
      width: 80%;
      font-size: 1em;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .sidebar button:hover {
      background-color: #1abc9c; /* Teal for hover effect */
    }

    h1 {
      font-size: 48px; /* Reduced font size for better responsiveness */
      color: #2c3e50; /* Dark Slate Blue for headers */
      transition: color 0.5s;
      margin-top: 0;
    }

    h2 {
      font-size: 24px;
      color: #2c3e50; /* Dark Slate Blue for sub-headers */
      transition: color 0.5s;
      margin-bottom: 10px;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      margin: 20px 0;
    }

    th, td {
      padding: 10px;
      text-align: left;
      border-bottom: 1px solid #ddd;
    }

    th {
      background-color: #34495e; /* Dark Blue-Gray to match sidebar */
      color: white;
    }

    td {
      color: #2c3e50; /* Dark Slate Blue for table data */
    }

    .chart-container {
      width: 100%;
      max-width: 800px;
      margin: 20px auto;
    }

    .theme-toggle {
      position: fixed;
      top: 10px;
      right: 10px;
      background-color: #3498db; /* Bright Blue for theme toggle */
      color: white;
      padding: 8px 12px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .theme-toggle:hover {
      background-color: #2980b9; /* Darker Blue for hover effect */
    }

    /* Dark mode styles */
    .dark-mode body {
      background-color: #555555; /* Neutral Grey for body background */
      color: #ecf0f1; /* Light gray for text */
    }

    .dark-mode .container {
      background-color: #555555; /* Match body background */
    }

    .dark-mode .main-content {
      background-color: #444444; /* Darker Grey for main content */
    }

    .dark-mode .sidebar {
      background-color: #444444; /* Dark Blue-Gray for sidebar in dark mode */
    }

    .dark-mode .sidebar button {
      background-color: #3c4f6c; /* Muted Navy for sidebar buttons */
      color: #ecf0f1; /* Light gray for button text */
    }

    .dark-mode .sidebar button:hover {
      background-color: #1abc9c; /* Teal on hover */
    }

    .dark-mode h1, .dark-mode h2 {
      color: #ecf0f1; /* Light gray for headers */
    }

    .dark-mode p {
      color: #bdc3c7; /* Lighter gray for paragraphs */
    }

    .dark-mode .data {
      color: #ffffff; /* White for highlighted data */
    }

    .dark-mode button {
      background-color: #1abc9c; /* Teal for buttons in dark mode */
      color: white;
    }

    .dark-mode button:hover {
      background-color: #16a085; /* Darker Teal on hover */
    }

    .dark-mode table th {
      background-color: #34495e; /* Dark Blue-Gray to match sidebar in dark mode */
      color: #ffffff; /* White text for table headers */
    }

    .dark-mode table td {
      color: #ecf0f1; /* Light Gray for table data in dark mode */
    }

    /* Print Styles */
    @media print {
      body {
        background-color: #ffffff; /* White background for printing */
        color: #000000; /* Black text for better contrast */
        font-size: 12pt; /* Standardize font size for print */
      }

      /* Hide interactive and non-essential elements */
      .sidebar, .theme-toggle, button, .sensor-card, .dashboard-container, canvas, table, .chart-container {
        display: none;
      }

      /* Adjust main content layout */
      .container {
        flex-direction: column;
        padding-left: 0;
      }

      .main-content {
        background-color: #ffffff; /* Ensure main content has white background */
        padding: 20px;
      }

      /* Adjust headings and text */
      h1 {
        font-size: 24pt;
        color: #000000;
        margin-bottom: 10px;
      }

      h2 {
        font-size: 18pt;
        color: #000000;
      }

      p, .data {
        font-size: 14pt;
        color: #000000;
      }

      /* Ensure links are visible with URLs */
      a::after {
        content: " (" attr(href) ")";
        color: #000000;
        font-size: 10pt;
      }

      /* Remove box shadows and borders for a cleaner print */
      table {
        border: none;
      }

      th, td {
        border-bottom: 1px solid #ddd;
      }

      /* Page breaks for better pagination */
      h1, h2, h3 {
        page-break-after: avoid;
      }

      /* Ensure images are appropriately sized or hidden if decorative */
      img {
        max-width: 100%;
        height: auto;
      }
    }
  </style>

</head>
<body>

  <!-- Light/Dark mode toggle button on the right -->
  <button class='theme-toggle' onclick='toggleTheme()'>Toggle Light/Dark Mode</button>

  <!-- Container that holds both the sidebar and main content -->
  <div class='container'>

    <!-- Sidebar with navigation buttons -->
    <div class='sidebar'>
      <button onclick="window.location.href='/start'">Dashboard</button>
      <button onclick="window.location.href='/dashboard'">Detail View</button>
      <button onclick="window.location.href='/healthinfo'">Health Information</button>
      <button onclick="window.location.href='/historical'">Historical Mode</button>
      <button onclick="window.location.href='/historical-oxygen'">Historical Oxygen</button>
    </div>

    <!-- Main content area -->
    <div class='main-content'>
      <h1>Historical Temperature Data</h1>

      <!-- Add a section for the table -->
      <h2>Last 10 Temperature Measurements</h2>
      <table>
        <thead>
          <tr><th>Entry</th><th>Temperature (°C)</th><th>Date/Time</th></tr>
        </thead>
        <tbody id='temperature-table'></tbody>
      </table>

      <!-- Add a section for the chart -->
      <h2>Temperature Overview</h2>
      <div class='chart-container'>
        <canvas id='temperatureChart'></canvas>
      </div>

    </div> <!-- Close main-content -->
  </div> <!-- Close container -->

  <!-- JavaScript code to display the table and chart -->
  <script>
    // Function to get axis color based on theme
    function getAxisColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#2c3e50'; /* Dark Slate Blue */
    }

    // Function to get legend color based on theme
    function getLegendColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#2c3e50'; /* Dark Slate Blue */
    }

    // Function to toggle light/dark theme
    function toggleTheme() {
      document.body.classList.toggle('dark-mode');
      if (document.body.classList.contains('dark-mode')) {
        localStorage.setItem('theme', 'dark');
      } else {
        localStorage.setItem('theme', 'light');
      }
      updateChartColors(); // Update chart colors when theme changes
    }

    // Apply saved theme on load
    document.addEventListener('DOMContentLoaded', (event) => {
      const theme = localStorage.getItem('theme');
      if (theme === 'dark') {
        document.body.classList.add('dark-mode');
      }
      initializeChart(); // Initialize chart after theme is set
    });

    let temperatureChart; // Global chart variable

    // Function to initialize the Chart.js line chart
    function initializeChart() {
      const ctx = document.getElementById('temperatureChart').getContext('2d');
      temperatureChart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: [], // Will be populated with timestamps
          datasets: [{
            label: 'Temperature (°C)',
            data: [], // Will be populated with temperature data
            backgroundColor: 'rgba(52, 152, 219, 0.2)', /* Bright Blue */
            borderColor: 'rgba(52, 152, 219, 1)', /* Bright Blue */
            borderWidth: 2,
            fill: true,
            tension: 0.4, // Smooth curves
          }]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          scales: {
            x: { 
              title: { 
                display: true, 
                text: 'Time',
                color: getAxisColor(),
                font: {
                  size: 16,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                }
              },
              ticks: {
                color: getAxisColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            },
            y: { 
              title: { 
                display: true, 
                text: 'Temperature (°C)',
                color: getAxisColor(),
                font: {
                  size: 16,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                }
              },
              ticks: {
                color: getAxisColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              },
              beginAtZero: false
            }
          },
          plugins: {
            legend: {
              labels: {
                color: getLegendColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            }
          }
        }
      });

      // Fetch and populate data
      fetchTemperatureData();
    }

    // Function to fetch temperature data and populate table and chart
    function fetchTemperatureData() {
      fetch('/getLastTenTemperatures')
        .then(response => response.json())
        .then(data => {
          const table = document.getElementById('temperature-table');
          let rows = '';

          // Populate the table with temperature data
          data.forEach((entry, index) => {
            rows += `<tr><td>${index + 1}</td><td>${entry.temperature}°C</td><td>${entry.timestamp}</td></tr>`;
          });
          table.innerHTML = rows;

          // Update the chart with temperature data
          temperatureChart.data.labels = data.map(entry => entry.timestamp);
          temperatureChart.data.datasets[0].data = data.map(entry => entry.temperature);
          temperatureChart.update();
        })
        .catch(error => console.log('Error fetching data:', error));
    }

    // Function to update chart colors based on theme
    function updateChartColors() {
      if (temperatureChart) {
        // Update axis titles and ticks
        temperatureChart.options.scales.x.title.color = getAxisColor();
        temperatureChart.options.scales.x.ticks.color = getAxisColor();
        temperatureChart.options.scales.y.title.color = getAxisColor();
        temperatureChart.options.scales.y.ticks.color = getAxisColor();

        // Update legend labels
        temperatureChart.options.plugins.legend.labels.color = getLegendColor();

        temperatureChart.update();
      }

      // Update table header and data colors based on theme
      if (document.body.classList.contains('dark-mode')) {
        // Update table headers
        document.querySelectorAll('th').forEach(th => {
          th.style.backgroundColor = '#34495e'; /* Dark Blue-Gray to match sidebar */
          th.style.color = 'white'; /* White text for table headers */
        });
        // Update table data
        document.querySelectorAll('td').forEach(td => {
          td.style.color = '#ecf0f1'; /* Light Gray for table data in dark mode */
        });
      } else {
        // Reset table headers to original colors
        document.querySelectorAll('th').forEach(th => {
          th.style.backgroundColor = '#34495e'; /* Dark Blue-Gray to match sidebar */
          th.style.color = 'white'; /* White text for table headers */
        });
        // Reset table data to original colors
        document.querySelectorAll('td').forEach(td => {
          td.style.color = '#2c3e50'; /* Dark Slate Blue for table data */
        });
      }
    }

    // Listen for theme toggle to update chart colors
    document.querySelector('.theme-toggle').addEventListener('click', function() {
      // Delay to allow theme toggle transition before updating colors
      setTimeout(updateChartColors, 500);
    });
  </script>

</body>
</html>
)rawliteral";

  request->send(200, "text/html", html);
});



// Serve the historical data for oxygen level page
server.on("/historical-oxygen", HTTP_GET, [](AsyncWebServerRequest *request) {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang='en'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Historical Oxygen Level Data</title>

  <!-- Include Chart.js for graphing -->
  <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>

  <!-- Include CSS styles matching the existing layout and look -->
  <style>
    /* Base Styles */
    body {
      font-family: Arial, sans-serif;
      background-color: #f5f5f5; /* Light Gray for main background */
      color: #2c3e50; /* Dark Slate Blue for primary text */
      margin: 0;
      padding: 0;
      transition: background-color 0.5s, color 0.5s;
    }

    .container {
      display: flex;
      min-height: 100vh;
      padding-left: 200px; /* Adjust padding-left to prevent sidebar overlap */
      transition: padding-left 0.5s;
    }

    .main-content {
      flex-grow: 1;
      padding: 20px;
      background-color: #f5f5f5; /* Light Gray for main content */
      transition: background-color 0.5s;
    }

    .sidebar {
      position: fixed;
      top: 0;
      left: 0;
      height: 100vh;
      width: 200px;
      background-color: #34495e; /* Dark Blue-Gray for sidebar */
      padding-top: 20px;
      z-index: 10;
      transition: background-color 0.5s;
    }

    .sidebar button {
      display: block;
      background-color: transparent;
      color: white;
      padding: 10px 20px;
      margin: 10px;
      border: none;
      text-align: left;
      width: 80%;
      font-size: 1em;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .sidebar button:hover {
      background-color: #1abc9c; /* Teal for hover effect */
    }

    h1 {
      font-size: 48px; /* Reduced font size for better responsiveness */
      color: #2c3e50; /* Dark Slate Blue for headers */
      transition: color 0.5s;
      margin-top: 0;
    }

    h2 {
      font-size: 24px;
      color: #2c3e50; /* Dark Slate Blue for sub-headers */
      transition: color 0.5s;
      margin-bottom: 10px;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      margin: 20px 0;
    }

    th, td {
      padding: 10px;
      text-align: left;
      border-bottom: 1px solid #ddd;
    }

    th {
      background-color: #34495e; /* Dark Blue-Gray to match sidebar */
      color: white;
    }

    td {
      color: #2c3e50; /* Dark Slate Blue for table data */
    }

    .chart-container {
      width: 100%;
      max-width: 800px;
      margin: 20px auto;
    }

    .theme-toggle {
      position: fixed;
      top: 10px;
      right: 10px;
      background-color: #3498db; /* Bright Blue for theme toggle */
      color: white;
      padding: 8px 12px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .theme-toggle:hover {
      background-color: #2980b9; /* Darker Blue for hover effect */
    }

    /* Dark mode styles */
    .dark-mode body {
      background-color: #555555; /* Neutral Grey for body background */
      color: #ecf0f1; /* Light gray for text */
    }

    .dark-mode .container {
      background-color: #555555; /* Match body background */
    }

    .dark-mode .main-content {
      background-color: #444444; /* Darker Grey for main content */
    }

    .dark-mode .sidebar {
      background-color: #444444; /* Dark Blue-Gray for sidebar in dark mode */
    }

    .dark-mode .sidebar button {
      background-color: #3c4f6c; /* Muted Navy for sidebar buttons */
      color: #ecf0f1; /* Light gray for button text */
    }

    .dark-mode .sidebar button:hover {
      background-color: #1abc9c; /* Teal on hover */
    }

    .dark-mode h1, .dark-mode h2 {
      color: #ecf0f1; /* Light gray for headers */
    }

    .dark-mode p {
      color: #bdc3c7; /* Lighter gray for paragraphs */
    }

    .dark-mode .data {
      color: #ffffff; /* White for highlighted data */
    }

    .dark-mode button {
      background-color: #1abc9c; /* Teal for buttons in dark mode */
      color: white;
    }

    .dark-mode button:hover {
      background-color: #16a085; /* Darker Teal on hover */
    }

    .dark-mode table th {
      background-color: #34495e; /* Dark Blue-Gray to match sidebar in dark mode */
      color: #ffffff; /* White text for table headers */
    }

    .dark-mode table td {
      color: #ecf0f1; /* Light Gray for table data in dark mode */
    }

    /* Print Styles */
    @media print {
      body {
        background-color: #ffffff; /* White background for printing */
        color: #000000; /* Black text for better contrast */
        font-size: 12pt; /* Standardize font size for print */
      }

      /* Hide interactive and non-essential elements */
      .sidebar, .theme-toggle, button, .sensor-card, .dashboard-container, canvas, table, .chart-container {
        display: none;
      }

      /* Adjust main content layout */
      .container {
        flex-direction: column;
        padding-left: 0;
      }

      .main-content {
        background-color: #ffffff; /* Ensure main content has white background */
        padding: 20px;
      }

      /* Adjust headings and text */
      h1 {
        font-size: 24pt;
        color: #000000;
        margin-bottom: 10px;
      }

      h2 {
        font-size: 18pt;
        color: #000000;
      }

      p, .data {
        font-size: 14pt;
        color: #000000;
      }

      /* Ensure links are visible with URLs */
      a::after {
        content: " (" attr(href) ")";
        color: #000000;
        font-size: 10pt;
      }

      /* Remove box shadows and borders for a cleaner print */
      table {
        border: none;
      }

      th, td {
        border-bottom: 1px solid #ddd;
      }

      /* Page breaks for better pagination */
      h1, h2, h3 {
        page-break-after: avoid;
      }

      /* Ensure images are appropriately sized or hidden if decorative */
      img {
        max-width: 100%;
        height: auto;
      }
    }
  </style>

</head>
<body>

  <!-- Light/Dark mode toggle button on the right -->
  <button class='theme-toggle' onclick='toggleTheme()'>Toggle Light/Dark Mode</button>

  <!-- Container that holds both the sidebar and main content -->
  <div class='container'>

    <!-- Sidebar with navigation buttons -->
    <div class='sidebar'>
      <button onclick="window.location.href='/start'">Dashboard</button>
      <button onclick="window.location.href='/dashboard'">Detail View</button>
      <button onclick='window.location.href="/healthinfo"'>Health Information</button> 
      <button onclick="window.location.href='/historical'">Historical Mode</button>
      <button onclick='window.location.href="/historical-temperature"'>Historical Temperature</button> 
    </div>

    <!-- Main content area -->
    <div class='main-content'>
      <h1>Historical Oxygen Level Data</h1>

      <!-- Add a section for the table -->
      <h2>Last 10 Oxygen Level Measurements</h2>
      <table>
        <thead>
          <tr><th>Entry</th><th>Oxygen Level (%)</th><th>Date/Time</th></tr>
        </thead>
        <tbody id='oxygen-table'></tbody>
      </table>

      <!-- Add a section for the chart -->
      <h2>Oxygen Level Overview</h2>
      <div class='chart-container'><canvas id='oxygenChart'></canvas></div>

    </div> <!-- Close main-content -->
  </div> <!-- Close container -->

  <!-- JavaScript code to display the table and chart -->
  <script>
    // Function to get axis color based on theme
    function getAxisColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#2c3e50'; /* Dark Slate Blue */
    }

    // Function to get legend color based on theme
    function getLegendColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#2c3e50'; /* Dark Slate Blue */
    }

    // Function to toggle light/dark theme
    function toggleTheme() {
      document.body.classList.toggle('dark-mode');
      if (document.body.classList.contains('dark-mode')) {
        localStorage.setItem('theme', 'dark');
      } else {
        localStorage.setItem('theme', 'light');
      }
      updateChartColors(); // Update chart colors when theme changes
    }

    // Apply saved theme on load
    document.addEventListener('DOMContentLoaded', (event) => {
      const theme = localStorage.getItem('theme');
      if (theme === 'dark') {
        document.body.classList.add('dark-mode');
      }
      initializeChart(); // Initialize chart after theme is set
    });

    let oxygenChart; // Global chart variable

    // Function to initialize the Chart.js line chart
    function initializeChart() {
      const ctx = document.getElementById('oxygenChart').getContext('2d');
      oxygenChart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: [], // Will be populated with timestamps
          datasets: [{
            label: 'Oxygen Level (%)',
            data: [], // Will be populated with oxygen level data
            backgroundColor: 'rgba(52, 152, 219, 0.2)', /* Bright Blue */
            borderColor: 'rgba(52, 152, 219, 1)', /* Bright Blue */
            borderWidth: 2,
            fill: true,
            tension: 0.4, // Smooth curves
          }]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          scales: {
            x: { 
              title: { 
                display: true, 
                text: 'Time',
                color: getAxisColor(),
                font: {
                  size: 16,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                }
              },
              ticks: {
                color: getAxisColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            },
            y: { 
              title: { 
                display: true, 
                text: 'Oxygen Level (%)',
                color: getAxisColor(),
                font: {
                  size: 16,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                }
              },
              ticks: {
                color: getAxisColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              },
              beginAtZero: false
            }
          },
          plugins: {
            legend: {
              labels: {
                color: getLegendColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            }
          }
        }
      });

      // Fetch and populate data
      fetchOxygenData();
    }

    // Function to fetch oxygen data and populate table and chart
    function fetchOxygenData() {
      fetch('/getLastTenOxygenLevels')
        .then(response => response.json())
        .then(data => {
          const table = document.getElementById('oxygen-table');
          let rows = '';

          // Populate the table with oxygen level data
          data.forEach((entry, index) => {
            rows += `<tr><td>${index + 1}</td><td>${entry.oxygen_level}%</td><td>${entry.timestamp}</td></tr>`;
          });
          table.innerHTML = rows;

          // Update the chart with oxygen level data
          oxygenChart.data.labels = data.map(entry => entry.timestamp);
          oxygenChart.data.datasets[0].data = data.map(entry => entry.oxygen_level);
          oxygenChart.update();
        })
        .catch(error => console.log('Error fetching data:', error));
    }

    // Function to update chart colors based on theme
    function updateChartColors() {
      if (oxygenChart) {
        // Update axis titles and ticks
        oxygenChart.options.scales.x.title.color = getAxisColor();
        oxygenChart.options.scales.x.ticks.color = getAxisColor();
        oxygenChart.options.scales.y.title.color = getAxisColor();
        oxygenChart.options.scales.y.ticks.color = getAxisColor();

        // Update legend labels
        oxygenChart.options.plugins.legend.labels.color = getLegendColor();

        oxygenChart.update();
      }

      // Update table header and data colors based on theme
      if (document.body.classList.contains('dark-mode')) {
        // Update table headers
        document.querySelectorAll('th').forEach(th => {
          th.style.backgroundColor = '#34495e'; /* Dark Blue-Gray to match sidebar */
          th.style.color = 'white'; /* White text for table headers */
        });
        // Update table data
        document.querySelectorAll('td').forEach(td => {
          td.style.color = '#ecf0f1'; /* Light Gray for table data in dark mode */
        });
      } else {
        // Reset table headers to original colors
        document.querySelectorAll('th').forEach(th => {
          th.style.backgroundColor = '#34495e'; /* Dark Blue-Gray to match sidebar */
          th.style.color = 'white'; /* White text for table headers */
        });
        // Reset table data to original colors
        document.querySelectorAll('td').forEach(td => {
          td.style.color = '#2c3e50'; /* Dark Slate Blue for table data */
        });
      }
    }

    // Listen for theme toggle to update chart colors
    document.querySelector('.theme-toggle').addEventListener('click', function() {
      // Delay to allow theme toggle transition before updating colors
      setTimeout(updateChartColors, 500);
    });
  </script>

</body>
</html>
)rawliteral";

  request->send(200, "text/html", html);
});



// Serve the historical data for heart rate page
server.on("/historical-heartrate", HTTP_GET, [](AsyncWebServerRequest *request) {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang='en'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Historical Heart Rate Data</title>

  <!-- Include Chart.js for graphing -->
  <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>

  <!-- Include CSS styles matching the existing layout and look -->
  <style>
    /* Base Styles */
    body {
      font-family: Arial, sans-serif;
      background-color: #f5f5f5; /* Light Gray for main background */
      color: #2c3e50; /* Dark Slate Blue for primary text */
      margin: 0;
      padding: 0;
      transition: background-color 0.5s, color 0.5s;
    }

    .container {
      display: flex;
      min-height: 100vh;
      padding-left: 200px; /* Adjust padding-left to prevent sidebar overlap */
      transition: padding-left 0.5s;
    }

    .main-content {
      flex-grow: 1;
      padding: 20px;
      background-color: #f5f5f5; /* Light Gray for main content */
      transition: background-color 0.5s;
    }

    .sidebar {
      position: fixed;
      top: 0;
      left: 0;
      height: 100vh;
      width: 200px;
      background-color: #34495e; /* Dark Blue-Gray for sidebar */
      padding-top: 20px;
      z-index: 10;
      transition: background-color 0.5s;
    }

    .sidebar button {
      display: block;
      background-color: transparent;
      color: white;
      padding: 10px 20px;
      margin: 10px;
      border: none;
      text-align: left;
      width: 80%;
      font-size: 1em;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .sidebar button:hover {
      background-color: #1abc9c; /* Teal for hover effect */
    }

    h1 {
      font-size: 48px; /* Reduced font size for better responsiveness */
      color: #2c3e50; /* Dark Slate Blue for headers */
      transition: color 0.5s;
      margin-top: 0;
    }

    h2 {
      font-size: 24px;
      color: #2c3e50; /* Dark Slate Blue for sub-headers */
      transition: color 0.5s;
      margin-bottom: 10px;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      margin: 20px 0;
    }

    th, td {
      padding: 10px;
      text-align: left;
      border-bottom: 1px solid #ddd;
    }

    th {
      background-color: #34495e; /* Dark Blue-Gray to match sidebar */
      color: white;
    }

    td {
      color: #2c3e50; /* Dark Slate Blue for table data */
    }

    .chart-container {
      width: 100%;
      max-width: 800px;
      margin: 20px auto;
    }

    .theme-toggle {
      position: fixed;
      top: 10px;
      right: 10px;
      background-color: #3498db; /* Bright Blue for theme toggle */
      color: white;
      padding: 8px 12px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .theme-toggle:hover {
      background-color: #2980b9; /* Darker Blue for hover effect */
    }

    /* Dark mode styles */
    .dark-mode body {
      background-color: #555555; /* Neutral Grey for body background */
      color: #ecf0f1; /* Light gray for text */
    }

    .dark-mode .container {
      background-color: #555555; /* Match body background */
    }

    .dark-mode .main-content {
      background-color: #444444; /* Darker Grey for main content */
    }

    .dark-mode .sidebar {
      background-color: #444444; /* Dark Blue-Gray for sidebar in dark mode */
    }

    .dark-mode .sidebar button {
      background-color: #3c4f6c; /* Muted Navy for sidebar buttons */
      color: #ecf0f1; /* Light gray for button text */
    }

    .dark-mode .sidebar button:hover {
      background-color: #1abc9c; /* Teal on hover */
    }

    .dark-mode h1, .dark-mode h2 {
      color: #ecf0f1; /* Light gray for headers */
    }

    .dark-mode p {
      color: #bdc3c7; /* Lighter gray for paragraphs */
    }

    .dark-mode .data {
      color: #ffffff; /* White for highlighted data */
    }

    .dark-mode button {
      background-color: #1abc9c; /* Teal for buttons in dark mode */
      color: white;
    }

    .dark-mode button:hover {
      background-color: #16a085; /* Darker Teal on hover */
    }

    .dark-mode table th {
      background-color: #34495e; /* Dark Blue-Gray to match sidebar in dark mode */
      color: #ffffff; /* White text for table headers */
    }

    .dark-mode table td {
      color: #ecf0f1; /* Light Gray for table data in dark mode */
    }

    /* Print Styles */
    @media print {
      body {
        background-color: #ffffff; /* White background for printing */
        color: #000000; /* Black text for better contrast */
        font-size: 12pt; /* Standardize font size for print */
      }

      /* Hide interactive and non-essential elements */
      .sidebar, .theme-toggle, button, .sensor-card, .dashboard-container, canvas, table, .chart-container {
        display: none;
      }

      /* Adjust main content layout */
      .container {
        flex-direction: column;
        padding-left: 0;
      }

      .main-content {
        background-color: #ffffff; /* Ensure main content has white background */
        padding: 20px;
      }

      /* Adjust headings and text */
      h1 {
        font-size: 24pt;
        color: #000000;
        margin-bottom: 10px;
      }

      h2 {
        font-size: 18pt;
        color: #000000;
      }

      p, .data {
        font-size: 14pt;
        color: #000000;
      }

      /* Ensure links are visible with URLs */
      a::after {
        content: " (" attr(href) ")";
        color: #000000;
        font-size: 10pt;
      }

      /* Remove box shadows and borders for a cleaner print */
      table {
        border: none;
      }

      th, td {
        border-bottom: 1px solid #ddd;
      }

      /* Page breaks for better pagination */
      h1, h2, h3 {
        page-break-after: avoid;
      }

      /* Ensure images are appropriately sized or hidden if decorative */
      img {
        max-width: 100%;
        height: auto;
      }
    }
  </style>

</head>
<body>

  <!-- Light/Dark mode toggle button on the right -->
  <button class='theme-toggle' onclick='toggleTheme()'>Toggle Light/Dark Mode</button>

  <!-- Container that holds both the sidebar and main content -->
  <div class='container'>

    <!-- Sidebar with navigation buttons -->
    <div class='sidebar'>
      <button onclick="window.location.href='/start'">Dashboard</button>
      <button onclick="window.location.href='/dashboard'">Detail View</button>
      <button onclick='window.location.href="/healthinfo"'>Health Information</button> 
      <button onclick="window.location.href='/historical'">Historical Mode</button>
      <button onclick='window.location.href="/historical-temperature"'>Historical Temperature</button> 
      <button onclick='window.location.href="/historical-oxygen"'>Historical Oxygen Level</button>
    </div>

    <!-- Main content area -->
    <div class='main-content'>
      <h1>Historical Heart Rate Data</h1>

      <!-- Add a section for the table -->
      <h2>Last 10 Heart Rate Measurements</h2>
      <table>
        <thead>
          <tr><th>Entry</th><th>Heart Rate (BPM)</th><th>Date/Time</th></tr>
        </thead>
        <tbody id='heartrate-table'></tbody>
      </table>

      <!-- Add a section for the chart -->
      <h2>Heart Rate Overview</h2>
      <div class='chart-container'><canvas id='heartrateChart'></canvas></div>

    </div> <!-- Close main-content -->
  </div> <!-- Close container -->

  <!-- JavaScript code to display the table and chart -->
  <script>
    // Function to get axis color based on theme
    function getAxisColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#2c3e50'; /* Dark Slate Blue */
    }

    // Function to get legend color based on theme
    function getLegendColor() {
      return document.body.classList.contains('dark-mode') ? '#ffffff' : '#2c3e50'; /* Dark Slate Blue */
    }

    // Function to toggle light/dark theme
    function toggleTheme() {
      document.body.classList.toggle('dark-mode');
      if (document.body.classList.contains('dark-mode')) {
        localStorage.setItem('theme', 'dark');
      } else {
        localStorage.setItem('theme', 'light');
      }
      updateChartColors(); // Update chart colors when theme changes
    }

    // Apply saved theme on load
    document.addEventListener('DOMContentLoaded', (event) => {
      const theme = localStorage.getItem('theme');
      if (theme === 'dark') {
        document.body.classList.add('dark-mode');
      }
      initializeChart(); // Initialize chart after theme is set
    });

    let heartrateChart; // Global chart variable

    // Function to initialize the Chart.js line chart
    function initializeChart() {
      const ctx = document.getElementById('heartrateChart').getContext('2d');
      heartrateChart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: [], // Will be populated with timestamps
          datasets: [{
            label: 'Heart Rate (BPM)',
            data: [], // Will be populated with heart rate data
            backgroundColor: 'rgba(52, 152, 219, 0.2)', /* Bright Blue */
            borderColor: 'rgba(52, 152, 219, 1)', /* Bright Blue */
            borderWidth: 2,
            fill: true,
            tension: 0.4, // Smooth curves
          }]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          scales: {
            x: { 
              title: { 
                display: true, 
                text: 'Time',
                color: getAxisColor(),
                font: {
                  size: 16,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                }
              },
              ticks: {
                color: getAxisColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            },
            y: { 
              title: { 
                display: true, 
                text: 'Heart Rate (BPM)',
                color: getAxisColor(),
                font: {
                  size: 16,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                }
              },
              ticks: {
                color: getAxisColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              },
              beginAtZero: false
            }
          },
          plugins: {
            legend: {
              labels: {
                color: getLegendColor(),
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            }
          }
        }
      });

      // Fetch and populate data
      fetchHeartRateData();
    }

    // Function to fetch heart rate data and populate table and chart
    function fetchHeartRateData() {
      fetch('/getLastTenHeartRates')
        .then(response => response.json())
        .then(data => {
          const table = document.getElementById('heartrate-table');
          let rows = '';

          // Populate the table with heart rate data
          data.forEach((entry, index) => {
            rows += `<tr><td>${index + 1}</td><td>${entry.heart_rate} BPM</td><td>${entry.timestamp}</td></tr>`;
          });
          table.innerHTML = rows;

          // Update the chart with heart rate data
          heartrateChart.data.labels = data.map(entry => entry.timestamp);
          heartrateChart.data.datasets[0].data = data.map(entry => entry.heart_rate);
          heartrateChart.update();
        })
        .catch(error => console.log('Error fetching data:', error));
    }

    // Function to update chart colors based on theme
    function updateChartColors() {
      if (heartrateChart) {
        // Update axis titles and ticks
        heartrateChart.options.scales.x.title.color = getAxisColor();
        heartrateChart.options.scales.x.ticks.color = getAxisColor();
        heartrateChart.options.scales.y.title.color = getAxisColor();
        heartrateChart.options.scales.y.ticks.color = getAxisColor();

        // Update legend labels
        heartrateChart.options.plugins.legend.labels.color = getLegendColor();

        heartrateChart.update();
      }

      // Update table header and data colors based on theme
      if (document.body.classList.contains('dark-mode')) {
        // Update table headers
        document.querySelectorAll('th').forEach(th => {
          th.style.backgroundColor = '#34495e'; /* Dark Blue-Gray to match sidebar */
          th.style.color = 'white'; /* White text for table headers */
        });
        // Update table data
        document.querySelectorAll('td').forEach(td => {
          td.style.color = '#ecf0f1'; /* Light Gray for table data in dark mode */
        });
      } else {
        // Reset table headers to original colors
        document.querySelectorAll('th').forEach(th => {
          th.style.backgroundColor = '#34495e'; /* Dark Blue-Gray to match sidebar */
          th.style.color = 'white'; /* White text for table headers */
        });
        // Reset table data to original colors
        document.querySelectorAll('td').forEach(td => {
          td.style.color = '#2c3e50'; /* Dark Slate Blue for table data */
        });
      }
    }

    // Listen for theme toggle to update chart colors
    document.querySelector('.theme-toggle').addEventListener('click', function() {
      // Delay to allow theme toggle transition before updating colors
      setTimeout(updateChartColors, 500);
    });
  </script>

</body>
</html>
)rawliteral";

  request->send(200, "text/html", html);
});






}
 
 // Serve the dashboard page
server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request){  
    timeClient.update();  // Update time from NTP server
    String currentTime = timeClient.getFormattedTime(); // Get current time
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Wellness Monitor Dashboard</title>

  <!-- Include Chart.js -->
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

  <style>
    /* Base Styles */
    body {
      font-family: Arial, sans-serif;
      background-color: #f5f5f5; /* Light Gray for main background */
      margin: 0;
      padding: 0;
      transition: background-color 0.5s, color 0.5s;
      color: #2c3e50; /* Dark Slate Blue for primary text */
    }

    .container {
      display: flex;
      min-height: 100vh;
      padding-left: 200px; /* Adjust padding-left to prevent sidebar overlap */
      transition: padding-left 0.5s;
    }

    .main-content {
      flex-grow: 1;
      padding: 20px;
      background-color: #f5f5f5; /* Light Gray for main content */
      transition: background-color 0.5s;
    }

    .sidebar {
      position: fixed;
      top: 0;
      left: 0;
      height: 100vh;
      width: 200px;
      background-color: #34495e; /* Dark Blue-Gray for sidebar */
      padding-top: 20px;
      transition: background-color 0.5s;
      z-index: 10;
    }

    .sidebar button {
      display: block;
      background-color: transparent;
      color: white;
      padding: 10px 20px;
      margin: 10px;
      border: none;
      text-align: left;
      width: 80%;
      font-size: 1em;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .sidebar button:hover {
      background-color: #1abc9c; /* Teal for hover effect */
    }

    h1 {
      font-size: 48px; /* Reduced font size for better responsiveness */
      color: #2c3e50; /* Dark Slate Blue for headers */
      transition: color 0.5s;
      margin-top: 0;
    }

    p {
      font-size: 20px; /* Reduced font size for better readability */
      color: #7f8c8d; /* Gray for regular paragraphs */
      margin: 0;
      transition: color 0.5s;
    }

    .data {
      font-size: 24px; /* Increased font size for data points */
      color: #2980b9; /* Bright Blue for highlighted data */
      transition: color 0.5s;
    }

    button {
      background-color: #1abc9c; /* Teal for primary buttons */
      color: white;
      padding: 10px 20px;
      border: none;
      border-radius: 5px;
      font-size: 1em;
      cursor: pointer;
      margin-top: 20px;
      transition: background-color 0.3s;
    }

    button:hover {
      background-color: #16a085; /* Darker Teal for hover effect */
    }

    .theme-toggle {
      position: fixed;
      top: 10px;
      right: 10px;
      background-color: #3498db; /* Bright Blue for theme toggle */
      color: white;
      padding: 8px 12px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      transition: background-color 0.3s;
    }

    .theme-toggle:hover {
      background-color: #2980b9; /* Darker Blue for hover effect */
    }

    .dashboard-container {
      display: flex;
      justify-content: space-around;
      align-items: center;
      background-color: #f5f5f5; /* Light Gray to match main background */
      margin-top: 20px;
      width: 100%;
      gap: 20px;
    }

    .sensor-card {
      width: 300px;
      padding: 20px;
      margin: 10px;
      background-color: white; /* White background for sensor cards */
      border-radius: 15px;
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); /* Subtle shadow */
      text-align: center;
      transition: background-color 0.3s, color 0.3s;
    }

    .sensor-card:hover {
      background-color: #ecf0f1; /* Slightly darker on hover */
    }

    .sensor-value {
      font-size: 48px;
      color: #2980b9; /* Bright Blue for sensor values */
    }

    /* Dark mode adjustments */
    .dark-mode body {
      background-color: #555555; /* Neutral Grey for body background */
      color: #ecf0f1; /* Light gray for text */
    }

    .dark-mode .container {
      background-color: #555555; /* Match body background */
    }

    .dark-mode .main-content {
      background-color: #444444; /* Darker Grey for main content */
    }

    .dark-mode .sidebar {
      background-color: #34495e; /* Dark Blue-Gray for sidebar in dark mode */
    }

    .dark-mode .sidebar button {
      background-color: #3c4f6c; /* Muted Navy for sidebar buttons */
      color: #ecf0f1; /* Light gray for button text */
    }

    .dark-mode .sidebar button:hover {
      background-color: #1abc9c; /* Teal on hover */
    }

    .dark-mode h1 {
      color: #ecf0f1; /* Light gray for headers */
    }

    .dark-mode p {
      color: #bdc3c7; /* Lighter gray for paragraphs */
    }

    .dark-mode .data {
      color: #000000; /* White for highlighted data */
    }

    .dark-mode button {
      background-color: #1abc9c; /* Teal for buttons in dark mode */
      color: white;
    }

    .dark-mode button:hover {
      background-color: #16a085; /* Darker Teal on hover */
    }

    .dark-mode .dashboard-container {
      background-color: #444444; /* Darker Grey to match main content */
    }

    .dark-mode .sensor-card {
      background-color: #3c4f6c; /* Muted Navy for sensor cards in dark mode */
      color: #ecf0f1; /* Light gray for text */
    }

    .dark-mode .sensor-value {
      color: #ffffff; /* White for sensor values */
    }

    /* Bell Body and Container Styles */
    .bell-container {
      position: absolute;
      top: 5px;
      right: 210px; /* Adjusted to prevent overlap with theme toggle */
      display: flex;
      flex-direction: column;
      align-items: center;
    }

    .bell-icon {
      cursor: pointer;
      width: 90px;
      height: 90px;
      object-fit: contain;
      margin-bottom: 10px;
    }

    /* Dropdown Styles */
    .dropdown {
      display: none;
      position: absolute;
      top: 80px;
      right: 0;
      background-color: #cce7ff; /* Light Blue for dropdown in light mode */
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
      width: 200px;
      border-radius: 5px;
      padding: 10px;
      z-index: 10;
      text-align: center;
    }

    .dropdown.show {
      display: block;
    }

    .dropdown p {
      margin: 0;
      font-size: 1.2em;
      color: #00796b;
    }

    /* Notification Styles */
    .notification-item {
      display: flex;
      align-items: center;
      padding: 10px;
      border-bottom: 1px solid #ccc;
    }

    .notification-icon {
      width: 80px;
      height: 80px;
      margin-right: 10px;
      display: flex;
      align-items: center;
      justify-content: center;
      border-radius: 50%;
    }

    .notification-details {
      flex-grow: 1;
    }

    .notification-title {
      font-size: 14px;
      font-weight: bold;
    }

    .notification-time {
      font-size: 12px;
      color: #999;
      text-align: right;
    }

    /* Dark mode adjustments for notifications */
    .dark-mode .dropdown {
      background-color: #333333; /* Dark background for dropdown in dark mode */
      color: white;
    }

    .dark-mode .notification-item {
      border-bottom: 1px solid #444444;
    }

    .dark-mode .notification-title {
      color: #dddddd;
    }

    .dark-mode .notification-time {
      color: #aaaaaa;
    }

    /* Light mode specific icon colors */
    .notification-icon.temperature {
      background-color: #ffebee; /* Light mode temperature icon color */
    }

    .notification-icon.heart-rate {
      background-color: #e3f2fd; /* Light mode heart rate icon color */
    }

    .notification-icon.oxygen {
      background-color: #e0f7fa; /* Light mode oxygen icon color */
    }

    /* Dark mode specific icon colors */
    .dark-mode .notification-icon.temperature {
      background-color: #ff7961; /* Dark mode temperature icon */
    }

    .dark-mode .notification-icon.heart-rate {
      background-color: #42a5f5; /* Dark mode heart rate icon */
    }

    .dark-mode .notification-icon.oxygen {
      background-color: #26c6da; /* Dark mode oxygen icon */
    }

    /* Alert Prompt Styles */
    .alert-prompt {
      position: fixed;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background-color: white;
      padding: 20px;
      border: 2px solid red;
      border-radius: 10px;
      display: none;
      z-index: 10;
    }

    .alert-prompt p {
      color: red;
      font-size: 1.2em;
    }

    .alert-prompt button {
      background-color: red;
      color: white;
      padding: 10px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
    }

    /* Responsive Chart Canvas */
    canvas {
      max-width: 100%;
      height: auto;
      margin: 20px 0;
    }

    /* Print Styles */
    @media print {
      body {
        background-color: #ffffff; /* White background for printing */
        color: #000000; /* Black text for better contrast */
        font-size: 12pt; /* Standardize font size for print */
      }

      /* Hide interactive and non-essential elements */
      .sidebar, .theme-toggle, button, .sensor-card, .dashboard-container, canvas, .bell-container, .alert-prompt, .dropdown {
        display: none;
      }

      /* Adjust main content layout */
      .container {
        flex-direction: column;
        padding-left: 0;
      }

      .main-content {
        background-color: #ffffff; /* Ensure main content has white background */
        padding: 20px;
      }

      /* Adjust headings and text */
      h1 {
        font-size: 24pt;
        color: #000000;
        margin-bottom: 10px;
      }

      p, .data {
        font-size: 14pt;
        color: #000000;
      }

      /* Ensure links are visible with URLs */
      a::after {
        content: " (" attr(href) ")";
        color: #000000;
        font-size: 10pt;
      }

      /* Remove box shadows and borders for a cleaner print */
      .sensor-card {
        box-shadow: none;
        border: none;
        background-color: transparent;
        color: #000000;
      }

      /* Page breaks for better pagination */
      h1, h2, h3 {
        page-break-after: avoid;
      }

      /* Ensure images are appropriately sized or hidden if decorative */
      img {
        max-width: 100%;
        height: auto;
      }
    }
  </style>

</head>
<body>

  <!-- Light/Dark mode toggle button on the right -->
  <button class="theme-toggle" onclick="toggleTheme()">Toggle Light/Dark Mode</button>

  <!-- Bell Icon and Dropdown for Notifications -->
  <div class="bell-container">
    <img src="https://github.com/23795824/health-system-skripsie/blob/main/bell_icon3.png?raw=true" class="bell-icon" alt="Bell Icon" onclick="toggleDropdown()">
    <div class="dropdown" id="notificationDropdown">
      <h3>Notifications</h3>
      <div id="notificationsContent"></div>
    </div>
  </div>

  <!-- Container that holds both the sidebar and main content -->
  <div class="container">

    <!-- Sidebar -->
    <div class="sidebar">
      <button onclick="window.location.href='/start'">Dashboard</button>
      <button onclick="window.location.href='/historical'">Historical Mode</button>
      <button onclick="showSection('temp')">Temperature</button>
      <button onclick="showSection('hr')">Heart Rate</button>
      <button onclick="showSection('oxy')">Oxygen Level</button>
      <button onclick="window.location.href='/healthinfo'">Health Information</button>
    </div>

    <!-- Main content area -->
    <div class="main-content">
      <h1>Wellness Monitor Dashboard</h1>

      <!-- Current Time Display -->
      <div class="header" style="text-align: right;">
        <p style="font-size: 28px; margin-top: -30px;">Current Time: <span id="timeDisplay">Loading...</span></p>
      </div>

      <!-- Temperature Section -->
      <div id="tempSection" style="margin-top: 30px;">
        <div class="header">
          <img src="https://www.transparentpng.com/download/temperature/climate-control-home-temperature-png-17.png" alt="Temperature Icon" style="width: 50px; vertical-align: middle;">
          <p style="display: inline; font-size: 24px;">Temperature: <span class="data" id="tempData">Loading...</span></p>
        </div>

        <!-- Button to toggle graphs visibility -->
        <button onclick="toggleGraphs()" style="margin-top: 30px;">Enable/Disable Graphical Data</button>

        <!-- Temperature Chart -->
        <canvas id="tempChart"></canvas>
      </div>

      <!-- Heart Rate Section -->
      <div id="hrSection" style="margin-top: 30px; display: none;">
        <div class="header">
          <img src="https://cdn-icons-png.flaticon.com/512/3708/3708641.png" alt="Heart Rate Icon" style="width: 50px; vertical-align: middle;">
          <p style="display: inline; font-size: 24px;">Heart Rate: <span class="data" id="hrData">Loading...</span></p>
        </div>

        <!-- Button to toggle graphs visibility -->
        <button onclick="toggleGraphs()" style="margin-top: 30px;">Enable/Disable Graphical Data</button>

        <!-- Heart Rate Chart -->
        <canvas id="hrChart"></canvas>
      </div>

      <!-- Oxygen Level Section -->
      <div id="oxySection" style="margin-top: 40px; display: none;">
        <div class="header">
          <img src="https://d2g2p0i8cc2bmh.cloudfront.net/2023-03/icon-blood-oximeters.png" alt="Oximeter Icon" style="width: 50px; vertical-align: middle;">
          <p style="display: inline; font-size: 24px;">Oxygen Level: <span class="data" id="oxyData">Loading...</span></p>
        </div>

        <!-- Button to toggle graphs visibility -->
        <button onclick="toggleGraphs()" style="margin-top: 30px;">Enable/Disable Graphical Data</button>

        <!-- Oxygen Level Chart -->
        <canvas id="oxyChart"></canvas>
      </div>

      <!-- Alert Prompts -->
      <div id="tempAlertPrompt" class="alert-prompt">
        <p>Temperature lower than normal!</p>
        <button onclick="closeTempAlert()">Close</button>
      </div>
      
      <div id="tempAlertPrompt2" class="alert-prompt">
        <p>Temperature higher than normal!</p>
        <button onclick="closeTempAlert2()">Close</button>
      </div>
      
      <div id="hrAlertPrompt" class="alert-prompt">
        <p>Heart Rate lower than normal!</p>
        <button onclick="closeHrAlert()">Close</button>
      </div>
      
      <div id="hrAlertPrompt2" class="alert-prompt">
        <p>Heart Rate higher than normal!</p>
        <button onclick="closeHrAlert2()">Close</button>
      </div>
      
      <div id="oxyAlertPrompt" class="alert-prompt">
        <p>Oxygen Level is lower than normal!</p>
        <button onclick="closeOxyAlert()">Close</button>
      </div>

    </div> <!-- Close main-content -->
  </div> <!-- Close container -->

  <!-- JavaScript for Chart.js and Theme Toggle -->
  <script>
    let tempChart, hrChart, oxyChart;

    // Function to create Temperature Chart
    function createTempChart(ctx) {
      tempChart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: [],
          datasets: [{
            label: 'Temperature (°C)',
            data: [],
            borderColor: 'rgba(255, 99, 132, 1)',
            backgroundColor: 'rgba(255, 99, 132, 0.2)',
            fill: false
          }]
        },
        options: {
          scales: {
            x: {
              display: true,
              title: {
                display: true,
                text: 'Time (s)',
                font: {
                  size: 18,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                },
                color: '#000000' /* White in dark mode */
              },
              grid: {
                color: 'rgba(255, 255, 255, 0.2)' /* White grid lines in dark mode */
              },
              ticks: {
                color: '#000000', /* White tick labels in dark mode */
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            },
            y: {
              beginAtZero: false,
              min: 34,
              max: 39,
              title: {
                display: true,
                text: 'Temperature (°C)',
                font: {
                  size: 18,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                },
                color: '#000000' /* White in dark mode */
              },
              grid: {
                color: 'rgba(255, 255, 255, 0.2)' /* White grid lines in dark mode */
              },
              ticks: {
                color: '#000000', /* White tick labels in dark mode */
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            }
          },
          plugins: {
            legend: {
              labels: {
                color: '#000000', /* White legend text in dark mode */
                font: {
                  size: 16,
                  family: 'Arial, sans-serif'
                }
              }
            }
          }
        }
      });
    }

    // Function to create Heart Rate Chart
    function createHrChart(ctx) {
      hrChart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: [],
          datasets: [{
            label: 'Heart Rate (BPM)',
            data: [],
            borderColor: 'rgba(54, 162, 235, 1)',
            backgroundColor: 'rgba(54, 162, 235, 0.2)',
            fill: false
          }]
        },
        options: {
          scales: {
            x: {
              display: true,
              title: {
                display: true,
                text: 'Time (s)',
                font: {
                  size: 18,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                },
                color: '#000000' /* White in dark mode */
              },
              grid: {
                color: 'rgba(255, 255, 255, 0.2)' /* White grid lines in dark mode */
              },
              ticks: {
                color: '#000000', /* White tick labels in dark mode */
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            },
            y: {
              beginAtZero: false,
              min: 40,
              max: 110,
              title: {
                display: true,
                text: 'Heart Rate (BPM)',
                font: {
                  size: 18,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                },
                color: '#000000' /* White in dark mode */
              },
              grid: {
                color: 'rgba(255, 255, 255, 0.2)' /* White grid lines in dark mode */
              },
              ticks: {
                color: '#000000', /* White tick labels in dark mode */
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            }
          },
          plugins: {
            legend: {
              labels: {
                color: '#000000', /* White legend text in dark mode */
                font: {
                  size: 16,
                  family: 'Arial, sans-serif'
                }
              }
            }
          }
        }
      });
    }

    // Function to create Oxygen Level Chart
    function createOxyChart(ctx) {
      oxyChart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: [],
          datasets: [{
            label: 'Oxygen Level (%)',
            data: [],
            borderColor: 'rgba(75, 192, 192, 1)',
            backgroundColor: 'rgba(75, 192, 192, 0.2)',
            fill: false
          }]
        },
        options: {
          scales: {
            x: {
              display: true,
              title: {
                display: true,
                text: 'Time (s)',
                font: {
                  size: 18,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                },
                color: '#000000' /* White in dark mode */
              },
              grid: {
                color: 'rgba(255, 255, 255, 0.2)' /* White grid lines in dark mode */
              },
              ticks: {
                color: '#000000', /* White tick labels in dark mode */
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            },
            y: {
              beginAtZero: false,
              min: 90,
              max: 100,
              title: {
                display: true,
                text: 'Oxygen Level (%)',
                font: {
                  size: 18,
                  weight: 'bold',
                  family: 'Arial, sans-serif'
                },
                color: '#000000' /* White in dark mode */
              },
              grid: {
                color: 'rgba(255, 255, 255, 0.2)' /* White grid lines in dark mode */
              },
              ticks: {
                color: '#000000', /* White tick labels in dark mode */
                font: {
                  size: 14,
                  family: 'Arial, sans-serif'
                }
              }
            }
          },
          plugins: {
            legend: {
              labels: {
                color: '#000000', /* White legend text in dark mode */
                font: {
                  size: 16,
                  family: 'Arial, sans-serif'
                }
              }
            }
          }
        }
      });
    }

    // Initialize Charts on Page Load
    window.onload = function() {
      createTempChart(document.getElementById('tempChart').getContext('2d'));
      createHrChart(document.getElementById('hrChart').getContext('2d'));
      createOxyChart(document.getElementById('oxyChart').getContext('2d'));

      // Set theme based on localStorage
      var theme = localStorage.getItem('theme');
      if (theme === 'dark') { 
        document.body.classList.add('dark-mode'); 
        updateChartColors(); // Update chart colors if dark mode is active
      }

      // Show dashboard section by default
      showSection('temp');
    };

    // Function to toggle graph visibility
    function toggleGraphs() {
      let sections = ['temp', 'hr', 'oxy'];
      sections.forEach(section => {
        let chart = document.getElementById(section + 'Chart');
        if(chart.style.display === 'none') {
          chart.style.display = 'block';
        } else {
          chart.style.display = 'none';
        }
      });
    }

    // Function to show specific section
    function showSection(section) {
      let sections = ['tempSection', 'hrSection', 'oxySection'];
      sections.forEach(sec => {
        document.getElementById(sec).style.display = 'none';
      });
      document.getElementById(section + 'Section').style.display = 'block';
    }

    // Function to toggle light/dark theme
    function toggleTheme() {
      document.body.classList.toggle('dark-mode');
      updateChartColors(); // Update chart colors when theme changes
      // Save theme preference
      if (document.body.classList.contains('dark-mode')) {
        localStorage.setItem('theme', 'dark');
      } else {
        localStorage.setItem('theme', 'light');
      }
    }

    // Function to update chart colors based on theme
    function updateChartColors() {
      let isDarkMode = document.body.classList.contains('dark-mode');
      let gridColor = isDarkMode ? 'rgba(255, 255, 255, 0.2)' : 'rgba(0, 0, 0, 0.1)';
      let fontColor = isDarkMode ? '#ffffff' : '#666666';

      // Update Temperature Chart
      tempChart.options.scales.x.grid.color = gridColor;
      tempChart.options.scales.y.grid.color = gridColor;
      tempChart.options.scales.x.ticks.color = fontColor;
      tempChart.options.scales.y.ticks.color = fontColor;
      tempChart.options.plugins.legend.labels.color = fontColor;
      tempChart.update();

      // Update Heart Rate Chart
      hrChart.options.scales.x.grid.color = gridColor;
      hrChart.options.scales.y.grid.color = gridColor;
      hrChart.options.scales.x.ticks.color = fontColor;
      hrChart.options.scales.y.ticks.color = fontColor;
      hrChart.options.plugins.legend.labels.color = fontColor;
      hrChart.update();

      // Update Oxygen Level Chart
      oxyChart.options.scales.x.grid.color = gridColor;
      oxyChart.options.scales.y.grid.color = gridColor;
      oxyChart.options.scales.x.ticks.color = fontColor;
      oxyChart.options.scales.y.ticks.color = fontColor;
      oxyChart.options.plugins.legend.labels.color = fontColor;
      oxyChart.update();
    }

    // Function to close alert prompts
    function closeTempAlert() { document.getElementById('tempAlertPrompt').style.display = 'none'; }
    function closeTempAlert2() { document.getElementById('tempAlertPrompt2').style.display = 'none'; }
    function closeHrAlert() { document.getElementById('hrAlertPrompt').style.display = 'none'; }
    function closeHrAlert2() { document.getElementById('hrAlertPrompt2').style.display = 'none'; }
    function closeOxyAlert() { document.getElementById('oxyAlertPrompt').style.display = 'none'; }

    // Function to toggle the notification dropdown
    function toggleDropdown() {
      var dropdown = document.getElementById('notificationDropdown');
      if (dropdown.classList.contains('show')) {
        dropdown.classList.remove('show');
      } else {
        dropdown.classList.add('show');
      }
    }

    // Function to update notifications
    function updateNotifications(data) {
      console.log('Received Data:', data);
      let notifications = '';

      // Temperature Notifications
      if (data.temp_IF_sensor !== 0 && data.temp_IF_sensor < 35) {
        notifications += `<div class='notification-item'>
                            <img src='https://github.com/23795824/health-system-skripsie/blob/main/Temp_icon.png?raw=true' class='notification-icon temperature' alt='Temperature Icon'>
                            <div class='notification-details'>
                              <div class='notification-title'>Low Temp detected!</div>
                              <div class='notification-time'>${new Date().toLocaleTimeString()}</div>
                            </div>
                          </div>`;
      }
      if (data.temp_IF_sensor !== 0 && data.temp_IF_sensor > 37.5) {
        notifications += `<div class='notification-item'>
                            <img src='https://github.com/23795824/health-system-skripsie/blob/main/Temp_icon.png?raw=true' class='notification-icon temperature' alt='Temperature Icon'>
                            <div class='notification-details'>
                              <div class='notification-title'>High Temp detected!</div>
                              <div class='notification-time'>${new Date().toLocaleTimeString()}</div>
                            </div>
                          </div>`;
      }

      // Heart Rate Notifications
      if (data.heartrate !== 0 && data.heartrate < 50) {
        notifications += `<div class='notification-item'>
                            <img src='https://github.com/23795824/health-system-skripsie/blob/main/Heart_icon.png?raw=true' class='notification-icon heart-rate' alt='Heart Rate Icon'>
                            <div class='notification-details'>
                              <div class='notification-title'>Low HR detected!</div>
                              <div class='notification-time'>${new Date().toLocaleTimeString()}</div>
                            </div>
                          </div>`;
      }
      if (data.heartrate !== 0 && data.heartrate > 100) {
        notifications += `<div class='notification-item'>
                            <img src='https://github.com/23795824/health-system-skripsie/blob/main/Heart_icon.png?raw=true' class='notification-icon heart-rate' alt='Heart Rate Icon'>
                            <div class='notification-details'>
                              <div class='notification-title'>High HR detected!</div>
                              <div class='notification-time'>${new Date().toLocaleTimeString()}</div>
                            </div>
                          </div>`;
      }

      // Oxygen Level Notifications
      if (data.oxygen !== 0 && data.oxygen < 95) {
        notifications += `<div class='notification-item'>
                            <img src='https://github.com/23795824/health-system-skripsie/blob/main/Oxy_icon.png?raw=true' class='notification-icon oxygen' alt='Oxygen Level Icon'>
                            <div class='notification-details'>
                              <div class='notification-title'>Low SPO2 detected!</div>
                              <div class='notification-time'>${new Date().toLocaleTimeString()}</div>
                            </div>
                          </div>`;
      }

      // If no notifications, display a default message
      if (notifications === '') {
        notifications = '<p>No new notifications</p>';
      }

      // Update the dropdown content
      document.getElementById('notificationsContent').innerHTML = notifications;
      console.log('Updated Notifications:', notifications);
    }

    // Fetch data and update notifications every second
    setInterval(function() {
      fetch('/data').then(response => response.json()).then(data => {
        document.getElementById('tempData').innerHTML = data.temp_IF_sensor + ' &#8451;';
        document.getElementById('hrData').innerHTML = data.heartrate + ' BPM';
        document.getElementById('oxyData').innerHTML = data.oxygen + ' %';

        // Update charts with real-time data
        updateChartData(tempChart, data.temp_IF_sensor);
        updateChartData(hrChart, data.heartrate);
        updateChartData(oxyChart, data.oxygen);

        // Update notifications
        updateNotifications(data);
      }).catch(error => {
        console.error('Error fetching data:', error);
      });
    }, 1000);  // Update every second

    // Function to update chart data
    function updateChartData(chart, dataPoint) {
      if (chart.data.labels.length >= 20) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
      }
      chart.data.labels.push('');
      chart.data.datasets[0].data.push(dataPoint);
      chart.update();
    }

    // JavaScript for fetching the time
    function updateTime() {
      fetch('/time').then(response => response.json()).then(data => {
        document.getElementById('timeDisplay').innerHTML = data.time;
      }).catch(error => {
        console.error('Error fetching time:', error);
      });
    }
    // Call updateTime every second (1000ms)
    setInterval(updateTime, 1000);
  </script>
</body>
</html>
)rawliteral";

    request->send(200, "text/html", html);
});

// Serve data as JSON for the graphs
server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
  String json = "{\"temp_IF_sensor\":" + String(temp_IF_sensor) + ",\"heartrate\":" + String(Heartrate_value) + ",\"oxygen\":" + String(Oximeter_value) + "}";
  request->send(200, "application/json", json);
});

// New Endpoint for Resetting Password
server.on("/reset-password", HTTP_GET, [](AsyncWebServerRequest *request){
  // Clear the stored password in non-volatile storage (NVS)
  preferences.remove("password");
  request->send(200, "text/plain", "Password has been reset. Please set a new password.");
  Serial.println("Password reset. The next time the user accesses the server, they will be prompted to set a new password.");
});

// Endpoint to serve the current time as JSON
server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request){
  timeClient.update(); // Update time
  String currentTime = timeClient.getFormattedTime();
  String json = "{\"time\":\"" + currentTime + "\"}";
  request->send(200, "application/json", json);
});

// Endpoint to serve the average for historical mode
server.on("/getAverages", HTTP_GET, [](AsyncWebServerRequest *request) {
  String jsonResponse = "{";
  jsonResponse += "\"temperature\":" + String(avgTemp) + ",";
  jsonResponse += "\"heart_rate\":" + String(avgHeartRate) + ",";
  jsonResponse += "\"oxygen_level\":" + String(avgOxygen);
  jsonResponse += "}";

  request->send(200, "application/json", jsonResponse);
});

// Serve last 10 temperature measurements as JSON
server.on("/getLastTenTemperatures", HTTP_GET, [](AsyncWebServerRequest *request) {
  String json = "[";
  
  // Assuming you store the last 10 temperature values in an array or list
  for (int i = 0; i < lastTenTemperatures.size(); i++) {
    json += "{\"temperature\":" + String(lastTenTemperatures[i].temperature) + ",";
    json += "\"timestamp\":\"" + lastTenTemperatures[i].timestamp + "\"}";
    if (i < lastTenTemperatures.size() - 1) {
      json += ",";
    }
  }
  
  json += "]";
  
  request->send(200, "application/json", json);
});


// Serve the endpoint to retrieve the last ten heart rate measurements
server.on("/getLastTenHeartRates", HTTP_GET, [](AsyncWebServerRequest *request) {
  String jsonResponse = "[";
  for (size_t i = 0; i < lastTenHeartRates.size(); i++) {
    jsonResponse += "{\"heart_rate\": " + String(lastTenHeartRates[i].heartRate) +
                    ", \"timestamp\": \"" + lastTenHeartRates[i].timestamp + "\"}";
    if (i < lastTenHeartRates.size() - 1) {
      jsonResponse += ",";
    }
  }
  jsonResponse += "]";
  request->send(200, "application/json", jsonResponse);
});

// Serve the endpoint to retrieve the last ten oxygen levels
server.on("/getLastTenOxygenLevels", HTTP_GET, [](AsyncWebServerRequest *request) {
  String jsonResponse = "[";
  for (size_t i = 0; i < lastTenOxygenLevels.size(); i++) {
    jsonResponse += "{\"oxygen_level\": " + String(lastTenOxygenLevels[i].oxygen_level) +
                    ", \"timestamp\": \"" + lastTenOxygenLevels[i].timestamp + "\"}";
    if (i < lastTenOxygenLevels.size() - 1) {
      jsonResponse += ",";
    }
  }
  jsonResponse += "]";
  request->send(200, "application/json", jsonResponse);
});











// Server Start
server.begin();
Serial.println("Server started successfully.");

// Initialize the SD card
 // if (!SD.begin(SD_CS)) {
 //   Serial.println("Card failed, or not present");
    // Handle initialization failure
  //  while (1);
  //}
 // Serial.println("SD card initialized.");

  

}

void loop() {
  int i = 0;
  int16_t itemp = 0;
  
  // Read data via I2C
  // I2C buffer of "Arduino MKR" is 256 buffer. (It is enough)
    memset(rbuf, 0, N_READ);
    Wire.beginTransmission(D6T_ADDR);  // I2C slave address
    Wire.write(D6T_CMD);               // D6T register
    Wire.endTransmission();            
    Wire.requestFrom(D6T_ADDR, N_READ);
    while (Wire.available()) {
        rbuf[i++] = Wire.read();
    }
    D6T_checkPEC(rbuf, N_READ - 1);

    // Convert to temperature data (degC)
    ptat = (double)conv8us_s16_le(rbuf, 0) / 10.0;
  for (i = 0; i < N_PIXEL; i++) {
    itemp = conv8us_s16_le(rbuf, 2 + 2*i);
    pix_data[i] = (double)itemp / 10.0;
  }

  temp_IF_sensor = pix_data[0];
  temp_IF_sensor = temp_IF_sensor + calibration_offset;
  

  // Set tempDataValid flag if temp_IF_sensor is within expected operational range
  if(temp_IF_sensor > 34 && temp_IF_sensor < 40.0){
    tempDataValid = true;
  }else{
    tempDataValid = false;
  }

  //Serial.print("Temperature: ");
  //Serial.print(temp_IF_sensor, 1);
  //Serial.println(" [degC]");
    
  // Information from the readBpm function will be saved to our "body" variable
  body = bioHub.readBpm();

  // Update heart rate and oximeter values, and validate data
  Heartrate_value = body.heartRate;
  Oximeter_value = body.oxygen;

  Heartrate_value = 70;
  Oximeter_value = 98;

  
   //Serial.print("\nOxygen levels: ");
   //Serial.print(body.oxygen, 1);
   //Serial.print("\nHeartrate (BPM): ");
   //Serial.print(body.heartRate, 1);


  heartRateDataValid = (Heartrate_value > 0);
  oxygenDataValid = (Oximeter_value > 0);



  // Log data every 10 seconds
  static unsigned long lastLogTime = 0;
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastLogTime >= 10000) { // 10 seconds
    logDataToFirebase();
    
    retrieveHistoricalData(); // Call the new function
    retrieveLastTenValidMeasurements();
    lastLogTime = currentMillis;
  }

// Update only the values
  updateMeasurements(temp_IF_sensor, Heartrate_value, Oximeter_value);
 
  timeClient.update();
  

  // Add a delay to avoid overwhelming the sensor and ensure reliable readings
  delay(500);
}

// Function to display static elements that don't need frequent updates
void displayStaticElements() {
  // Header with background
  tft.fillRect(0, 0, tft.width(), 40, HX8357_BLACK);  // Background for the header
  tft.setTextColor(HX8357_TEAL);
  tft.setTextSize(4);
  tft.setCursor(30, 10);
  tft.println("Wellness Monitor");

  // Background sections for data
  displayBackgroundSections();

  // Static labels for Temperature, Heart Rate, and Oxygen Level
  displaySectionLabel("Temperature", 65);  // Adjusted yPosition to center in the section
  displaySectionLabel("Heart Rate", 145);  // Adjusted yPosition to center in the section
  displaySectionLabel("Oxygen Level", 225); // Adjusted yPosition to center in the section

  // Draw dividers
  displayDividers();
}

// Function to display background sections for each measurement
void displayBackgroundSections() {
  tft.fillRect(0, 50, tft.width(), 60, HX8357_DARKGREY);   // Temperature section
  tft.fillRect(0, 130, tft.width(), 60, HX8357_DARKGREY);  // Heart Rate section
  tft.fillRect(0, 210, tft.width(), 60, HX8357_DARKGREY);  // Oxygen Level section
}

// Helper function to display labels for each section
void displaySectionLabel(String label, int yPosition) {
  tft.setTextSize(3.5);
  tft.setCursor(20, yPosition);
  tft.setTextColor(HX8357_WHITE);
  tft.println(label);
}

// Function to draw dividers between sections
void displayDividers() {
  tft.drawLine(0, 120, tft.width(), 120, HX8357_WHITE); // Divider between Temperature and Heart Rate
  tft.drawLine(0, 200, tft.width(), 200, HX8357_WHITE); // Divider between Heart Rate and Oxygen Level
}

// Function to update measurements without clearing the whole screen
void updateMeasurements(float temperature, int heartRate, int oxygenLevel) {
  // Update Temperature Value
  updateValue(temperature, 36.1, 37.2, 250, 60, " C", lastTemperature_2);  // Adjusted x-position for centering

  // Update Heart Rate Value
  updateValue(heartRate, 60, 100, 250, 140, " BPM", lastHeartRate_2);  // Adjusted x-position for centering

  // Update Oxygen Level Value
  updateValue(oxygenLevel, 95, 100, 250, 220, " %", lastOxygenLevel_2);  // Adjusted x-position for centering

  // Get actual timestamp from a clock or RTC module
  String currentTime = timeClient.getFormattedTime(); // Get current time

  // Update timestamp
  displayTimestamp(currentTime);
}

// Helper function to update values with specific positioning and color coding
void updateValue(float value, float safeMin, float safeMax, int x, int y, String unit, float& lastValue) {
  // Check if the value has changed; if not, skip the update
  if (value == lastValue) return;

  // Clear the old value by overwriting it with the background color
  tft.setTextColor(HX8357_DARKGREY);
  tft.setCursor(x, y);
  tft.setTextSize(4);
  tft.print(lastValue, 1);  // Clear previous value text with background color
  tft.print(unit);

  // Set color based on value range without using a black background
  if (value < safeMin || value > safeMax) {
    tft.setTextColor(HX8357_RED);  // Red color for danger
  } else {
    tft.setTextColor(HX8357_GREEN);  // Green color for safe range
  }

  // Display updated value
  tft.setCursor(x, y);
  tft.setTextSize(4);
  tft.print(value, 1);  // The ', 1' keeps only one decimal place
  tft.print(unit);

  // Update last value for next check
  lastValue = value;
}

// Function to display timestamp at the bottom of the screen
void displayTimestamp(String time) {
  // Only update if the time has changed
  if (time == lastTime) return;

  const int timestampHeight = 30;  // Increased height for better coverage
  const int padding = 5;           // Padding for text positioning

  // Clear the old timestamp by overwriting it with the background color
  tft.setTextColor(HX8357_BLACK);
  tft.setCursor(10, tft.height() - timestampHeight + padding);
  tft.setTextSize(2.5);
  tft.print("Last update: ");
  tft.print(lastTime);  // Erase previous time

  // Set the new color for the timestamp
  tft.setTextColor(HX8357_YELLOW);

  // Display the updated timestamp
  tft.setCursor(10, tft.height() - timestampHeight + padding);
  tft.print("Last update: ");
  tft.print(time);

  // Update the last time for the next check
  lastTime = time;
}




// Replace with your actual functions to read sensor values
float readTemperature() {
  // Placeholder for actual temperature reading code
  return temp_IF_sensor;
}

int readHeartRate() {
  // Placeholder for actual heart rate reading code
  return Heartrate_value;
}

int readOximeter() {
  // Placeholder for actual oximeter reading code
  return Oximeter_value;
}


void logDataToFirebase() {
  // Only log valid data
  if (Heartrate_value == 0 || Oximeter_value == 0) {
    Serial.println("Skipping log due to zero values.");
    return;
  }

  // Create FirebaseJson object to store the data
  FirebaseJson json;

  // Add sensor data to FirebaseJson object without leading slashes
  json.set("temperature", temp_IF_sensor);
  json.set("heart_rate", Heartrate_value);
  json.set("oxygen_level", Oximeter_value);

  // Define a unique path using millis()
  String path = "/sensor_data/" + String(millis());

  // Push the data to Firebase using fbdo
  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("Data sent successfully to path: " + path);

    // Print the JSON data that was sent
    String jsonString;
    json.toString(jsonString, true); // true for prettify
    Serial.println("Data sent:");
    Serial.println(jsonString);
  } else {
    Serial.println("Failed to send data");
    Serial.println("Error code: " + String(fbdo.httpCode()));
    Serial.println("Reason: " + fbdo.errorReason());
  }
}



void retrieveHistoricalData() {
  fbdoGet.clear(); // Clear any previous state
  FirebaseJson jsonData;
  FirebaseJsonData result;

  // Path to the sensor data
  String path = "/sensor_data";

  // Create a QueryFilter to limit the data retrieved
  QueryFilter query;
  query.orderBy("$key");
  query.limitToLast(5); // Retrieve the last 5 entries

  // Fetch the data using fbdoGet with the query
  if (Firebase.RTDB.getJSON(&fbdoGet, path, &query)) {
    // Check if data is available
    if (fbdoGet.dataType() == "json") {
      jsonData = fbdoGet.to<FirebaseJson>();

      // Initialize variables to calculate averages
      float totalTemp = 0, totalHeartRate = 0, totalOxygen = 0;
      int tempCount = 0, heartRateCount = 0, oxygenCount = 0;

      String key, value;
      int type;
      FirebaseJson subJson;

      // Iterate through the retrieved entries
      size_t len = jsonData.iteratorBegin();
      for (size_t i = 0; i < len; i++) {
        jsonData.iteratorGet(i, type, key, value);
        subJson.setJsonData(value);

        float temp = 0, heartRate = 0, oxygen = 0;

        if (subJson.get(result, "temperature")) {
          temp = result.to<float>();
          if (temp != 0) {
            totalTemp += temp;
            tempCount++;
          }
        }

        if (subJson.get(result, "heart_rate")) {
          heartRate = result.to<float>();
          if (heartRate != 0) {  // Exclude zero values
            totalHeartRate += heartRate;
            heartRateCount++;
          }
        }

        if (subJson.get(result, "oxygen_level")) {
          oxygen = result.to<float>();
          if (oxygen != 0) {  // Exclude zero values
            totalOxygen += oxygen;
            oxygenCount++;
          }
        }
      }
      jsonData.iteratorEnd(); // Clean up the iterator

      // Calculate averages
      avgTemp = tempCount > 0 ? totalTemp / tempCount : 0;
      avgHeartRate = heartRateCount > 0 ? totalHeartRate / heartRateCount : 0;
      avgOxygen = oxygenCount > 0 ? totalOxygen / oxygenCount : 0;

      // Print the results
      Serial.println("Averages of the last 5 entries:");
      Serial.printf("Temperature: %.2f°C\n", avgTemp);
      Serial.printf("Heart Rate: %.2f BPM\n", avgHeartRate);
      Serial.printf("Oxygen Level: %.2f%%\n", avgOxygen);
    } else {
      Serial.println("No JSON data found at the specified path.");
    }
  } else {
    Serial.println("Failed to retrieve data:");
    Serial.println("HTTP Code: " + String(fbdoGet.httpCode()));
    Serial.println("Error Reason: " + fbdoGet.errorReason());
  }
}


void retrieveLastTenValidMeasurements() {
    fbdoGet.clear(); // Clear any previous state
    FirebaseJson jsonData;
    FirebaseJsonData result;

    // Path to the sensor data
    String path = "/sensor_data";

    // Create a QueryFilter to limit the data retrieved
    QueryFilter query;
    query.orderBy("$key");
    query.limitToLast(50); // Retrieve the last 50 entries (to filter valid ones)

    // Fetch the data using fbdoGet with the query
    if (Firebase.RTDB.getJSON(&fbdoGet, path, &query)) {
        // Check if data is available
        if (fbdoGet.dataType() == "json") {
            jsonData = fbdoGet.to<FirebaseJson>();

            // Clear the arrays before updating
            lastTenTemperatures.clear();
            lastTenHeartRates.clear();
            lastTenOxygenLevels.clear();

            // Initialize variables for counting valid entries
            int validEntries = 0;

            // Iterate through the retrieved entries, label them 1 to 10
            size_t len = jsonData.iteratorBegin();
            for (size_t i = 0; i < len && validEntries < 10; i++) {
                String key, value;
                int type;
                FirebaseJson subJson;

                // Get the JSON data
                jsonData.iteratorGet(i, type, key, value);
                subJson.setJsonData(value);

                float temp = 0, heartRate = 0, oxygen = 0;

                // Get the temperature
                if (subJson.get(result, "temperature")) {
                    temp = result.to<float>();
                }

                // Get the heart rate
                if (subJson.get(result, "heart_rate")) {
                    heartRate = result.to<float>();
                }

                // Get the oxygen level
                if (subJson.get(result, "oxygen_level")) {
                    oxygen = result.to<float>();
                }

                // Only include non-zero values
                if (temp > 0 && heartRate > 0 && oxygen > 0) {
                    validEntries++;  // Only count valid entries

                    // Store the valid entries in the arrays
                    addTemperatureReading(temp);
                    addHeartRateReading(heartRate);
                    addOxygenLevelReading(oxygen);

                    // Print the valid entry data for debugging
                   // Serial.printf("Entry #%d\n", validEntries);
                   // Serial.printf("  Temperature: %.2f°C\n", temp);
                   // Serial.printf("  Heart Rate: %.2f BPM\n", heartRate);
                   // Serial.printf("  Oxygen Level: %.2f%%\n", oxygen);
                   // Serial.println("------------------------------");
                }
            }

            jsonData.iteratorEnd(); // Clean up the iterator

            if (validEntries == 0) {
                Serial.println("No valid data found.");
            }
        } else {
            Serial.println("No JSON data found at the specified path.");
        }
    } else {
        Serial.println("Failed to retrieve data:");
        Serial.println("HTTP Code: " + String(fbdoGet.httpCode()));
        Serial.println("Error Reason: " + fbdoGet.errorReason());
    }
}



// Function to store temperature data
void addTemperatureReading(float temperature) {
  String timestamp = String(millis() / 1000) + " seconds";
  TemperatureData data = { temperature, timestamp };
  
  lastTenTemperatures.push_back(data);
  if (lastTenTemperatures.size() > 10) {
    lastTenTemperatures.erase(lastTenTemperatures.begin());
  }
}

// Function to store heart rate data
void addHeartRateReading(float heartRate) {
  String timestamp = String(millis() / 1000) + " seconds";
  HeartRateData data = { heartRate, timestamp };
  
  lastTenHeartRates.push_back(data);
  if (lastTenHeartRates.size() > 10) {
    lastTenHeartRates.erase(lastTenHeartRates.begin());
  }
}

// Function to store oxygen level data
void addOxygenLevelReading(float oxygen) {
  String timestamp = String(millis() / 1000) + " seconds"; 
  OxygenLevelData data = { oxygen, timestamp };

  lastTenOxygenLevels.push_back(data);
  if (lastTenOxygenLevels.size() > 10) {
    lastTenOxygenLevels.erase(lastTenOxygenLevels.begin());
  }
}

