#include <DHT.h>
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <time.h>

// ----------- DHT22 -------------
#define DHTPIN D5
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ----------- Sensors -----------
const int soundPin = D6;
const int mqAnalogPin = A0;

// ----------- Motor Driver -------
#define MOTOR_IN1 D1
#define MOTOR_IN2 D2

// -------- Motor timing ----------
const int motorRunTime = 6000; // milliseconds motor runs to open/close curtain
bool curtainOpen = false;

// --------- MQ smoothing ---------
const int MQ_SMOOTH_COUNT = 8;
int mqSamples[MQ_SMOOTH_COUNT];
int mqIndex = 0;
bool mqFilled = false;

// -------- Sound tuning ----------
const int soundSampleWindow = 100;
const int noiseThreshold = 50;

// -------- WiFi ------------------
#define WIFI_SSID "abcd"
#define WIFI_PASSWORD "****"

// -------- Firebase --------------
FirebaseConfig config;
FirebaseAuth auth;
FirebaseData fbdo;

// -------- Time (NTP) ------------
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;
const int daylightOffset_sec = 0;

// -------- Logging interval ------
unsigned long lastLogTime = 0;
const unsigned long logInterval = 5 * 60 * 1000;


// ---------- Motor Control --------

void openCurtain() {

  if (curtainOpen) return;

  Serial.println("Opening Curtain");

  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);

  delay(motorRunTime);

  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);

  curtainOpen = true;
}

void closeCurtain() {

  if (!curtainOpen) return;

  Serial.println("Closing Curtain");

  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);

  delay(motorRunTime);

  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);

  curtainOpen = false;
}


// -------- MQ smoothing ----------
float mqGetSmoothed() {

  long sum = 0;
  int count = mqFilled ? MQ_SMOOTH_COUNT : mqIndex;

  if (count == 0)
    return 0;

  for (int i = 0; i < count; i++)
    sum += mqSamples[i];

  return (float)sum / count;
}


// -------- Setup -----------------

void setup() {

  Serial.begin(115200);

  dht.begin();

  pinMode(soundPin, INPUT);

  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);

  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);

  for (int i = 0; i < MQ_SMOOTH_COUNT; i++)
    mqSamples[i] = 0;

  // WiFi connection
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");

  // NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  Serial.print("Syncing time");

  time_t now = time(nullptr);

  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  Serial.println("\nTime Synced");

  // Firebase
  config.database_url = "****";
  config.signer.tokens.legacy_token = "****";

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}


// -------- Main Loop --------------

void loop() {

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT read error");
    delay(2000);
    return;
  }

  // MQ sensor
  int mqRaw = analogRead(mqAnalogPin);

  mqSamples[mqIndex++] = mqRaw;

  if (mqIndex >= MQ_SMOOTH_COUNT) {
    mqIndex = 0;
    mqFilled = true;
  }

  float mqAvg = mqGetSmoothed();

  // Sound sensor
  int triggerCount = 0;

  unsigned long startTime = millis();

  while (millis() - startTime < soundSampleWindow) {

    if (digitalRead(soundPin) == LOW)
      triggerCount++;

    delayMicroseconds(50);
  }

  // Air quality classification
  String airQualityStatus;

  if (mqAvg < 200)
    airQualityStatus = "GOOD";
  else if (mqAvg < 400)
    airQualityStatus = "MODERATE";
  else
    airQualityStatus = "POOR";

  // Sound classification
  String soundStatus = (triggerCount > noiseThreshold) ? "NOISY" : "QUIET";

  // -------- Curtain Automation --------

  if (temperature > 30) {
    openCurtain();
  }

  if (temperature < 25) {
    closeCurtain();
  }

  time_t now = time(nullptr);

  // -------- Firebase Realtime Upload -----

  FirebaseJson json;

  json.set("temperature", temperature);
  json.set("humidity", humidity);
  json.set("airQualityRaw", mqAvg);
  json.set("airQualityStatus", airQualityStatus);
  json.set("soundTriggers", triggerCount);
  json.set("soundStatus", soundStatus);
  json.set("curtainOpen", curtainOpen);
  json.set("lastUpdate", (unsigned long)now);

  if (Firebase.setJSON(fbdo, "/environment", json)) {
    Serial.println("Firebase updated");
  } else {
    Serial.println("Firebase Error: " + fbdo.errorReason());
  }

  // -------- Historical Logging ----------

  unsigned long currentMillis = millis();

  if (currentMillis - lastLogTime >= logInterval) {

    lastLogTime = currentMillis;

    String timestamp = String(now);
    String path = "/environmentHistory/" + timestamp;

    Firebase.setJSON(fbdo, path.c_str(), json);

    Serial.println("Historical data saved");
  }

  delay(2000);
}
