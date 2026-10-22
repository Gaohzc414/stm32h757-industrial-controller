# Watchdog 设计审计

## 总体结构

当前看门狗由两层组成：

1. 软件任务健康监控：每个任务使用递增 heartbeat counter；
2. 硬件 IWDG1：软件判断不健康时停止刷新，等待硬件复位。

核心实现位于 `app/BSP/bsp_watchdog.c`。

## 软件监控

### Heartbeat counter

每个监控槽位包含 counter、上次观察到的 counter、最后进展时间和首次 kick 状态：

- `app/BSP/bsp_watchdog.c:18-26`

`BSP_Watchdog_Kick()` 递增 counter。监控任务通过 counter 是否发生变化判断任务是否继续运行：

- `app/BSP/bsp_watchdog.c:119-143`
- `app/BSP/bsp_watchdog.c:206-227`

这不是简单 boolean。计数器可以区分“任务曾经运行过”和“任务持续有进展”。

### 监控槽位与超时

当前槽位包括 LVGL、default、Modbus、ADC、CM4、W5500：

- `app/BSP/bsp_watchdog.h:10-18`
- `app/BSP/bsp_watchdog.c:28-44`

与本项目直接相关的阈值：

- CM7 default：`8000 ms`；
- ADC：`5000 ms`；
- CM4：`10000 ms`；
- LVGL：`5000 ms`。

来源：`app/BSP/bsp_watchdog.c:28-35`

### CM4 heartbeat

CM7 的 `SensorIpcTask` 启动时先预置 CM4 槽位一次：

- `app/CM7/Core/Src/freertos.c:180-183`

CM4 每次发布成功或失败的数据后，CM7 任务由 HSEM notification 唤醒并 kick `BSP_WDG_CM4`：

- `app/CM7/Core/Src/freertos.c:191-203`

如果 CM4 不再发布，CM4 槽位会在 `10000 ms` 后被判定为 stale。这里的“传感器超时”实际是跨核发布心跳超时，不是独立的传感器时间戳比较。

### Sensor valid 与 timeout 的区别

CM4 读取失败时仍发布数据，但置 `valid=0`：

- `app/CM4/BSP/Sensor/dht11.c:187-198`

CM7 将 `valid` 映射为在线/离线状态：

- `app/CM7/Core/Src/freertos.c:183-203`

因此当前有两种故障信号：

- DHT11 单次读取失败：`valid=0`，UI 可显示离线；
- CM4 长时间不发布：CM4 heartbeat stale，软件 watchdog 停止喂 IWDG。

## 硬件 IWDG

CM7 在慢速外设初始化完成后启动 IWDG：

- `app/CM7/Core/Src/main.c:169-177`

启动看门狗任务后，每 `2000 ms` 尝试刷新 IWDG：

- `app/BSP/bsp_watchdog.c:12-16,247-287`

源码日志给出的配置为：

- prescaler `128`；
- reload `2000`；
- window `4095`；
- 约 `8s @ 32kHz LSI`。

来源：`app/BSP/bsp_watchdog.c:303-307`

启动宽限期为 `45000 ms`：

- `app/BSP/bsp_watchdog.c:13-14,242-255`

当任务健康检查失败时，代码停止刷新 IWDG，并等待硬件复位：

- `app/BSP/bsp_watchdog.c:271-276`

复位原因从 `RCC->RSR` 读取，并区分 IWDG1、WWDG、POR、NRST、CM7/CM4 软件复位：

- `app/BSP/bsp_watchdog.c:76-98`

## Debug 案例一：OTA 期间 watchdog 误触发

### 现象：

OTA/Flash 擦写期间，LVGL、ADC 等任务被挂起，watchdog 若继续检查所有任务，会把预期挂起误判为死任务。

### 排查：

检查 OTA peer 挂起逻辑、watchdog 长操作状态和任务健康检查范围：

- OTA 挂起：`app/BSP/OTA/bsp_ota_manager.c:199-221`
- 长操作标记：`app/BSP/bsp_watchdog.c:100-117`
- 健康检查范围：`app/BSP/bsp_watchdog.c:194-205`

### 关键寄存器：

硬件复位最终由 IWDG1 完成，复位原因通过 `RCC->RSR` 的 `RCC_RSR_IWDG1RSTF` 判断：

- `app/BSP/bsp_watchdog.c:76-82`

### 根因：

早期 boolean 标记会被多个长操作调用相互覆盖：一个子系统退出长操作后可能清除仍被另一个子系统持有的豁免状态。

### 修复：

`s_long_op_active` 改为引用计数。进入时递增，退出时递减，只有计数归零才恢复完整任务监控：

- `app/BSP/bsp_watchdog.c:48-49,105-117`

长操作期间当前代码只监控 W5500 槽位：

- `app/BSP/bsp_watchdog.c:200-204`

### 验证：

该修复可由源码确认，且应用构建通过。仓库没有提供 OTA 期间实际 watchdog 日志，因此不宣称板级回归已完成。

## Debug 案例二：CM4 迁移导致监控失效

### 现象：

DHT11 从 CM7 迁移到 CM4 后，若 CM7 仍只监控本核任务，CM4 死机可能不会被发现。

### 排查：

检查 CM4 发布、HSEM 事件、CM7 IPC 任务和 watchdog 槽位：

- CM4 发布：`app/CM4/BSP/Sensor/dht11.c:187-199`
- HSEM 事件：`app/Common/Src/sensor_shared.c:65-68`
- CM7 notification：`app/Common/Src/sensor_shared.c:99-115`
- CM4 watchdog 槽位：`app/BSP/bsp_watchdog.c:28-35`

### 关键寄存器：

- HSEM2 事件通知进入 `HSEM2_IRQHandler()`：`app/CM7/Core/Src/stm32h7xx_it.c:334-339`
- 软件健康失败后由 IWDG1 复位：`app/BSP/bsp_watchdog.c:252-275`

### 根因：

传感器任务已运行在 CM4，原先仅在 CM7 侧 kick 的本地 heartbeat 不再代表传感器任务实际运行。

### 修复：

增加 `BSP_WDG_CM4` 槽位；CM7 每次收到 CM4 发布通知后 kick 该槽位，且启动时预置一次槽位，防止 CM4 从未启动时被永久忽略：

- `app/BSP/bsp_watchdog.h:10-18`
- `app/CM7/Core/Src/freertos.c:180-203`

### 验证：

跨核 heartbeat 路径已完成源码实现并随 CM7/CM4 构建通过。没有 CM4 停止发布的板级故障注入日志，因此硬件失核复位行为仍待验证。

## 设计限制

- watchdog 监控的是任务进展和跨核发布，不是对所有外设寄存器做健康检查；
- CM4 DHT11 任务本身没有直接调用 CM7 watchdog API；
- CM4 是否真正持续运行由 CM7 收到 HSEM 发布通知间接判断；
- OTA 长操作期间的监控范围与普通运行不同，诊断时必须结合 `s_long_op_active`。
