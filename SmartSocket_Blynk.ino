// --- БІБЛІОТЕКИ ---
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <time.h>
// ------------------

// --- ДАНІ ДЛЯ ПІДКЛЮЧЕННЯ ДО BLYNK ЗАСТОСУНКУ ---
char auth[] = "***-***";   // маркер автентифікації
char ssid[] = "***";                                // назва мережі Wi-Fi (2.4 Ghz)
char pass[] = "***";                  // пароль від мережі Wi-Fi
// ------------------------------------------------

#define RELAY_PIN D2  // пін ESP8266 на який підключено реле

// Глобальні змінні для часу (розкладу) для автоматизованого керування роботою живлення підключеного приладу
volatile uint32_t startTimeSec = 0;   // час старта (сек)
volatile uint32_t stopTimeSec  = 0;   // час зупинки (сек)
volatile int scheduledDay = -1;       // 0 = Sun ... 6 = Sat, 7 – щодня

bool scheduleActive = false;      // ← прапорець: чи активний розклад?
bool autoRelayState = false;      // ← поточний стан реле (керується розкладом або вручну)
bool lastReportedState = false;   // ← для відстеження попереднього надісланого стану на V2

// Функція оновлення статусу живлення (ЗМІНА СТАТУСУ) (V2)
void updatePowerStatus(bool fromSchedule) {
  if (lastReportedState != autoRelayState) {
    lastReportedState = autoRelayState;

    String statusText = autoRelayState ? "Power ON" : "Power OFF";
    statusText += fromSchedule ? " (schedule)" : " (manual)";
    
    Blynk.virtualWrite(V2, statusText);
    Blynk.setProperty(V2, "color", autoRelayState ? "#00FF00" : "#FF0000");

    Serial.print("Статус живлення: ");
    Serial.println(statusText);
  }
}

// Ручне керування живленням через кнопку (V0) – спрацює лише якщо розклад не активний (не заданий)
BLYNK_WRITE(V0) {
  if (!scheduleActive) {
    int manualState = param.asInt();
    autoRelayState = (manualState != 0);
    digitalWrite(RELAY_PIN, autoRelayState ? LOW : HIGH);
    Serial.print("Ручне керування реле: ");
    Serial.println(autoRelayState ? "ON" : "OFF");
    updatePowerStatus(false);
  } else {
    Serial.println("Ручне керування ігнорується, бо встановлено розклад");
  }
}

// Зчитування розкладу з віджета Time Input на V1
BLYNK_WRITE(V1) {
  // param[0] – час початку (сек)
  // param[1] – час завершення (сек)
  // param[2] – рядок з часовим поясом
  // param[3] – день тижня (0..6 або 7 — для кожного дня)
  // param[4] – offset від UTC (сек)
  startTimeSec = param[0].asInt();
  stopTimeSec  = param[1].asInt();
  String tz    = param[2].asStr();
  scheduledDay = param[3].asInt();
  int utcOffset = param[4].asInt();

  scheduleActive = !(startTimeSec == 0 && stopTimeSec == 0);  // ← якщо обидва значення часу дорівнюють 0 – вважаємо, що розклад не активний
  
  if (!scheduleActive) Blynk.setProperty(V0, "color", "#0085FE"); // ← повертаємо стандартний колір кнопки
  else Blynk.setProperty(V0, "color", "#004382AA"); // ← змінюємо колір кнопки на темно-синій, щоб показати, що вона неактивна
  
  Serial.println("Отримано новий розклад:");
  Serial.print("СТАРТ (сек): "); Serial.println(startTimeSec);
  Serial.print("СТОП  (сек): "); Serial.println(stopTimeSec);
  Serial.print("Часовий пояс: "); Serial.println(tz);
  Serial.print("День тижня: "); Serial.println(scheduledDay);
  Serial.print("offset UTC (сек): "); Serial.println(utcOffset);
  Serial.print("РОЗКЛАД АКТИВНИЙ: ");
  Serial.println(scheduleActive ? "YES" : "NO");
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  Serial.begin(115200);   // ← КІЛЬКІСТЬ БОД ДЛЯ ESP8266

  // Підключаємося до Wi-Fi
  WiFi.begin(ssid, pass);
  Serial.print("Підключення до Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi HAS CONNECTED SUCCESSFULLY");

  // НАЛАШТУЄМО НЕОБХІДНИЙ ЧАС ДЛЯ Europe/Kiev: UTC+10800 сек
  configTime(10800, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Time is synchronized via NTP");

  Blynk.begin(auth, ssid, pass, "blynk.cloud", 80);
  Serial.println("BLYNK HAS CONNECTED SUCCESSFULLY");
}

void loop() {
  Blynk.run();

  // Якщо розклад активний, оновлюємо стан реле відповідно до розкладу
  if (scheduleActive) {
    // Отримуємо поточний час (з урахуванням часового поясу)
    time_t now = time(nullptr);
    struct tm *tmInfo = localtime(&now);
    int currentSec = tmInfo->tm_hour * 3600 + tmInfo->tm_min * 60 + tmInfo->tm_sec;
    int currentDOW = tmInfo->tm_wday;  // 0 = неділя ... 6 = субота
  
    bool scheduleOn = false;
    // Якщо обрано "щодня" (7) або поточний день відповідає розкладу
    if (scheduledDay == 7 || currentDOW == scheduledDay) {
      if (startTimeSec <= stopTimeSec)
        scheduleOn = (currentSec >= startTimeSec && currentSec < stopTimeSec);
      else
        scheduleOn = (currentSec >= startTimeSec || currentSec < stopTimeSec);
    } 
  
    if (scheduleOn != autoRelayState) {
      autoRelayState = scheduleOn;
      digitalWrite(RELAY_PIN, autoRelayState ? LOW : HIGH);
      Serial.print("Реле ");
      if (!autoRelayState) Blynk.setProperty(V0, "color", "#0085FE"); // ← повертаємо стандартний колір кнопки
      else Blynk.setProperty(V0, "color", "#004382AA"); // ← змінюємо колір кнопки на темно-синій, щоб показати, що вона неактивна
      Serial.println(autoRelayState ? "ON за розкладом" : "OFF за розкладом");
      updatePowerStatus(true);
    }
  }
  
  delay(1000);
}
