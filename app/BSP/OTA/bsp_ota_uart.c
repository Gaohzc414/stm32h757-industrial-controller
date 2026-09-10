/**
 * @file    bsp_ota_uart.c
 * @brief   OTA USART6 DMA + 环形缓冲收发实现
 *
 * 接收路径：USART6 硬件 FIFO -> DMA -> s_dma_rx_buf（.dma_buffer 段）
 *          -> HAL_UARTEx_RxEventCallback（IDLE/满）-> 环形缓冲 -> 通知 OTA 任务。
 * 任务侧通过 try_read 从环形缓冲取字节、wait_rx 阻塞等通知，彻底取代原先
 * “逐字节 HAL_UART_Receive 轮询 + osDelay 睡眠”导致的丢字节问题。
 */
#include "bsp_ota_uart.h"

#include "bsp_ota_conf.h"
#include "bsp_log.h"
#include "stm32h7xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

extern UART_HandleTypeDef huart6;

/** 环形缓冲大小（2 的幂，便于掩码取模） */
#define OTA_UART_RING_SIZE   (2048U)
#define OTA_UART_RING_MASK   (OTA_UART_RING_SIZE - 1U)

/** SPSC 环形缓冲：DMA 中断为生产者，OTA 任务为消费者 */
static uint8_t s_ring[OTA_UART_RING_SIZE];
static volatile uint32_t s_ring_head;   /**< 生产者（ISR）写 */
static volatile uint32_t s_ring_tail;   /**< 消费者（任务）写 */

/** DMA 接收缓冲：D-Cache 已开启，须 32 字节对齐且大小为 32 的倍数 */
static uint8_t s_dma_rx_buf[BSP_OTA_RX_BUF_SIZE]
    __attribute__((section(".dma_buffer"), aligned(32)));

/** 被 RX 中断唤醒的任务句柄（osThreadId_t 强转） */
static TaskHandle_t s_notify_task;
/** 首字节已打印标记（一次性诊断日志，任务上下文安全） */
static volatile uint8_t s_rx_first_logged;

/**
 * @brief  生产者入队（仅在 DMA 中断上下文调用）
 * @param  src 源数据
 * @param  len 字节数
 */
static void ota_ring_push(const uint8_t *src, uint32_t len)
{
    uint32_t i;

    for (i = 0U; i < len; i++) {
        uint32_t head = s_ring_head;

        if ((head - s_ring_tail) >= OTA_UART_RING_SIZE) {
            /* 满：丢弃剩余字节，帧 CRC 校验失败会触发上位机重传 */
            return;
        }
        s_ring[head & OTA_UART_RING_MASK] = src[i];
        s_ring_head = head + 1U;
    }
}

/**
 * @brief  消费者出队（仅在任务上下文调用）
 * @param  dst 目标缓冲
 * @param  cap 期望读取字节数
 * @retval 实际读出字节数
 */
static uint32_t ota_ring_pop(uint8_t *dst, uint32_t cap)
{
    uint32_t n = 0U;
    uint32_t tail = s_ring_tail;

    while ((n < cap) && (s_ring_head != tail)) {
        dst[n] = s_ring[tail & OTA_UART_RING_MASK];
        n++;
        tail++;
    }
    s_ring_tail = tail;

    return n;
}

/**
 * @brief  初始化 OTA UART（依赖 CubeMX 初始化）
 */
void bsp_ota_uart_init(void)
{
    s_ring_head = 0U;
    s_ring_tail = 0U;
    LOG_I("OTA-UART", "USART6 DMA RX ready (ring=%u, dma=%u)",
          (unsigned int)OTA_UART_RING_SIZE, (unsigned int)sizeof(s_dma_rx_buf));
}

/**
 * @brief  绑定 RX 通知目标任务并武装 DMA 循环接收
 * @param  notify_task 被 RX 中断唤醒的任务句柄
 * @retval 0 成功，-1 失败
 */
int bsp_ota_uart_start_rx(void *notify_task)
{
    s_notify_task = (TaskHandle_t)notify_task;

    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart6, s_dma_rx_buf,
                                     (uint16_t)sizeof(s_dma_rx_buf)) != HAL_OK) {
        s_notify_task = NULL;
        LOG_E("OTA-UART", "arm DMA RX failed");
        return -1;
    }

    LOG_I("OTA-UART", "DMA RX armed");
    return 0;
}

/**
 * @brief  停止 DMA 接收
 */
void bsp_ota_uart_stop_rx(void)
{
    s_notify_task = NULL;
    (void)HAL_UART_AbortReceive(&huart6);
}

/**
 * @brief  非阻塞从环形缓冲读取数据
 * @param  buf 输出缓冲
 * @param  cap 期望读取字节数
 * @retval 实际读取字节数，0 表示当前无数据
 */
int bsp_ota_uart_try_read(uint8_t *buf, uint16_t cap)
{
    int n;

    if ((buf == NULL) || (cap == 0U)) {
        return 0;
    }

    n = (int)ota_ring_pop(buf, (uint32_t)cap);

    /* 一次性诊断：只要 USART6 收到过哪怕 1 字节，就打印一次 */
    if ((n > 0) && (s_rx_first_logged == 0U)) {
        s_rx_first_logged = 1U;
        LOG_I("OTA-UART", "first RX byte drained from ring");
    }

    return n;
}

/**
 * @brief  阻塞等待 RX 数据（有数据或超时后返回）
 * @param  timeout_ms 超时时间（毫秒）
 * @retval 0 等待结束
 */
int bsp_ota_uart_wait_rx(uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0U) ? 0U : pdMS_TO_TICKS(timeout_ms);

    (void)ulTaskNotifyTake(pdTRUE, ticks);

    return 0;
}

/**
 * @brief  发送 UART 数据
 * @param  buf 发送缓冲区
 * @param  len 字节数
 * @retval 0 成功，-1 失败
 */
int bsp_ota_uart_write(const uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return -1;
    }

    if (HAL_UART_Transmit(&huart6, (uint8_t *)buf, len, BSP_OTA_UART_TIMEOUT_MS) != HAL_OK) {
        return -1;
    }

    return 0;
}

/**
 * @brief  USART6 接收事件回调（IDLE 或 DMA 满）
 * @param  huart UART 句柄
 * @param  Size  本轮实际收到的字节数
 *
 * HAL 在 IDLE 中断或 DMA 传输完成时调用；不会自动重新武装接收，
 * 必须在此处再次调用 ReceiveToIdle_DMA，否则后续数据会被漏掉。
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if ((huart->Instance != USART6) || (Size == 0U)) {
        return;
    }

    /* DMA 直接写物理内存，CPU 侧 D-Cache 需先失效对应行，否则读到陈旧数据 */
    SCB_InvalidateDCache_by_Addr((uint32_t *)s_dma_rx_buf, (int32_t)sizeof(s_dma_rx_buf));

    ota_ring_push(s_dma_rx_buf, (uint32_t)Size);

    if (s_notify_task != NULL) {
        vTaskNotifyGiveFromISR(s_notify_task, &xHigherPriorityTaskWoken);
    }

    /* 立即重新武装 DMA，避免空窗期漏收后续数据 */
    if (HAL_UARTEx_ReceiveToIdle_DMA(huart, s_dma_rx_buf,
                                     (uint16_t)sizeof(s_dma_rx_buf)) != HAL_OK) {
        s_notify_task = NULL;
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
