# High-Level Layer — API Reference

**Версия:** `v1.3.7`
**Статус:** `stable`

---

## 1. Инициализация

### `void display_init_ex(const display_ll_config_t *cfg)`
Основной метод инициализации с настройкой GPIO.

### `void display_process(void)`
**Heartbeat.** Обязателен к вызову в `while(1)`. Обрабатывает анимации, оверлеи и автояркость.

---

## 2. Вывод Контента

### `void display_show_time(uint8_t hh, uint8_t mm, bool ignored)`
Выводит время `HH MM`. Третий аргумент игнорируется (v1.2.0+). Разделитель настраивается отдельно.

### `void display_show_text(const char *text)`
Выводит строку. Точка в строке (`"12.3"`) автоматически "приклеивается" к предыдущему символу.

---

## 3. Системные настройки

### Разделители (Dots)
Управление точками отделено от контента.

```c
// Включить точку на 2-м разряде (индекс 1), режим: мигание
display_set_dots_config((1 << 1), true);
```

### Оверлеи (Overlays) — Issue #9
Специальные режимы для отображения системных состояний. Они перекрывают любой контент и эффекты.

#### `bool display_overlay_boot(uint32_t duration_ms)`
**Анимация:** Последовательный перебор цифр 0..9 на всех разрядах.
**Назначение:** Self-test при включении питания.

#### `bool display_overlay_wifi(uint32_t duration_ms)`
**Анимация:** Синхронное мигание "8888".
**Назначение:** Индикация процесса подключения к сети.

#### `bool display_overlay_ntp(uint32_t duration_ms)`
**Анимация:** "Змейка" (бегающий сегмент по периметру).
**Назначение:** Ожидание синхронизации времени.

*Параметр `duration_ms` определяет общую длительность анимации.*

---

## 4. Управление Эффектами

Полный список эффектов см. в [Effects_System.md](./Effects_System.md).

```c
bool display_fx_wave(uint32_t duration_ms);
bool display_fx_glitch(uint32_t duration_ms);
bool display_fx_marquee(const char *text, uint32_t speed_ms);
void display_fx_stop(void);
```