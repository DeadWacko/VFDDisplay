# VFD Display Driver — Public API

## 1. Низкоуровневый API (`display_ll.h`)

Этот слой предоставляет прямой доступ к буферу сегментов и настройке яркости.
Он не знает ни про «время», ни про «эффекты», ни про оверлеи — только железо.

Примеры функций (актуальная модель):

- `bool display_ll_init(const display_ll_config_t *cfg);`
  - Инициализация LL-слоя (пины DATA/CLOCK/LATCH, число разрядов, частота refresh).
- `bool display_ll_is_initialized(void);`
- `bool display_ll_start_refresh(void);`
- `void display_ll_stop_refresh(void);`

Работа с буфером:

- `uint8_t display_ll_get_digit_count(void);`
- `vfd_seg_t *display_ll_get_buffer(void);`
- `void display_ll_set_digit_raw(uint8_t index, vfd_seg_t segmask);`

Яркость и гамма:

- `void display_ll_set_brightness(uint8_t index, uint8_t level);`
- `void display_ll_set_brightness_all(uint8_t level);`
- `uint8_t display_ll_apply_gamma(uint8_t linear);`
- `void display_ll_enable_gamma(bool enable);`

> LL-слой сейчас считается стабильным: он обеспечивает мультиплексирование разрядов,
> программный PWM по яркости и базовую гамма-коррекцию. Вся более сложная логика
> (эффекты, время, оверлеи) живёт выше, в HL-слое.

---

## 2. Высокоуровневый API (`display_api.h`)

Этот слой опирается на `display_ll.h` и предназначен для приложений:
часы, индикаторы, проекты на Pico. Здесь появляются понятия «число», «время»,
«дата», «эффекты» и «оверлеи».

### 2.1. Базовый жизненный цикл

- `void display_init(uint8_t digit_count);`
  - Инициализирует LL внутри себя и поднимает HL-состояние.
- `void display_process(void);`
  - Периодический тик HL-логики (мигание точки, позже — эффекты/оверлеи).
  - Ожидается вызов из главного цикла приложения.

### 2.2. Отрисовка контента

Уровень «что показать на экране», без эффектов.

**Уже реализовано:**

- `[x]` `void display_show_number(int32_t value);`
  - Показать целое число, выровненное по правому краю.
- `[x]` `void display_show_text(const char *text);`
  - Показать строку (до 10 символов, в рамках ширины индикатора).
- `[x]` `void display_show_time(uint8_t hours, uint8_t minutes, bool show_colon);`
  - Показать время в формате `HHMM` или `HH:MM` (через DP-сегменты).
- `[x]` `void display_show_date(uint8_t day, uint8_t month);`
  - Показать дату в формате `DDMM` или `DD.MM`.
- `[x]` `vfd_seg_t *display_content_buffer(void);`
  - Доступ к текущему CONTENT-буферу для отладки/просмотра.

**Планируется (пока не реализовано):**

- `[ ]` Скроллинг текста.
- `[ ]` Числовые анимации (морфинг, перелистывание).

### 2.3. Яркость и режимы

**Уже реализовано:**

- `[x]` `void display_set_brightness(uint8_t value);`
  - Глобальная яркость индикатора (0–255), прокидывается в LL.

**Заглушки / в разработке:**

- `[ ]` `void display_set_night_mode(bool enable);`
  - Ночной режим (смягчённая яркость, приглушённые эффекты).
- `[ ]` `void display_set_auto_brightness(bool enable);`
  - Автоматическая яркость (фоторезистор/датчик).

### 2.4. Точка / двоеточие

**Уже реализовано:**

- `[x]` `void display_set_dot_blinking(bool enable);`
  - Управление миганием точки/двоеточия в режиме времени `HH:MM`.
  - На текущем железе реализовано через DP (бит 7) второго и третьего разрядов.
  - Логика мигания живёт в `display_process()` (примерно 1 Гц).

### 2.5. Режимы и статус

- `[x]` `display_mode_t display_get_mode(void);`
  - Текущий режим:
    - `DISPLAY_MODE_CONTENT`
    - `DISPLAY_MODE_EFFECT`
    - `DISPLAY_MODE_OVERLAY`
- `[ ]` `bool display_is_effect_running(void);`
- `[ ]` `bool display_is_overlay_running(void);`

Пока обе функции возвращают `false` (эффекты/оверлеи ещё не перенесены из легаси),
но API уже зарезервирован.

### 2.6. Эффекты (FX API)

Эти функции описаны в спецификации и частично реализованы в легаси,
но ещё не перенесены в новый HL-слой.

Планируемый набор:

- `[ ]` `bool display_fx_pulse(uint32_t duration_ms);`
- `[ ]` `bool display_fx_brightness_wave(uint32_t duration_ms);`
- `[ ]` `bool display_fx_fade_in(uint32_t duration_ms);`
- `[ ]` `bool display_fx_fade_out(uint32_t duration_ms);`
- `[ ]` `bool display_fx_dynamic_flicker(uint32_t duration_ms);`
- `[ ]` `bool display_fx_matrix(uint32_t duration_ms, uint32_t frame_ms);`
- `[ ]` `void display_fx_stop(void);`

Фактическая реализация будет жить в отдельном модуле (`display_fx.c`)
и использовать общий heartbeat (`display_process()`).

### 2.7. Оверлеи (Overlay API)

Специальные состояния, поверх обычного контента (Wi-Fi, NTP, boot и т.д.).

Планируемые функции:

- `[ ]` `bool display_overlay_boot(uint32_t duration_ms);`
- `[ ]` `bool display_overlay_wifi(uint32_t duration_ms);`
- `[ ]` `bool display_overlay_ntp(uint32_t duration_ms);`
- `[ ]` `void display_overlay_stop(void);`

Оверлеи будут отвечать за временную замену отображения
(подключение к Wi-Fi, синхронизация времени и т.п.), после чего
возвращать экран в нормальный режим CONTENT.

Полная спецификация эффектов и оверлеев описана в `FX.md` / `SPEC.md`
и будет заполняться по мере переноса логики из легаси (`display.c`)
в новые модули.

---

## 3. Legacy API (`display.h`)

Старый заголовок `display.h` используется текущими примерами и будет
постепенно переведён на новый API. На переходный период он может выступать
в роли thin-wrapper над `display_ll` / `display_api`.

В новых проектах рекомендуется использовать `display_ll.h` / `display_api.h`.
