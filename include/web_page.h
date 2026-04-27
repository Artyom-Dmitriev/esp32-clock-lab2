// ===============================================================
// web_page.h
// HTML + CSS + JavaScript для веб-интерфейса аналоговых часов.
// Вся страница хранится во flash-памяти ESP32 (PROGMEM),
// чтобы не занимать оперативку и работать без интернета —
// никаких внешних CDN, всё лежит прямо в прошивке.
// ===============================================================

#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <Arduino.h>

// R"rawliteral( ... )rawliteral" — это "сырой" строковый литерал C++11,
// в нём не нужно экранировать кавычки и переносы строк.
// PROGMEM кладёт строку во flash-память, а не в RAM.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <!-- Адаптивная вёрстка для телефона: ширина = ширине устройства -->
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 — Аналоговые часы</title>
  <style>
    /* === CSS-переменные для тем (светлая/тёмная) === */
    :root {
      --bg: #f0f4f8;
      --card: #ffffff;
      --text: #1a202c;
      --muted: #718096;
      --accent: #3182ce;
      --danger: #e53e3e;
      --success: #38a169;
      --warning: #ed8936;
      --shadow: rgba(0,0,0,0.08);
      --face: #ffffff;
      --hand: #1a202c;
      --second: #e53e3e;
      --border: #e2e8f0;
    }
    /* Если у body класс "dark" — переопределяем цвета */
    body.dark {
      --bg: #1a202c;
      --card: #2d3748;
      --text: #f7fafc;
      --muted: #a0aec0;
      --accent: #63b3ed;
      --shadow: rgba(0,0,0,0.4);
      --face: #2d3748;
      --hand: #f7fafc;
      --border: #4a5568;
    }

    /* Сброс стандартных отступов */
    * { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      font-family: -apple-system, "Segoe UI", Roboto, sans-serif;
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      padding: 20px;
      transition: background 0.3s, color 0.3s; /* плавная смена темы */
    }

    .container { max-width: 720px; margin: 0 auto; }

    /* Шапка: заголовок и переключатель темы */
    header {
      display: flex; justify-content: space-between; align-items: center;
      margin-bottom: 24px;
    }
    h1 { font-size: 22px; font-weight: 600; }
    h2 {
      font-size: 16px; margin-bottom: 12px;
      color: var(--muted); font-weight: 500;
    }

    /* Кнопка темы (солнце/луна) */
    #themeBtn {
      background: var(--card); border: none; cursor: pointer;
      width: 40px; height: 40px; border-radius: 50%;
      box-shadow: 0 2px 8px var(--shadow); font-size: 18px;
    }

    /* Карточка — общая обёртка для блоков */
    .card {
      background: var(--card); border-radius: 16px; padding: 20px;
      margin-bottom: 16px; box-shadow: 0 2px 12px var(--shadow);
    }

    /* === Аналоговый циферблат (SVG) === */
    .clock-wrapper { display: flex; justify-content: center; align-items: center; }
    svg.clock { width: 280px; height: 280px; }
    /* Стрелки — линии. transform-origin в центре циферблата (100,100). */
    .hand {
      stroke-linecap: round;
      transform-origin: 100px 100px;
      /* плавный поворот; секундной отключим в .second (см. ниже) */
      transition: transform 0.3s cubic-bezier(.4,2,.6,1);
    }
    .hand.second { transition: none; } /* секундная — без анимации, чтобы тикала чётко */

    /* === Цифровое отображение под циферблатом === */
    .digital {
      font-family: "Courier New", monospace;
      font-size: 42px; font-weight: bold;
      text-align: center; color: var(--accent);
      margin-top: 12px; letter-spacing: 2px;
    }
    .uptime {
      text-align: center; color: var(--muted);
      font-size: 14px; margin-top: 4px;
    }

    /* === Формы и кнопки === */
    label { display: block; font-size: 13px; color: var(--muted); margin-bottom: 6px; }
    input[type="time"], input[type="text"], input[type="number"] {
      width: 100%; padding: 10px 12px; font-size: 16px;
      border: 1px solid var(--border); border-radius: 8px;
      background: var(--bg); color: var(--text);
    }
    .btn {
      padding: 10px 20px; font-size: 15px; font-weight: 500;
      border: none; border-radius: 8px; cursor: pointer;
      color: white; background: var(--accent);
      margin-top: 10px; transition: opacity 0.2s;
    }
    .btn:hover { opacity: 0.9; }
    .btn.danger  { background: var(--danger);  }
    .btn.success { background: var(--success); }
    .btn.warning { background: var(--warning); }

    /* Сетка форм — на широком экране в 2 колонки */
    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    @media (max-width: 500px) { .grid-2 { grid-template-columns: 1fr; } }

    /* Уведомление о срабатывании будильника/таймера — мигает */
    .alert {
      display: none;
      background: var(--danger); color: white;
      padding: 12px; border-radius: 8px; text-align: center;
      animation: blink 0.6s infinite;
      margin-bottom: 16px; font-weight: bold;
    }
    @keyframes blink { 50% { opacity: 0.3; } }

    /* Статус-сообщение под формой */
    .status {
      font-size: 13px; color: var(--success);
      margin-top: 8px; min-height: 18px;
    }

    /* Большой счётчик таймера */
    .timer-display {
      font-family: "Courier New", monospace;
      font-size: 32px; font-weight: bold;
      text-align: center; color: var(--warning);
      margin: 12px 0;
    }

    /* Лог событий — список */
    .log-list {
      max-height: 200px;
      overflow-y: auto;
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 8px;
      background: var(--bg);
      font-family: "Courier New", monospace;
      font-size: 12px;
    }
    .log-entry {
      padding: 4px 6px;
      border-bottom: 1px solid var(--border);
    }
    .log-entry:last-child { border-bottom: none; }
    .log-time { color: var(--muted); margin-right: 8px; }

    /* Маленькие "бэйджики" с инфой об устройстве */
    .info-row {
      display: flex; flex-wrap: wrap; gap: 8px;
      font-size: 12px; color: var(--muted);
      margin-top: 8px;
    }
    .info-row span {
      background: var(--bg); padding: 4px 10px; border-radius: 12px;
    }
  </style>
</head>
<body>
  <div class="container">

    <header>
      <h1>ESP32 — Аналоговые часы</h1>
      <!-- Кнопка переключения темы -->
      <button id="themeBtn" onclick="toggleTheme()" title="Сменить тему">🌙</button>
    </header>

    <!-- Уведомления о срабатывании будильника / таймера -->
    <div id="alarmNotice" class="alert">⏰ БУДИЛЬНИК! Время вышло!</div>
    <div id="timerNotice" class="alert" style="background: var(--warning);">⏱️ ТАЙМЕР! Время истекло!</div>

    <!-- ============ КАРТОЧКА: ЧАСЫ ============ -->
    <div class="card">
      <div class="clock-wrapper">
        <!-- SVG циферблат 200x200 (растягивается до 280px через CSS) -->
        <svg class="clock" viewBox="0 0 200 200">
          <!-- Внешний круг циферблата -->
          <circle cx="100" cy="100" r="95" fill="var(--face)"
                  stroke="var(--hand)" stroke-width="2"/>

          <!-- Часовые риски (12 штук). Генерируем через JS ниже -->
          <g id="ticks"></g>

          <!-- Цифры 12, 3, 6, 9 -->
          <text x="100" y="25"  text-anchor="middle" font-size="14"
                font-weight="bold" fill="var(--hand)">12</text>
          <text x="178" y="105" text-anchor="middle" font-size="14"
                font-weight="bold" fill="var(--hand)">3</text>
          <text x="100" y="185" text-anchor="middle" font-size="14"
                font-weight="bold" fill="var(--hand)">6</text>
          <text x="22"  y="105" text-anchor="middle" font-size="14"
                font-weight="bold" fill="var(--hand)">9</text>

          <!-- Стрелки. id'шники нужны JS для поворота через transform -->
          <!-- Часовая (короткая, толстая) -->
          <line id="hourHand"   class="hand"        x1="100" y1="100" x2="100" y2="55"
                stroke="var(--hand)"   stroke-width="6"/>
          <!-- Минутная (длиннее, тоньше) -->
          <line id="minuteHand" class="hand"        x1="100" y1="100" x2="100" y2="35"
                stroke="var(--hand)"   stroke-width="4"/>
          <!-- Секундная (длинная, тонкая, красная) -->
          <line id="secondHand" class="hand second" x1="100" y1="110" x2="100" y2="25"
                stroke="var(--second)" stroke-width="2"/>
          <!-- Центральная точка -->
          <circle cx="100" cy="100" r="5" fill="var(--second)"/>
        </svg>
      </div>

      <!-- Цифровое дублирование для удобства чтения -->
      <div class="digital" id="digitalTime">--:--:--</div>
      <div class="uptime"  id="uptime">Аптайм: -- мс</div>

      <!-- Бэйджики с инфой -->
      <div class="info-row">
        <span id="badgeAlarm">🔕 Будильник: выкл</span>
        <span id="badgeStatus">📡 Связь: проверка...</span>
      </div>
    </div>

    <!-- ============ КАРТОЧКА: УСТАНОВКА ВРЕМЕНИ ============ -->
    <div class="card">
      <h2>⏲️ Установить время</h2>
      <div class="grid-2">
        <input type="time" id="newTime" step="1" value="12:00:00">
        <button class="btn" onclick="setTime()">Установить</button>
      </div>
      <div class="status" id="setStatus"></div>
    </div>

    <!-- ============ КАРТОЧКА: БУДИЛЬНИК ============ -->
    <div class="card">
      <h2>⏰ Будильник</h2>
      <div class="grid-2">
        <input type="time" id="alarmTime" value="07:00">
        <div>
          <button class="btn success" onclick="setAlarm()">Включить</button>
          <button class="btn danger"  onclick="clearAlarm()">Выключить</button>
        </div>
      </div>
      <div class="status" id="alarmStatus"></div>
    </div>

    <!-- ============ КАРТОЧКА: ТАЙМЕР ============ -->
    <div class="card">
      <h2>⏱️ Таймер обратного отсчёта</h2>
      <!-- Большой счётчик секунд (или MM:SS, форматируется JS) -->
      <div class="timer-display" id="timerDisplay">--:--</div>
      <div class="grid-2">
        <input type="number" id="timerSec" min="1" max="3600" value="10" placeholder="секунд">
        <div>
          <button class="btn warning" onclick="startTimer()">Запустить</button>
          <button class="btn danger"  onclick="stopTimer()">Стоп</button>
        </div>
      </div>
      <div class="status" id="timerStatus"></div>
    </div>

    <!-- ============ КАРТОЧКА: СБРОС ============ -->
    <div class="card">
      <h2>🔄 Сброс</h2>
      <button class="btn danger" onclick="resetTime()">Сбросить время на 00:00:00</button>
      <div class="status" id="resetStatus"></div>
    </div>

    <!-- ============ КАРТОЧКА: ЛОГ СОБЫТИЙ ============ -->
    <div class="card">
      <h2>📜 Журнал последних событий</h2>
      <div class="log-list" id="logList">
        <div class="log-entry">Загрузка...</div>
      </div>
      <button class="btn" onclick="loadLog()" style="font-size: 12px; padding: 6px 12px;">
        Обновить
      </button>
    </div>

  </div>

<script>
// ===============================================================
//                          JAVASCRIPT
// ===============================================================

// --- Переключение темы (тёмная/светлая) ---
// Сохраняем выбор в localStorage, чтобы при перезагрузке тема сохранялась.
function toggleTheme() {
  document.body.classList.toggle('dark');
  const isDark = document.body.classList.contains('dark');
  localStorage.setItem('theme', isDark ? 'dark' : 'light');
  document.getElementById('themeBtn').textContent = isDark ? '☀️' : '🌙';
}
// При загрузке страницы — применяем сохранённую тему
if (localStorage.getItem('theme') === 'dark') {
  document.body.classList.add('dark');
  document.getElementById('themeBtn').textContent = '☀️';
}

// --- Генерация 12 рисок на циферблате через SVG ---
// Делаем это в JS, чтобы не писать руками 12 одинаковых тегов.
const ticks = document.getElementById('ticks');
for (let i = 0; i < 12; i++) {
  const angle = i * 30; // каждая риска через 30 градусов
  ticks.innerHTML += `<line x1="100" y1="10" x2="100" y2="18"
    stroke="var(--hand)" stroke-width="2"
    transform="rotate(${angle} 100 100)"/>`;
}

// --- Форматирование секунд в "MM:SS" (для таймера) ---
function fmtMMSS(secs) {
  if (secs < 0) return '--:--';
  const m = Math.floor(secs / 60);
  const s = secs % 60;
  return String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
}

// --- Глобальный флаг связи (для бэйджика статуса) ---
let isOnline = false;

// --- Запрос времени с ESP32 каждую секунду ---
// fetch к /api/time возвращает JSON {h, m, s, ms, alarm, timer, alarmH, alarmM, timerFired}
async function updateClock() {
  try {
    const r = await fetch('/api/time');
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const t = await r.json();

    // Цифровое время — с ведущими нулями (padStart)
    const hh = String(t.h).padStart(2, '0');
    const mm = String(t.m).padStart(2, '0');
    const ss = String(t.s).padStart(2, '0');
    document.getElementById('digitalTime').textContent = `${hh}:${mm}:${ss}`;
    document.getElementById('uptime').textContent =
      `Аптайм: ${t.ms.toLocaleString('ru')} мс (≈ ${(t.ms/1000).toFixed(1)} сек)`;

    // === Поворот стрелок ===
    // Секундная: 360°/60 = 6° на секунду
    const secAngle = t.s * 6;
    // Минутная: 6° на минуту + плавно от секунд (6/60 = 0.1° на секунду)
    const minAngle = t.m * 6 + t.s * 0.1;
    // Часовая: 360°/12 = 30° на час + плавно от минут (30/60 = 0.5° на минуту)
    const hourAngle = (t.h % 12) * 30 + t.m * 0.5;

    document.getElementById('secondHand').style.transform = `rotate(${secAngle}deg)`;
    document.getElementById('minuteHand').style.transform = `rotate(${minAngle}deg)`;
    document.getElementById('hourHand').style.transform   = `rotate(${hourAngle}deg)`;

    // Уведомления
    document.getElementById('alarmNotice').style.display = t.alarm      ? 'block' : 'none';
    document.getElementById('timerNotice').style.display = t.timerFired ? 'block' : 'none';

    // Бэйдж "Будильник"
    const badgeA = document.getElementById('badgeAlarm');
    if (t.alarmH >= 0) {
      const ah = String(t.alarmH).padStart(2, '0');
      const am = String(t.alarmM).padStart(2, '0');
      badgeA.textContent = `🔔 Будильник: ${ah}:${am}`;
    } else {
      badgeA.textContent = '🔕 Будильник: выкл';
    }

    // Бэйдж "Связь"
    document.getElementById('badgeStatus').textContent = '✅ Связь: онлайн';
    isOnline = true;

    // Таймер: показываем оставшееся время или прочерки
    document.getElementById('timerDisplay').textContent =
      (t.timer >= 0) ? fmtMMSS(t.timer) : '--:--';

  } catch (e) {
    // Если связь с ESP32 потеряна — показываем это
    document.getElementById('digitalTime').textContent = '!! связь !!';
    document.getElementById('badgeStatus').textContent = '❌ Связь: оффлайн';
    isOnline = false;
  }
}

// --- Установка времени через форму ---
async function setTime() {
  const v = document.getElementById('newTime').value;
  const status = document.getElementById('setStatus');
  try {
    const r = await fetch('/set?time=' + encodeURIComponent(v));
    status.textContent  = r.ok ? '✓ Время установлено' : '✗ Ошибка';
    status.style.color  = r.ok ? 'var(--success)' : 'var(--danger)';
    updateClock(); // сразу обновим часы
    loadLog();     // и лог
  } catch (e) {
    status.textContent = '✗ Нет связи с ESP32';
    status.style.color = 'var(--danger)';
  }
}

// --- Сброс времени ---
async function resetTime() {
  if (!confirm('Точно сбросить время на 00:00:00?')) return;
  const status = document.getElementById('resetStatus');
  try {
    const r = await fetch('/reset');
    status.textContent = r.ok ? '✓ Сброшено' : '✗ Ошибка';
    status.style.color = r.ok ? 'var(--success)' : 'var(--danger)';
    updateClock();
    loadLog();
  } catch (e) {
    status.textContent = '✗ Нет связи';
    status.style.color = 'var(--danger)';
  }
}

// --- Установка будильника ---
async function setAlarm() {
  const v = document.getElementById('alarmTime').value;
  const status = document.getElementById('alarmStatus');
  try {
    const r = await fetch('/alarm?time=' + encodeURIComponent(v));
    status.textContent = r.ok ? '✓ Будильник на ' + v : '✗ Ошибка';
    status.style.color = r.ok ? 'var(--success)' : 'var(--danger)';
    updateClock();
    loadLog();
  } catch (e) {
    status.textContent = '✗ Нет связи';
    status.style.color = 'var(--danger)';
  }
}

// --- Выключение будильника ---
async function clearAlarm() {
  const status = document.getElementById('alarmStatus');
  try {
    await fetch('/alarm?time=off');
    status.textContent = '✓ Будильник выключен';
    status.style.color = 'var(--muted)';
    updateClock();
    loadLog();
  } catch (e) {
    status.textContent = '✗ Нет связи';
    status.style.color = 'var(--danger)';
  }
}

// --- Запуск таймера ---
async function startTimer() {
  const sec = document.getElementById('timerSec').value;
  const status = document.getElementById('timerStatus');
  if (!sec || sec < 1) {
    status.textContent = '✗ Введи число секунд (1-3600)';
    status.style.color = 'var(--danger)';
    return;
  }
  try {
    const r = await fetch('/timer?seconds=' + sec);
    status.textContent = r.ok ? `✓ Таймер запущен на ${sec} сек` : '✗ Ошибка';
    status.style.color = r.ok ? 'var(--success)' : 'var(--danger)';
    updateClock();
    loadLog();
  } catch (e) {
    status.textContent = '✗ Нет связи';
    status.style.color = 'var(--danger)';
  }
}

// --- Остановка таймера ---
async function stopTimer() {
  try {
    await fetch('/timer?seconds=0');
    document.getElementById('timerStatus').textContent = '✓ Остановлен';
    document.getElementById('timerStatus').style.color = 'var(--muted)';
    updateClock();
    loadLog();
  } catch (e) { /* игнорируем */ }
}

// --- Загрузка журнала событий ---
async function loadLog() {
  try {
    const r = await fetch('/api/log');
    const list = await r.json();
    const el = document.getElementById('logList');
    if (list.length === 0) {
      el.innerHTML = '<div class="log-entry">(пусто)</div>';
      return;
    }
    // Самые свежие — сверху
    el.innerHTML = list.reverse().map(e => {
      // Форматируем timestamp в HH:MM:SS из миллисекунд от старта
      const total = Math.floor(e.t / 1000);
      const h = String(Math.floor(total / 3600)).padStart(2, '0');
      const m = String(Math.floor((total / 60) % 60)).padStart(2, '0');
      const s = String(total % 60).padStart(2, '0');
      return `<div class="log-entry">
        <span class="log-time">${h}:${m}:${s}</span>${e.m}
      </div>`;
    }).join('');
  } catch (e) {
    document.getElementById('logList').innerHTML =
      '<div class="log-entry">Не удалось загрузить лог</div>';
  }
}

// === ЗАПУСК ===
// Сразу обновляем + далее каждую секунду.
updateClock();
setInterval(updateClock, 1000);

// Лог обновляем реже (раз в 5 сек) — он меняется не каждую секунду.
loadLog();
setInterval(loadLog, 5000);
</script>
</body>
</html>
)rawliteral";

#endif // WEB_PAGE_H
