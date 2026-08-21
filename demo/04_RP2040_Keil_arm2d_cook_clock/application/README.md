# 应用层外设任务

本目录沿用 demo1 的 `*_task.c/.h` 模式，将各硬件功能分别封装为独立任务。

- `buzzer_task.c/.h`：蜂鸣器任务与本工程的倒计时结束、生日快乐旋律。
- `power_task.c/.h`：电源保持按键状态机和通用 GPIO 按键去抖。
- `qmi8658c_task.c/.h`：QMI8658 I2C 初始化、轮询和六轴数据读取。

I2C 默认使用 `i2c0` 的 `GPIO0`（SDA）和 `GPIO1`（SCL）；电源按键默认使用
`GPIO2`（保持电源）和 `GPIO9`（按键检测）。这些定义均可在 `bsp_cfg.h` 覆盖。

在 `main()` 的 `platform_init()` 之后初始化，并在主循环中调用各任务：

```c
#include "application/buzzer_task.h"
#include "application/power_task.h"
#include "application/qmi8658c_task.h"

int main(void)
{
    platform_init();
    buzzer_task_init();
    power_task_init();
    (void)qmi8658c_init();

    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        buzzer_task(now_ms);
        power_task(now_ms);
        qmi8658c_task(now_ms);
    }
}
```

`qmi8658c_read(float data[6])` 返回加速度（`data[0..2]`，g）和角速度
（`data[3..5]`，dps）。蜂鸣器播放和普通按键轮询都是非阻塞的，必须持续调用对应 task。

## 提示音策略

使用 `cook_clock_set_countdown_finished_effect()` 可绑定倒计时结束时的显示与声音：

- `COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_DEFAULT`：上行确认音与 `00:00`。
- `COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_BIRTHDAY`：生日快乐旋律与 `happy birthday`。

播放过程非阻塞，由 `buzzer_task()` 推进。调用
`app_alert_set_handler()` 可替换应用自己的音符表、PCM 或视觉提示策略：

```c
static void app_custom_alert(app_alert_t alert, void *target)
{
    (void)target;

    if (APP_ALERT_COUNTDOWN_FINISHED == alert) {
        app_buzzer_beep(1000u, 120u);
    }
}

app_alert_set_handler(app_custom_alert, NULL);
```