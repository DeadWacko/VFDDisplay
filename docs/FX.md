
# VFD Display — Effects System (FX)

Документ описывает архитектуру и реализацию FX-движка (`display_fx.c`), который
полностью перенесён из легаси и расширен новыми эффектами.

## 1. Цели FX-слоя

FX-движок должен обеспечивать:
- плавные анимации яркости и сегментов
- полную изоляцию от LL-слоя
- работу по FSM через `display_process()`
- удобный API для запуска эффектов
- корректный возврат к CONTENT после завершения

FX не касается GPIO/PWM напрямую — только сегментов и яркости.

## 2. Общая архитектура FX

FX работает как state machine.

APP → display_fx_xxx()
    └→ HL ставит режим MODE_EFFECT
       └→ FX получает управление
          └→ display_fx_tick() вызывается на каждом heartbeat
             └→ по завершении FX → MODE_CONTENT → восстановление состояния

Эффект может управлять:
- яркостью LL (глобальной или per-digit)
- сегментами HL (`display_ll_get_buffer()`)

## 3. Структура FX-слоя

### 3.1. Состояние FX

Главная структура:

```c
typedef struct {
    bool      active;
    fx_type_t type;

    absolute_time_t start_time;
    uint32_t  duration_ms;
    uint32_t  frame_ms;

    uint8_t   base_brightness;

    // GLITCH
    bool      glitch_active;
    uint32_t  glitch_last_ms;
    uint32_t  glitch_next_ms;
    uint32_t  glitch_step;
    uint8_t   glitch_digit;
    uint8_t   glitch_bit;
    vfd_seg_t glitch_saved_digit;

    // MATRIX RAIN
    uint32_t  matrix_last_ms;
    uint32_t  matrix_step;
    uint32_t  matrix_total_steps;
    uint8_t   matrix_min_percent;
    uint8_t   matrix_brightness_percent[VFD_MAX_DIGITS];

    // MORPH
    vfd_seg_t morph_start[VFD_MAX_DIGITS];
    vfd_seg_t morph_target[VFD_MAX_DIGITS];
    uint32_t  morph_step;
    uint32_t  morph_steps;

    // DISSOLVE
    uint8_t   dissolve_order[VFD_MAX_DIGITS * 8];
    uint32_t  dissolve_total_bits;
    uint32_t  dissolve_step;
} fx_state_t;
```

Все эффекты работают внутри этой state-machine.

## 4. Перечень реализованных эффектов

**4.1. FADE IN / FADE OUT**  
Яркость меняется по гамма-кривой:  
`brightness = gamma(normalized_time)`  
HL-буфер не трогается.

**4.2. PULSE (дыхание)**  
- синусоидальная яркость: `-cos(phase) → [0..1]`  
- гамма-коррекция  
- минимальная яркость 8%  
- плавное «дыхание»

**4.3. WAVE (волна яркости)**  
- синус с фазовым сдвигом `phase - digit_index * shift`  
- пер-дигитная яркость  
- применяется в HL через `display_ll_set_brightness(i)`

**4.4. GLITCH (хаотический фликер)**  
Работает по паттерну 1-0-1-0-1-1-0 для одного случайного сегмента.  
Жёстко изолирован: после окончания бита восстанавливается исходная маска.

**4.5. MATRIX RAIN**  
- каждые `frame_ms` один случайный разряд поднимается до 100%  
- остальные «гаснут» до min-percent  
- эффект цифрового дождя

**4.6. MORPH (цифра → цифра)**  
- плавный переход сегментных масок  
- каждый сегмент включается/выключается по порядку  
- количество шагов задаётся пользователем

**4.7. DISSOLVE (рассыпание сегментов)**  
- каждому сегменту назначается случайная задержка  
- порядок через перемешивание массива 0..digits*8  
- выключает сегменты в случайном порядке до полной пустоты

## 5. Интерфейсы FX

```c
bool display_fx_fade_in(uint32_t duration_ms);
bool display_fx_fade_out(uint32_t duration_ms);
bool display_fx_pulse(uint32_t duration_ms);
bool display_fx_wave(uint32_t duration_ms);
bool display_fx_glitch(uint32_t duration_ms);
bool display_fx_matrix(uint32_t duration_ms, uint32_t frame_ms);
bool display_fx_morph(uint32_t duration_ms, const vfd_seg_t *target, uint32_t steps);
bool display_fx_dissolve(uint32_t duration_ms);

void display_fx_tick(void);
void display_fx_stop(void);
bool display_fx_is_running(void);
```

Все эффекты работают через heartbeat.

## 6. Статус миграции (ноябрь 2025)

| Эффект        | Статус                       |
|---------------|------------------------------|
| Fade In       | перенесён                    |
| Fade Out      | перенесён                    |
| Pulse         | перенесён + улучшена плавность |
| Wave          | перенесён                    |
| Glitch        | перенесён                    |
| Matrix Rain   | реализован                   |
| Morph         | полностью реализован         |
| Dissolve      | полностью реализован         |

FX-движок полностью завершён и интегрирован в `display_process()`.

## 7. Принципы разработки FX

- FX не трогает LL напрямую
- HL-буфер всегда корректен
- блокировок LL/HL нет
- один активный эффект одновременно
- эффекты обязаны завершаться
- плавность обеспечивается гаммой LL