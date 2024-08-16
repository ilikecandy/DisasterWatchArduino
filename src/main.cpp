/*

Conditions for detection:

Risks:
- Drought: Soil moisture < 300 || Temperature > 35 || Humidity < 30
- Fire risk: Temperature > 30 && Humidity < 30

Disasters:
- Earthquake: Acceleration > 500 for 5 readings
- Fire: Temperature > 60 && Light level > 1000

*/

#include <Wire.h>
#include <DHT.h>
#include <BMI160Gen.h>
// #include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>

// Wifi credentials
const String ssid = "*****";          // Replace with your Wi-Fi SSID
const String password = "*****";  // Replace with your Wi-Fi password
const String serverName = "https://notifications-api-ten.vercel.app/";  // Replace with your URL
const String token = "*****";         // Replace with token

// Pin defs
#define I2C_SDA 27
#define I2C_SCL 14
#define DHTPIN 12
#define WATER_SENSOR_PIN 33
#define LIGHT_SENSOR_PIN 32

// Sensor setup
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define BMI160_I2C_ADDR 0x68
BMI160GenClass bmi160;

// LCD setup with i2c
// LiquidCrystal_I2C lcd(0x3F, 16, 2);


// Detection thresholds
#define EARTHQUAKE_THRESHOLD 500    // ~230 when the board is still
#define EARTHQUAKE_READINGS 5      // Number of samples to confirm an earthquake
#define EARTHQUAKE_READINGS_TO_CLEAR 5 // Number of samples below threshold to clear the alert
#define EARTHQUAKE_WARNING_READINGS 2 // Number of samples to display warning
int FIRE_TEMP_THRESHOLD = 30;      // Fire detection temperature in °C
#define FIRE_LIGHT_THRESHOLD 100    // Fire detection light level (0-4095)
int FIRE_RISK_TEMP = 30;           // Fire risk temperature in °C
#define FIRE_RISK_HUMIDITY 30       // Fire risk humidity in %
#define DROUGHT_SOIL_MOISTURE_THRESHOLD 1000 // Low soil moisture threshold (0-4095)
#define DROUGHT_TEMP_THRESHOLD 35   // High temperature threshold for drought risk
#define DROUGHT_HUMIDITY_THRESHOLD 30 // Low humidity threshold for drought risk

// rolling average
#define DATA_POINTS_PER_DAY 1440  // Assuming data is collected every minute
#define DAYS_TO_AVERAGE 7
#define TOTAL_DATA_POINTS (DATA_POINTS_PER_DAY * DAYS_TO_AVERAGE)

// Arrays to store the last week's data
float temperatureData[TOTAL_DATA_POINTS];
int dataIndex = 0;
int dataCount = 0;
int readingSeconds = 0; // Counter for readings- record every 60s, so 30 readings

int earthquakeSamples = 0;          // Counter for earthquake readings
int belowThresholdReadings = 0;     // Counter for consecutive readings below the threshold
// String riskStatus = "";
// String disasterStatus = "";

bool earthquakeWarning = false;
bool earthquakeDetected = false;
bool fireWarning = false;
bool fireDetected = false;
bool droughtWarning = false;

// Function to store the data and maintain a rolling average
void storeData(float temperature, float humidity) {
    temperatureData[dataIndex] = temperature;
    dataIndex = (dataIndex + 1) % TOTAL_DATA_POINTS; // Loop the index to stay within array bounds
    
    if (dataCount < TOTAL_DATA_POINTS) {
        dataCount++; // Track the total number of data points stored
    }
}
 
// Function to calculate the rolling average of the last week's data
void calculateRollingAverage(float &avgTemp, float &avgHumidity) {
    float tempSum = 0;

    // Accumulate the sum of the stored data
    for (int i = 0; i < dataCount; i++) {
        tempSum += temperatureData[i];
    }

    // Calculate the averages
    if (dataCount > 0) {
        avgTemp = tempSum / dataCount;
    }
}

// Adjust fire risk temperature and humidity thresholds based on the calculated averages
void adjustFireRiskThresholds(float avgTemp, float avgHumidity) {
    if (avgTemp > 0 && avgHumidity > 0) {
        FIRE_RISK_TEMP = avgTemp + 5; // Adjust temp threshold with a margin
    }
}

// Display state- cycling between risks (0) and disasters (1)
int displayState = 0;

// Function to update the LCD display with current statuses
// void updateLCD(bool riskDetected, bool disasterDetected) {
//     lcd.clear();
//     // Display all ok if no risks or disasters detected
//     if (!riskDetected && !disasterDetected) {
//         lcd.setCursor(0, 0);
//         lcd.print("ALL OK!");
//     } else {
//         switch (displayState) {
//             case 0: // Display risks
//                 if (riskDetected) {
//                     lcd.setCursor(0, 0);
//                     lcd.print("Current Risks:");
//                     lcd.setCursor(0, 1);
//                     lcd.print(riskStatus.substring(0, 16));
//                 } else {
//                     lcd.setCursor(0, 0);
//                     lcd.print("No Risks");
//                 }
//                 break;
//             case 1: // Display disasters
//                 if (disasterDetected) {
//                     lcd.setCursor(0, 0);
//                     lcd.print("DANGER:");
//                     lcd.setCursor(0, 1);
//                     lcd.print(disasterStatus.substring(0, 16));
//                 } else {
//                     lcd.setCursor(0, 0);
//                     lcd.print("No Disasters");
//                 }
//                 break;
//         }
//     }
// }

// Function to print the current display state to the serial monitor
// Function to update the serial log with current statuses
// void updateSerialLog(bool riskDetected, bool disasterDetected) {
//     Serial.println("-----");

//     // Display all ok if no risks or disasters detected
//     if (!riskDetected && !disasterDetected) {
//         Serial.println("ALL OK!");
//     } else {
//         switch (displayState) {
//             case 0: // Display risks
//                 if (riskDetected) {
//                     Serial.println("Current Risks:");
//                     Serial.println(riskStatus.substring(0, 16));
//                 } else {
//                     Serial.println("No Risks");
//                 }
//                 break;
//             case 1: // Display disasters
//                 if (disasterDetected) {
//                     Serial.println("DANGER:");
//                     Serial.println(disasterStatus.substring(0, 16));
//                 } else {
//                     Serial.println("No Disasters");
//                 }
//                 break;
//         }
//     }
    
//     Serial.println("-----");
// }

// void sendGetRequest(String url) {
//     HTTPClient http;
//     http.begin(url);
//     int httpCode = http.GET();
//     if (httpCode > 0) {
//         String payload = http.getString();
//         Serial.println(httpCode);
//         Serial.println(payload);
//     } else {
//         Serial.println("Error on HTTP request");
//     }
//     http.end();
// }

// JSON data
// {
//     "token": "*****",
//     "data": {
//         "type": "watch" | "danger" | "clear"
//         "name": "earthquake" | "fire" | "drought"
//         "temperature": 30,
//         "humidity": 40,
//         "water": 300,
//         "light": 1000,
//         "acceleration": 500
//     }
// }
void sendPostAction(String type, String name, float temperature, float humidity, int water, int light, float acceleration) {
    HTTPClient http;
    http.begin(serverName + "notifications");
    http.addHeader("Content-Type", "application/json");

    String data = "{\"token\":\"" + token + "\",\"data\":{\"type\":\"" + type + "\",\"name\":\"" + name + "\",\"temperature\":" + String(temperature) + ",\"humidity\":" + String(humidity) + ",\"water\":" + String(water) + ",\"light\":" + String(light) + ",\"acceleration\":" + String(acceleration) + "}}";
    
    int httpCode = http.POST(data);
    if (httpCode > 0) {
        String payload = http.getString();
        Serial.println(httpCode);
        Serial.println(payload);
    } else {
        Serial.println("Error on HTTP request");
    }
    http.end();
}

void setup() {
    Serial.begin(9600);
  
    // Initialize I2C and sensors
    Wire.begin(I2C_SDA, I2C_SCL);
    dht.begin();

    if (!bmi160.begin(BMI160GenClass::I2C_MODE, Wire, BMI160_I2C_ADDR)) {
        Serial.println(F("BMI160 allocation failed"));
        while (1);
    }

    pinMode(WATER_SENSOR_PIN, INPUT);
    pinMode(LIGHT_SENSOR_PIN, INPUT);

    // Initialize LCD
    // lcd.init();
    // lcd.backlight();
    // lcd.clear();
    
    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    // lcd.print("Connecting Wi-Fi...");
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("Connected");

    delay(5000);
    // send temp clear
    sendPostAction("clear", "fire", 0, 0, 0, 0, 0);
}

void loop() {
    // Rolling avg data
    if (readingSeconds % DATA_POINTS_PER_DAY == 0) {
        float avgTemp, avgHumidity;
        calculateRollingAverage(avgTemp, avgHumidity);
        adjustFireRiskThresholds(avgTemp, avgHumidity);
    }
    readingSeconds += 2;

    // Read sensor data
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    int waterSensorValue = analogRead(WATER_SENSOR_PIN);
    int lightSensorValue = analogRead(LIGHT_SENSOR_PIN);

    // Check for sensor errors
    if (isnan(humidity) || isnan(temperature)) {
        Serial.println(F("Failed to read from DHT sensor!"));
        // lcd.clear();
        // lcd.print("Sensor Error!");
        return;
    }

    // Read accelerometer data
    int xAccel = bmi160.getRotationX();
    int yAccel = bmi160.getRotationY();
    int zAccel = bmi160.getRotationZ();
    float totalAccel = sqrt(sq(xAccel) + sq(yAccel) + sq(zAccel));

    // DUMP SENSOR DATA FOR TESTING
    Serial.print("Temperature: ");
    Serial.println(temperature);
    Serial.print("Humidity: ");
    Serial.println(humidity);
    Serial.print("Water Sensor: ");
    Serial.println(waterSensorValue);
    Serial.print("Light Sensor: ");
    Serial.println(lightSensorValue);
    // Serial.print("Total Accel: ");
    // Serial.println(totalAccel);

        
    // Reset statuses
    // riskStatus = "";
    // disasterStatus = "";

    // Earthquake detection
    if (totalAccel > EARTHQUAKE_THRESHOLD) {
        earthquakeSamples++;
        // Reset the counter for readings below threshold since an earthquake reading was detected
        belowThresholdReadings = 0;
    } else {
        belowThresholdReadings++;
        if (belowThresholdReadings > EARTHQUAKE_READINGS_TO_CLEAR) {
            earthquakeSamples = 0;
        }
    }
    // Display earthquake alert if the threshold is reached
    if (earthquakeSamples > EARTHQUAKE_READINGS) {
        if (!earthquakeDetected) {
            sendPostAction("danger", "earthquake", temperature, humidity, waterSensorValue, lightSensorValue, totalAccel);
            earthquakeDetected = true;
        }
    } else if (earthquakeSamples > EARTHQUAKE_WARNING_READINGS) {
        if (!earthquakeWarning) {
            sendPostAction("watch", "earthquake", temperature, humidity, waterSensorValue, lightSensorValue, totalAccel);
            earthquakeWarning = true;
        }
    } else {
        if (earthquakeDetected) {
            sendPostAction("clear", "earthquake", temperature, humidity, waterSensorValue, lightSensorValue, totalAccel);
            earthquakeDetected = false;
        }
        earthquakeWarning = false;
    }


    // Fire detection and risk assessment
    if (temperature > FIRE_TEMP_THRESHOLD && lightSensorValue > FIRE_LIGHT_THRESHOLD) {
        if (!fireDetected) {
            sendPostAction("danger", "fire", temperature, humidity, waterSensorValue, lightSensorValue, totalAccel);
            fireDetected = true;
        }
    } else if (temperature > FIRE_RISK_TEMP && humidity < FIRE_RISK_HUMIDITY) {
        if (!fireWarning) {
            sendPostAction("watch", "fire", temperature, humidity, waterSensorValue, lightSensorValue, totalAccel);
            fireWarning = true;
        }
    } else {
        if (fireDetected) {
            sendPostAction("clear", "fire", temperature, humidity, waterSensorValue, lightSensorValue, totalAccel);
            fireDetected = false;
        }
        fireWarning = false;
    }

    // Drought detection
    bool soilMoistureFlag = waterSensorValue < DROUGHT_SOIL_MOISTURE_THRESHOLD;
    bool temperatureFlag = temperature > DROUGHT_TEMP_THRESHOLD;
    bool humidityFlag = humidity < DROUGHT_HUMIDITY_THRESHOLD;

    if (soilMoistureFlag || temperatureFlag || humidityFlag) {
        if (!droughtWarning) {
            sendPostAction("watch", "drought", temperature, humidity, waterSensorValue, lightSensorValue, totalAccel);
            droughtWarning = true;
        }
    } else {
        if (droughtWarning) {
            sendPostAction("clear", "drought", temperature, humidity, waterSensorValue, lightSensorValue, totalAccel);
            droughtWarning = false;
        }
    }
    
    // Update LCD display and print serial output
    // updateLCD(!riskStatus.equals(""), !disasterStatus.equals(""));
    // updateSerialLog(!riskStatus.equals(""), !disasterStatus.equals(""));

    // Cycle through screens
    delay(2000); // Change screen every 2 seconds
    // displayState = (displayState + 1) % 2;
}

