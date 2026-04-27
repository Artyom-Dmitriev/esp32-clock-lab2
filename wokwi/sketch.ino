// ================================================================
// Лабораторная работа №2, вариант 5
// "Аналоговые часы с веб-интерфейсом"
//
// Плата:  NodeMCU-32S (ESP32-WROOM-32)
// Среда:  PlatformIO + Arduino framework
//
// ----------------------------------------------------------------
// СПИСОК ВОЗМОЖНОСТЕЙ:
//
// БАЗОВЫЕ (по заданию варианта 5):
//   1. Время отсчитывается через millis() и каждую секунду печатается
//      в Serial Monitor.
//   2. Кнопка BOOT (GPIO 0) — аппаратный сброс времени на 00:00:00.
//   3. Веб-интерфейс на http://192.168.4.1 :
//        - аналоговый циферблат + цифровое время (синхронизированы),
//        - форма установки времени,
//        - количество миллисекунд с момента запуска,
//        - кнопка программного сброса.
//
// ДОПОЛНИТЕЛЬНЫЕ ФИЧИ:
//   4. NVS (Preferences) — время и настройки сохраняются во flash и
//      переживают перезагрузку платы.
//   5. mDNS — устройство доступно по http://esp32.local (не нужно
//      запоминать IP).
//   6. Будильник — пищит зуммером + мигает LED + показывает алерт
//      на странице. Хранится в NVS.
//   7. Таймер обратного отсчёта — задаёшь N секунд, по истечении
//      срабатывает звуковой сигнал.
//   8. Лог событий — кольцевой буфер на 10 последних событий
//      (нажатия кнопки, установка времени, сбросы и т.д.),
//      отображается на странице.
//   9. JSON API /api/time и /api/log для программного доступа.
//  10. Пьезо-зуммер на GPIO 25 — звуковая индикация будильника/таймера.
// ================================================================

#include <Arduino.h>          // базовый Arduino API (Serial, pinMode, millis, ...)
#include <WiFi.h>             // Wi-Fi функционал ESP32
#include <WebServer.h>        // встроенный HTTP-сервер на 80 порту
#include <ESPmDNS.h>          // mDNS — резолвинг "esp32.local" в локальной сети
#include <Preferences.h>      // удобный API поверх NVS (Non-Volatile Storage)
#include "web_page.h"         // наша HTML-страница (PROGMEM-строка INDEX_HTML)

// ================================================================
// КОНСТАНТЫ И ПИНЫ
// ================================================================

// --- Wi-Fi точка доступа ---
// SSID и пароль для собственной точки доступа ESP32.
// Подключаешься телефоном/ноутом к этой сети — никакой роутер не нужен.
const char* AP_SSID     = "ESP32_Clock";
const char* AP_PASSWORD = "12345678";    // WPA2 требует минимум 8 символов

// --- mDNS-имя ---
// После запуска устройство доступно по адресу http://esp32.local
// (работает на телефонах с iOS/Android и десктопах с Bonjour).
const char* MDNS_HOST   = "esp32";

// --- Пины ---
// GPIO 0  — кнопка BOOT, уже распаяна на плате NodeMCU-32S
//           (при нажатии пин притягивается к GND, т.е. читается LOW).
// GPIO 2  — встроенный синий светодиод на плате.
// GPIO 25 — пьезо-зуммер (DAC-пин ESP32, подходит для tone()).
//           В Wokwi-симуляции к этому пину подключим виртуальный buzzer.
const int BUTTON_PIN = 0;
const int LED_PIN    = 2;
const int BUZZER_PIN = 25;

// --- Тайминги ---
const unsigned long DEBOUNCE_MS         = 50;     // антидребезг кнопки
const unsigned long ALARM_DURATION_MS   = 60000;  // как долго звонит будильник (1 мин)
const unsigned long LONG_PRESS_MS       = 2000;   // длинное нажатие для печати лога

// --- Размер кольцевого буфера лога ---
const int LOG_SIZE = 10;

// ================================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ И ПЕРЕМЕННЫЕ
// ================================================================

// HTTP-сервер на 80 порту (стандарт HTTP).
WebServer server(80);

// Объект Preferences для работы с NVS — встроенным хранилищем настроек.
// NVS — это часть flash-памяти, специально размеченная для key-value пар.
// Данные переживают перезагрузку и даже отключение питания.
Preferences prefs;

// === Хранение времени ===
// Идея: запоминаем момент millis(), когда время было "установлено",
// и базовое время в этот момент (в секундах от полуночи).
// Тогда текущее время = (baseSeconds + (millis() - syncMillis)/1000) % 86400.
// Так не накапливается ошибка при долгой работе — время идёт ровно с millis().
unsigned long syncMillis  = 0;     // millis() в момент последней синхронизации
long          baseSeconds = 0;     // базовое время (сек от 00:00:00) в момент syncMillis

// === Будильник ===
int alarmHour       = -1;          // -1 означает "выключен"
int alarmMinute     = 0;
bool alarmTriggered = false;       // сработал ли сейчас (для веб-интерфейса)
unsigned long alarmStartMillis = 0;// когда сработал — для автоотключения

// === Таймер обратного отсчёта ===
// secondsLeft >= 0 — таймер активен; -1 означает "не запущен".
long timerSecondsLeft       = -1;
unsigned long timerLastTick = 0;   // millis() последнего уменьшения счётчика
bool timerFired             = false;// сработал ли (значит надо пищать)
unsigned long timerFiredAt  = 0;

// === Состояние кнопки (для антидребезга и определения длинного нажатия) ===
int lastButtonState = HIGH;        // сырой предыдущий уровень пина
int stableState     = HIGH;        // отфильтрованное состояние (после антидребезга)
unsigned long lastDebounceTime = 0;
unsigned long buttonPressedAt  = 0;// когда нажали (для определения длинного нажатия)

// === Печать в Serial раз в секунду ===
unsigned long lastSerialPrint = 0;

// ================================================================
// ЛОГ СОБЫТИЙ — КОЛЬЦЕВОЙ БУФЕР
// ================================================================
// Простая структура: массив строк фиксированного размера + индекс
// "куда писать дальше". Когда индекс достигает конца — заворачивается
// на 0 и затирает самые старые записи. Это идиоматический подход для
// микроконтроллеров — не выделяем память динамически.

struct LogEntry {
  unsigned long timestamp;          // millis() в момент события
  char message[48];                 // короткое описание (ровно 48 байт)
};

LogEntry eventLog[LOG_SIZE];        // массив записей
int      logHead   = 0;             // куда писать следующее событие
int      logCount  = 0;             // сколько всего записей (≤ LOG_SIZE)

// Добавить событие в лог. Дублируется в Serial для удобства отладки.
void addLog(const char* msg) {
  eventLog[logHead].timestamp = millis();
  // strncpy с явным завершающим нулём — безопасный способ копирования
  // в фиксированный буфер (защита от переполнения).
  strncpy(eventLog[logHead].message, msg, sizeof(eventLog[logHead].message) - 1);
  eventLog[logHead].message[sizeof(eventLog[logHead].message) - 1] = '\0';

  // Двигаем head по кругу.
  logHead = (logHead + 1) % LOG_SIZE;
  if (logCount < LOG_SIZE) logCount++;

  // Параллельно печатаем в Serial.
  Serial.print(F("[LOG] "));
  Serial.println(msg);
}

// ================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ВРЕМЕНИ
// ================================================================

// Возвращает текущее время в секундах от полуночи (0..86399).
long currentSeconds() {
  // (millis() - syncMillis) даёт прошедшие миллисекунды.
  // Делим на 1000 — получаем секунды.
  unsigned long elapsedSec = (millis() - syncMillis) / 1000UL;
  // Прибавляем базу и берём остаток от 86400 (секунд в сутках).
  long total = (baseSeconds + (long)elapsedSec) % 86400L;
  if (total < 0) total += 86400L;   // подстраховка, отрицательных быть не должно
  return total;
}

// Разбивает секунды на часы/минуты/секунды.
void splitTime(long secs, int &h, int &m, int &s) {
  h = secs / 3600;
  m = (secs / 60) % 60;
  s = secs % 60;
}

// Парсит строку "HH:MM" или "HH:MM:SS" в секунды от полуночи.
// Возвращает -1, если строка некорректная.
long parseTimeString(const String &s) {
  if (s.length() < 4) return -1;

  int firstColon = s.indexOf(':');
  if (firstColon < 0) return -1;

  int hours   = s.substring(0, firstColon).toInt();
  int minutes = 0;
  int seconds = 0;

  // Ищем второе двоеточие (для секунд).
  int secondColon = s.indexOf(':', firstColon + 1);
  if (secondColon > 0) {
    minutes = s.substring(firstColon + 1, secondColon).toInt();
    seconds = s.substring(secondColon + 1).toInt();
  } else {
    minutes = s.substring(firstColon + 1).toInt();
  }

  // Валидация диапазонов (RFC простоты).
  if (hours   < 0 || hours   > 23) return -1;
  if (minutes < 0 || minutes > 59) return -1;
  if (seconds < 0 || seconds > 59) return -1;

  return (long)hours * 3600L + minutes * 60L + seconds;
}

// ================================================================
// СОХРАНЕНИЕ / ВОССТАНОВЛЕНИЕ В NVS (Preferences)
// ================================================================
//
// NVS = Non-Volatile Storage. Это участок flash-памяти ESP32, в который
// можно писать пары "ключ-значение". В отличие от RAM, данные сохраняются
// после выключения питания. Переписывается ограниченное число раз
// (~100k циклов на ячейку), но для нашей задачи — это более чем достаточно.

void saveToNVS() {
  // Открываем "namespace" под именем "clock" (как папка в файловой системе).
  // Второй параметр false = открыть для записи.
  prefs.begin("clock", false);

  // Ключи короткие — это требование NVS (макс. 15 символов).
  prefs.putLong("base",       baseSeconds);
  prefs.putULong("syncOffset", millis() - syncMillis); // прошло мс с момента синхронизации
  prefs.putInt("alH",         alarmHour);
  prefs.putInt("alM",         alarmMinute);

  prefs.end(); // закрываем namespace
}

void loadFromNVS() {
  prefs.begin("clock", true); // true = read-only (быстрее)

  // Если ключа нет — возвращается значение по умолчанию (второй параметр).
  baseSeconds      = prefs.getLong("base",       0);
  unsigned long sv = prefs.getULong("syncOffset", 0);
  alarmHour        = prefs.getInt("alH",         -1);
  alarmMinute      = prefs.getInt("alM",          0);

  prefs.end();

  // syncMillis выставляем так, чтобы текущий "millis()" совпал по фазе.
  // Замечание: пока ESP32 был выключен, время "застыло" — это нормальное
  // поведение для часов, не имеющих батарейки реального времени (RTC).
  syncMillis = millis() - sv;
}

// ================================================================
// УСТАНОВКА ВРЕМЕНИ И СБРОС
// ================================================================

// Устанавливает базовое время. Часы будут показывать timeSeconds в текущий
// момент millis(). Также сохраняет в NVS, чтобы пережило перезагрузку.
void setTimeSeconds(long timeSeconds) {
  baseSeconds = timeSeconds;
  syncMillis  = millis();
  saveToNVS();
}

// Сбрасывает время на 00:00:00 (с записью в NVS и логом).
void resetTimeToZero() {
  setTimeSeconds(0);
  addLog("Время сброшено на 00:00:00");
}

// ================================================================
// УПРАВЛЕНИЕ ЗВУКОМ (зуммер)
// ================================================================
//
// Используем встроенную в Arduino функцию tone() — она генерирует
// прямоугольную волну на пине заданной частоты. ESP32 поддерживает
// её через LEDC (LED Controller, аппаратный PWM).

void buzzerOn(int freqHz = 1000) {
  tone(BUZZER_PIN, freqHz);     // непрерывный звук
}
void buzzerOff() {
  noTone(BUZZER_PIN);            // выключить
  digitalWrite(BUZZER_PIN, LOW); // на всякий случай — низкий уровень
}

// ================================================================
// HTTP-ОБРАБОТЧИКИ
// ================================================================

// GET /  — отдаём главную HTML-страницу из PROGMEM.
// send_P — версия send для строк, лежащих в PROGMEM (flash).
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// GET /api/time — отдаём JSON с текущим временем для AJAX.
// Формат: {"h":12,"m":34,"s":56,"ms":12345,"alarm":false,"timer":42,"alarmH":7,"alarmM":0}
void handleApiTime() {
  int h, m, s;
  splitTime(currentSeconds(), h, m, s);

  // Используем char-буфер вместо String — быстрее, не фрагментирует heap.
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"h\":%d,\"m\":%d,\"s\":%d,\"ms\":%lu,"
    "\"alarm\":%s,\"timer\":%ld,\"alarmH\":%d,\"alarmM\":%d,"
    "\"timerFired\":%s}",
    h, m, s, millis(),
    alarmTriggered ? "true" : "false",
    timerSecondsLeft,
    alarmHour, alarmMinute,
    timerFired ? "true" : "false");

  server.send(200, "application/json", buf);
}

// GET /api/log — отдаёт JSON с последними событиями.
// Формат: [{"t":12345,"m":"..."}, ...] (от старых к новым)
void handleApiLog() {
  // Динамическая String — для лога допустимо: вызывается редко, размер невелик.
  String json = "[";

  // Восстанавливаем хронологический порядок: начинаем с самой старой записи.
  // Если буфер ещё не заполнен — стартуем с индекса 0;
  // иначе — с logHead (это позиция, которую мы сейчас перезапишем след. раз,
  // т.е. там лежит самая старая запись).
  int start = (logCount < LOG_SIZE) ? 0 : logHead;

  for (int i = 0; i < logCount; i++) {
    int idx = (start + i) % LOG_SIZE;
    if (i > 0) json += ',';
    json += "{\"t\":";
    json += eventLog[idx].timestamp;
    json += ",\"m\":\"";
    json += eventLog[idx].message;
    json += "\"}";
  }
  json += ']';

  server.send(200, "application/json", json);
}

// GET /set?time=HH:MM:SS — установка времени с веб-формы.
void handleSetTime() {
  if (!server.hasArg("time")) {
    server.send(400, "text/plain", "Missing 'time' parameter");
    return;
  }

  String timeStr = server.arg("time");
  long secs = parseTimeString(timeStr);
  if (secs < 0) {
    server.send(400, "text/plain", "Invalid time format. Use HH:MM or HH:MM:SS");
    return;
  }

  setTimeSeconds(secs);

  char msg[48];
  snprintf(msg, sizeof(msg), "Время установлено: %s", timeStr.c_str());
  addLog(msg);

  server.send(200, "text/plain", "OK");
}

// GET /reset — сброс времени.
void handleReset() {
  resetTimeToZero();
  server.send(200, "text/plain", "OK");
}

// GET /alarm?time=HH:MM (или time=off) — установка/выключение будильника.
void handleSetAlarm() {
  if (!server.hasArg("time")) {
    server.send(400, "text/plain", "Missing 'time'");
    return;
  }

  String t = server.arg("time");

  // "off" — выключаем будильник.
  if (t == "off") {
    alarmHour = -1;
    alarmTriggered = false;
    digitalWrite(LED_PIN, LOW);
    buzzerOff();
    saveToNVS();
    addLog("Будильник выключен");
    server.send(200, "text/plain", "OFF");
    return;
  }

  long secs = parseTimeString(t);
  if (secs < 0) {
    server.send(400, "text/plain", "Invalid time");
    return;
  }

  alarmHour      = secs / 3600;
  alarmMinute    = (secs / 60) % 60;
  alarmTriggered = false;          // сбрасываем флаг — ждём нового срабатывания
  saveToNVS();

  char msg[48];
  snprintf(msg, sizeof(msg), "Будильник на %02d:%02d", alarmHour, alarmMinute);
  addLog(msg);

  server.send(200, "text/plain", "OK");
}

// GET /timer?seconds=N — запуск/остановка таймера обратного отсчёта.
// seconds=0 или отсутствие параметра останавливают таймер.
void handleTimer() {
  if (!server.hasArg("seconds")) {
    server.send(400, "text/plain", "Missing 'seconds'");
    return;
  }

  long n = server.arg("seconds").toInt();

  if (n <= 0) {
    // Остановить таймер.
    timerSecondsLeft = -1;
    timerFired       = false;
    buzzerOff();
    addLog("Таймер остановлен");
    server.send(200, "text/plain", "STOPPED");
    return;
  }

  // Ограничим разумной величиной (24 часа), чтобы случайно не задать гигантское значение.
  if (n > 86400) n = 86400;

  timerSecondsLeft = n;
  timerLastTick    = millis();
  timerFired       = false;

  char msg[48];
  snprintf(msg, sizeof(msg), "Таймер: %ld сек", n);
  addLog(msg);

  server.send(200, "text/plain", "OK");
}

// Обработчик 404 — для любых неизвестных URL.
void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ================================================================
// SETUP — выполняется один раз при старте/ресете
// ================================================================
void setup() {
  // Инициализация Serial для отладочного вывода (115200 — стандарт).
  Serial.begin(115200);
  delay(200);                   // даём порту "проснуться" перед первой печатью
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("  ESP32 Аналоговые часы с веб-интерфейсом"));
  Serial.println(F("  Лабораторная работа №2, вариант 5"));
  Serial.println(F("=================================================="));

  // Конфигурируем пины.
  pinMode(BUTTON_PIN, INPUT_PULLUP);   // подтяжка к питанию: нажатие = LOW
  pinMode(LED_PIN,    OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN,    LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Восстанавливаем время и будильник из NVS (если были сохранены).
  loadFromNVS();
  Serial.print(F("[NVS] Восстановлено время: "));
  int h, m, s; splitTime(currentSeconds(), h, m, s);
  Serial.printf("%02d:%02d:%02d\n", h, m, s);
  if (alarmHour >= 0) {
    Serial.printf("[NVS] Восстановлен будильник: %02d:%02d\n", alarmHour, alarmMinute);
  }

  // === Поднимаем Wi-Fi точку доступа ===
  // ESP32 сам становится "роутером" — никакой внешний интернет не нужен.
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (apOk) {
    Serial.print(F("[WIFI] Точка доступа: "));
    Serial.print(AP_SSID);
    Serial.print(F(" / "));
    Serial.println(AP_PASSWORD);
    Serial.print(F("[WIFI] IP адрес: http://"));
    Serial.println(WiFi.softAPIP());  // обычно 192.168.4.1
  } else {
    Serial.println(F("[WIFI] ОШИБКА: не удалось поднять точку доступа!"));
  }

  // === mDNS — короткое имя в локальной сети ===
  // После этого устройство откликается на http://esp32.local
  // (если устройство-клиент поддерживает mDNS, что верно для большинства ОС).
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print(F("[MDNS] Доступно по http://"));
    Serial.print(MDNS_HOST);
    Serial.println(F(".local"));
  } else {
    Serial.println(F("[MDNS] ОШИБКА: mDNS не запустился"));
  }

  // === Регистрируем HTTP-маршруты ===
  server.on("/",         handleRoot);       // главная страница
  server.on("/api/time", handleApiTime);    // JSON с временем (AJAX-опрос)
  server.on("/api/log",  handleApiLog);     // JSON с логом событий
  server.on("/set",      handleSetTime);    // установка времени
  server.on("/reset",    handleReset);      // сброс
  server.on("/alarm",    handleSetAlarm);   // будильник
  server.on("/timer",    handleTimer);      // таймер обратного отсчёта
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("[HTTP] Сервер запущен на порту 80"));
  Serial.println(F("=================================================="));

  addLog("Система запущена");

  // Короткий сигнал при старте — "пик-пик" зуммером + вспышка LED.
  // Это удобно: видно, что прошивка ожила, даже если Serial не подключён.
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_PIN, HIGH);
    buzzerOn(2000);                 // 2 кГц — приятный высокий писк
    delay(80);
    digitalWrite(LED_PIN, LOW);
    buzzerOff();
    delay(80);
  }
}

// ================================================================
// LOOP — основной цикл, выполняется бесконечно
// ================================================================
void loop() {
  // 1) Обрабатываем входящие HTTP-запросы. Без этого вызова сервер мёртв.
  server.handleClient();

  unsigned long now = millis();

  // ----------------------------------------------------------------
  // 2) Кнопка с антидребезгом + детект длинного нажатия.
  // ----------------------------------------------------------------
  int reading = digitalRead(BUTTON_PIN);

  // Если уровень сменился — обнуляем таймер антидребезга.
  if (reading != lastButtonState) {
    lastDebounceTime = now;
  }
  // Реагируем только если состояние держится дольше DEBOUNCE_MS.
  if ((now - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        // Кнопка только что нажата. Запоминаем момент — для длинного нажатия.
        buttonPressedAt = now;
      } else {
        // Кнопка только что отпущена.
        unsigned long held = now - buttonPressedAt;
        if (held >= LONG_PRESS_MS) {
          // Длинное нажатие — печатаем диагностику в Serial.
          Serial.println(F("[BTN] Длинное нажатие — диагностика:"));
          Serial.printf("       Heap free: %u bytes\n", ESP.getFreeHeap());
          Serial.printf("       Uptime:    %lu ms\n", now);
          Serial.printf("       Clients:   %d connected\n", WiFi.softAPgetStationNum());
          addLog("Диагностика (длинное нажатие)");
        } else {
          // Короткое нажатие — обычный сброс времени.
          resetTimeToZero();
          // Короткая вспышка LED + писк, чтобы пользователь увидел реакцию.
          digitalWrite(LED_PIN, HIGH);
          buzzerOn(1500);
          delay(80);
          digitalWrite(LED_PIN, LOW);
          buzzerOff();
        }
      }
    }
  }
  lastButtonState = reading;

  // ----------------------------------------------------------------
  // 3) Раз в секунду печатаем время в Serial-монитор (требование задания).
  // ----------------------------------------------------------------
  if (now - lastSerialPrint >= 1000) {
    lastSerialPrint = now;
    int h, m, s;
    splitTime(currentSeconds(), h, m, s);
    Serial.printf("[TIME] %02d:%02d:%02d  |  uptime: %lu ms\n", h, m, s, now);
  }

  // ----------------------------------------------------------------
  // 4) Проверка будильника.
  //    Срабатывает в начале минуты (когда секунды = 0) и держится 60 секунд,
  //    чтобы веб-страница успела показать уведомление.
  // ----------------------------------------------------------------
  if (alarmHour >= 0) {
    int h, m, s;
    splitTime(currentSeconds(), h, m, s);

    if (!alarmTriggered && h == alarmHour && m == alarmMinute && s == 0) {
      alarmTriggered   = true;
      alarmStartMillis = now;
      addLog("!!! БУДИЛЬНИК !!!");
    }

    // Автоотключение через ALARM_DURATION_MS после срабатывания.
    if (alarmTriggered && (now - alarmStartMillis > ALARM_DURATION_MS)) {
      alarmTriggered = false;
      digitalWrite(LED_PIN, LOW);
      buzzerOff();
    }

    // Пока будильник звенит — мигаем LED (раз в 250 мс) и пищим
    // прерывисто (200 мс пик / 200 мс тишина).
    if (alarmTriggered) {
      digitalWrite(LED_PIN, (now / 250) % 2);
      if ((now / 200) % 2) buzzerOn(2500);
      else                 buzzerOff();
    }
  }

  // ----------------------------------------------------------------
  // 5) Таймер обратного отсчёта.
  // ----------------------------------------------------------------
  if (timerSecondsLeft > 0) {
    // Уменьшаем счётчик раз в секунду.
    if (now - timerLastTick >= 1000) {
      timerLastTick = now;
      timerSecondsLeft--;

      if (timerSecondsLeft == 0) {
        // Сработал!
        timerFired   = true;
        timerFiredAt = now;
        addLog("!!! ТАЙМЕР !!!");
      }
    }
  }

  // Обработка сработавшего таймера (5 секунд звуковой сигнал).
  if (timerFired) {
    if (now - timerFiredAt < 5000UL) {
      // Чередуем две частоты — получается "сирена".
      int freq = ((now / 300) % 2) ? 1500 : 2200;
      buzzerOn(freq);
      digitalWrite(LED_PIN, (now / 150) % 2);
    } else {
      // Прошло 5 секунд — выключаем сирену и сбрасываем таймер.
      timerFired       = false;
      timerSecondsLeft = -1;
      buzzerOff();
      digitalWrite(LED_PIN, LOW);
    }
  }
}
