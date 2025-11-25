# VFD Display Driver — Public API

## 1. Низкоуровневый API (`display_ll.h`)

Этот слой предоставляет прямой доступ к буферу сегментов и настройке яркости.

Примеры функций:

- `display_ll_init(uint8_t digit_count);`
- `display_ll_get_buffer(void);`
- `display_ll_set_digit_raw(idx, segmask);`
- `display_ll_set_brightness(idx, level);`
- `display_ll_start_refresh();`
- `display_ll_stop_refresh();`

Подробное описание будет дополняться по мере стабилизации реализации.

## 2. Высокоуровневый API (`display_api.h`)

Этот слой предназначен для приложений (часы, индикаторы и т.д.).

Примеры функций:

- `display_init(digit_count);`
- `display_show_time(h, m, show_colon);`
- `display_show_number(value);`
- `display_show_text("HELLO");`
- `display_fx_wave(duration_ms);`
- `display_overlay_ntp(duration_ms);`
- `display_set_brightness(level);`

Полная спецификация будет заполняться по мере рефакторинга `display.c` и появления
реализации нового API.

## 3. Legacy API (`display.h`)

Старый заголовок `display.h` используется текущими примерами и будет
постепенно переведён на новый API. На переходный период он может выступать
в роли thin-wrapper над `display_ll`/`display_api`.

В новых проектах рекомендуется использовать `display_ll.h` / `display_api.h`.
