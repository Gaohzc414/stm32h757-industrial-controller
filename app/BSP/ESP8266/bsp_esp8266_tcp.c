/**
 * @file bsp_esp8266_tcp.c
 * @brief ESP8266 AT TCP 客户端：连接、主动 +IPD 接收与 CIPSEND 发送
 *
 * 本模块负责 ESP8266 模块 TCP 链路的建立/关闭，以及 OTA 期间 SOIP 字节流的收发。
 * 接收路径采用「ISR 入队 → 任务 poll 解析 → pending 缓冲 → try_recv_byte」三级结构，
 * 避免在 UART 中断里做字符串解析；发送路径通过 CIPSEND 三阶段握手（'>' → payload → SEND OK）。
 *
 * 数据流概览：
 *   UART RX ISR ──push──> s_isr_ring ──pop──> tcp_ipd_feed_bytes()
 *                                              │
 *                                              ├─ 解析 +IPD,128: 头
 *                                              └─ 二进制 payload ──> s_pending
 *                                                                    │
 *   OTA/SOIP 层 <──try_recv_byte()───────────────────────────────────┘
 */
#include "bsp_esp8266_tcp.h"
#include "bsp_esp8266_at.h"
#include "bsp_esp8266_app.h"
#include "bsp_esp8266_conf.h"
#include "bsp_log.h"
#include "bsp_watchdog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmsis_os.h"

#define ESP8266_TCP_TAG            "ESP-TCP"
#define ESP8266_CIPSTATUS_TIMEOUT_MS   5000U
#define ESP8266_CIPRECVMODE_TIMEOUT_MS 1500U
#define ESP8266_STATUS_TCP_UP      3
#define ESP8266_TCP_RX_CHUNK       4096U
#define ESP8266_TCP_ISR_RING_SIZE  2048U
/** 空闲中断可能连续到达，计数信号量避免丢失唤醒 */
#define ESP8266_TCP_RX_SEM_MAX      32U

/** @name TCP 接收与 +IPD 解析状态机私有变量
 *  数据流：UART ISR → s_isr_ring → poll_rx → +IPD 解析 → s_pending → try_recv_byte
 *  @{ */
/** TCP 接收就绪计数信号量：+IPD 收完或 ISR 入队后 Release，wait_rx 侧 Acquire 阻塞 */
static osSemaphoreId_t s_tcp_rx_sem;
/** 已剥离 +IPD 头的纯 TCP payload 缓冲，供 SOIP/OTA 层逐字节消费 */
static uint8_t s_pending[ESP8266_TCP_RX_CHUNK];
/** ISR→任务中转环：中断上下文仅 push，避免在 ISR 内做 +IPD 字符串解析 */
static uint8_t s_isr_ring[ESP8266_TCP_ISR_RING_SIZE];
static volatile uint16_t s_isr_head;   /**< 环写指针（ISR 更新） */
static volatile uint16_t s_isr_tail;   /**< 环读指针（任务 poll 更新，关中断访问） */
static uint16_t s_pending_len;         /**< pending 中有效 payload 总字节数 */
static uint16_t s_pending_pos;         /**< pending 已消费读指针 */
static uint8_t s_passive_ready;        /**< 被动模式（AT+CIPRECV）就绪标志，OTA 当前未用 */
static uint8_t s_active_mode;          /**< 主动 +IPD 模式已启用，为 0 时 feed_uart 直接丢弃 */

/** +IPD 行累积缓冲：逐字节拼 AT 推送行，遇 ':' 解析 payload 长度 */
static uint8_t s_ipd_line[96];
static uint16_t s_ipd_line_len;
/** +IPD payload 剩余待收字节；>0 时后续 UART 字节不再当 AT 文本，直接入 pending */
static uint32_t s_ipd_payload_remain;

/** CIPSEND 三阶段握手：want=1 等 '>'，want=2 等 SEND OK；got 记录到达阶段，0xFF=FAIL */
static volatile uint8_t s_cipsend_want;
static volatile uint8_t s_cipsend_got;
/** @} */

#if (BSP_ESP8266_TCP_RX_DEBUG != 0U)
/** 调试：当前 +IPD 包 payload 快照，收满一包后 hex/txt 打印 */
static uint8_t s_dbg_pkt[BSP_ESP8266_TCP_RX_DUMP_MAX];
static uint16_t s_dbg_pkt_len;
static uint32_t s_dbg_uart_bytes;   /**< 累计 UART 原始字节计数 */
static uint32_t s_dbg_ipd_packets; /**< 累计完整 +IPD 包数 */

/**
 * @brief  打印十六进制数据行（调试用）
 *
 * 将接收到的 TCP payload 前 24 字节以十六进制输出到日志，
 * 便于对照 SOIP 帧头或乱码问题，不影响正常收发逻辑。
 *
 * @param  data  待打印数据指针
 * @param  len   数据字节数
 * @retval 无
 */
static void tcp_log_hex_line(const uint8_t *data, uint16_t len)
{
  char line[80];
  uint16_t n = len;

  /* 日志行长度有限，最多打印前 24 字节，避免刷屏 */
  if (n > 24U) {
    n = 24U;
  }

  line[0] = '\0';
  for (uint16_t i = 0U; i < n; i++) {
    /* 逐字节追加 "%02X "，便于对照 SOIP 帧头 0xAA 0x55 等固定模式 */
    (void)snprintf(&line[strlen(line)], sizeof(line) - strlen(line), "%02X ",
                   (unsigned)data[i]);
  }
  LOG_I(ESP8266_TCP_TAG, "  hex: %s%s", line, (len > n) ? "..." : "");
}

/**
 * @brief  打印 TCP 接收 payload（调试用）
 *
 * 输出字节数、来源标签及可读 ASCII 预览；若含不可打印字符则仅打 hex。
 * 仅在 BSP_ESP8266_TCP_RX_DEBUG 开启时编译。
 *
 * @param  data  payload 数据指针
 * @param  len   payload 字节数
 * @param  via   来源描述（如 "ipd"）
 * @retval 无
 */
static void tcp_log_rx_payload(const uint8_t *data, uint16_t len, const char *via)
{
  uint16_t i;
  uint8_t printable = 1U;
  char txt[65];

  if ((data == NULL) || (len == 0U)) {
    return;
  }

  /* 先打概要：本包大小、来源标签、当前 pending 队列总长度 */
  LOG_I(ESP8266_TCP_TAG, "TCP RX %u bytes (%s) pending=%u",
        (unsigned)len, via, (unsigned)s_pending_len);
  tcp_log_hex_line(data, len);

  /* 短包且全为可打印 ASCII 时额外输出 txt 预览，便于快速识别 AT 文本误混入 */
  if (len >= sizeof(txt)) {
    printable = 0U;
  } else {
    for (i = 0U; i < len; i++) {
      uint8_t ch = data[i];
      /* 控制字符（除 \r \n \t）视为二进制，不再尝试 txt 预览 */
      if ((ch < 0x20U) && (ch != '\r') && (ch != '\n') && (ch != '\t')) {
        printable = 0U;
        break;
      }
      txt[i] = (char)((ch >= 0x20U && ch <= 0x7EU) ? ch : '.');
    }
    txt[len] = '\0';
  }

  if (printable != 0U) {
    LOG_I(ESP8266_TCP_TAG, "  txt: \"%s\"", txt);
  }
}
#endif /* BSP_ESP8266_TCP_RX_DEBUG */

/**
 * @brief  排空 TCP 接收信号量计数
 *
 * 连接建立/关闭前调用，清除历史 Release 累积的计数，
 * 避免旧唤醒导致 wait_rx 误判“已有数据”。
 *
 * @retval 无
 * @note  使用 timeout=0 非阻塞 Acquire，直到计数归零
 */
static void tcp_drain_rx_sem(void)
{
  if (s_tcp_rx_sem == NULL) {
    return;
  }

  /* 非阻塞取尽信号量计数，重置同步状态 */
  while (osSemaphoreAcquire(s_tcp_rx_sem, 0U) == osOK) {
  }
}

/**
 * @brief  释放 TCP 接收信号量唤醒等待任务
 *
 * +IPD payload 收满或 ISR 环有新数据时调用，通知 OTA 任务
 * 从 wait_rx 阻塞返回并继续 poll 解析。
 *
 * @retval 无
 * @note  计数信号量上限 ESP8266_TCP_RX_SEM_MAX，连续 Release 不会丢唤醒
 */
static void tcp_signal_rx(void)
{
  if (s_tcp_rx_sem != NULL) {
    (void)osSemaphoreRelease(s_tcp_rx_sem);
  }
}

/**
 * @brief  重置 ISR 环形缓冲区
 *
 * TCP 重连/关闭时清空 ISR 与任务间中转环，防止旧 +IPD 字节
 * 在链路断开后仍被 poll 解析并污染 SOIP 状态机。
 *
 * @retval 无
 * @note  关中断访问 head/tail，与 ISR push 互斥
 */
static void tcp_reset_isr_ring(void)
{
  uint32_t primask = __get_PRIMASK();

  /* 保存当前中断屏蔽状态，避免在已关中断的上下文里重复操作 PRIMASK */
  __disable_irq();
  s_isr_head = 0U;
  s_isr_tail = 0U;
  __set_PRIMASK(primask);  /* 恢复进入函数前的中断使能状态 */
}

/**
 * @brief  向 ISR 环形缓冲压入数据（中断上下文安全）
 *
 * UART RX 中断或 feed_uart 路径调用，仅做 O(1) 入队，不做 +IPD 解析。
 * 环满时丢弃后续字节并可选打 WARN，避免 ISR 长时间阻塞。
 *
 * @param  data  待入队数据指针
 * @param  len   字节数
 * @retval 无
 * @note  可在 ISR 上下文调用；head 为 volatile，tail 由任务侧更新
 */
static void tcp_isr_ring_push(const uint8_t *data, uint16_t len)
{
  uint16_t i;

  if ((data == NULL) || (len == 0U)) {
    return;
  }

  for (i = 0U; i < len; i++) {
    /* 每次循环重新读 head：ISR 可能在本循环期间被更高优先级中断抢占并推进 head */
    uint16_t head = s_isr_head;
    uint16_t next = (uint16_t)((head + 1U) % ESP8266_TCP_ISR_RING_SIZE);

    /* 环满：next==tail 表示无空闲槽，停止入队防止覆盖未读数据 */
    if (next == s_isr_tail) {
#if (BSP_ESP8266_TCP_RX_DEBUG != 0U)
      LOG_W(ESP8266_TCP_TAG, "ISR ring overflow, drop byte");
#endif
      break;
    }

    s_isr_ring[head] = data[i];
    s_isr_head = next;  /* 仅更新 head；tail 由任务侧 pop 时推进 */
  }
}

/**
 * @brief  从 ISR 环形缓冲批量弹出数据
 *
 * poll_rx 在任务上下文调用，一次最多取 out_cap 字节交给 +IPD 解析。
 * 关中断期间读写 tail，与 ISR push 互斥，保证一致性。
 *
 * @param  out      输出缓冲区
 * @param  out_cap  输出缓冲区容量（字节）
 * @retval 实际弹出的字节数，0 表示环空
 */
static uint16_t tcp_isr_ring_pop_batch(uint8_t *out, uint16_t out_cap)
{
  uint16_t count = 0U;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();

  /* 批量 drain：减少关中断次数，提高 poll 吞吐；tail 仅在关中断期间修改 */
  while ((s_isr_tail != s_isr_head) && (count < out_cap)) {
    out[count++] = s_isr_ring[s_isr_tail];
    s_isr_tail = (uint16_t)((s_isr_tail + 1U) % ESP8266_TCP_ISR_RING_SIZE);
  }

  __set_PRIMASK(primask);  /* 恢复中断，允许 ISR 继续 push */
  return count;
}

/**
 * @brief  重置 pending payload 缓冲
 *
 * 清空已解析出的纯 TCP 字节队列。连接关闭、重连或 +IPD 包消费完毕时调用，
 * 防止旧 SOIP 帧残留被 OTA 协议层误读。
 *
 * @retval 无
 */
static void tcp_reset_pending(void)
{
  s_pending_len = 0U;  /* 有效 payload 总字节数归零 */
  s_pending_pos = 0U;  /* 消费读指针回到缓冲起始 */
}

/**
 * @brief  重置 +IPD 行解析缓冲
 *
 * 丢弃当前正在累积的 AT 推送行（如 "+IPD,0,128:" 的前缀部分）。
 * 解析完一行头或检测到噪声时调用。
 *
 * @retval 无
 */
static void tcp_reset_ipd_line(void)
{
  s_ipd_line_len = 0U;
  s_ipd_line[0] = '\0';  /* 保持 C 字符串合法，便于 strstr 搜索 */
}

/**
 * @brief  重置 +IPD 解析状态机
 *
 * 同时清空行缓冲与 payload 剩余计数，使解析器回到「等待 +IPD 头」初始态。
 * TCP 重连/关闭、噪声丢弃、CLOSED 通知时调用。
 *
 * @retval 无
 */
static void tcp_reset_ipd_parser(void)
{
  tcp_reset_ipd_line();
  s_ipd_payload_remain = 0U;  /* 0 表示当前不在二进制 payload 接收态 */
}

/**
 * @brief  扫描 +IPD 行缓冲中的 CIPSEND 握手应答
 *
 * CIPSEND 期间模块返回的 '>'、SEND OK、FAIL 等 AT 文本也会经过
 * s_ipd_line 累积路径，在此与 +IPD 头解析并行检测，驱动 s_cipsend_got 状态。
 *
 * @retval 无
 * @note  s_cipsend_want==0 时直接返回，不影响正常 +IPD 接收
 */
static void tcp_cipsend_scan_line(void)
{
  if (s_cipsend_want == 0U) {
    return;  /* 无 CIPSEND 事务进行中，无需扫描 */
  }

  /* 阶段 1：AT+CIPSEND=n 发出后，模块单独一行返回 '>' 表示可以发 payload */
  if (s_cipsend_want == 1U) {
    for (uint16_t i = 0U; i < s_ipd_line_len; i++) {
      if (s_ipd_line[i] == '>') {
        s_cipsend_got = 1U;
        s_cipsend_want = 0U;  /* 握手完成，停止扫描 */
        tcp_reset_ipd_line();
        return;
      }
    }
    /* 行内尚无 '>'，继续累积 UART 字节 */
  }

  /* 阶段 2：payload 发完后等 SEND OK；标准 ESP-AT 固件返回此字符串 */
  if ((s_cipsend_want == 2U) && (strstr((char *)s_ipd_line, "SEND OK") != NULL)) {
    s_cipsend_got = 2U;
    s_cipsend_want = 0U;
    tcp_reset_ipd_line();
    return;
  }

  /* 部分第三方固件用 "Recv xxx bytes" 代替 SEND OK，同样视为发送成功 */
  if ((s_cipsend_want == 2U) && (strstr((char *)s_ipd_line, "Recv") != NULL)) {
    s_cipsend_got = 2U;
    s_cipsend_want = 0U;
    tcp_reset_ipd_line();
    return;
  }

  /* FAIL 表示 CIPSEND 被模块拒绝（缓冲满、未连接等），got=0xFF 通知上层失败 */
  if ((s_cipsend_want != 0U) && (strstr((char *)s_ipd_line, "FAIL") != NULL)) {
    s_cipsend_got = 0xFFU;
    s_cipsend_want = 0U;
    tcp_reset_ipd_line();
  }
}

/**
 * @brief  检测 AT 响应中的 TCP 连接关闭通知
 *
 * ESP8266 在 TCP 断开时会主动推送 "CLOSED" 或 "link is not" 等文本，
 * 调用 bsp_esp8266_notify_tcp_closed() 通知应用层触发重连。
 *
 * @param  resp  AT 响应字符串（可为 bsp_esp8266_at_last_response() 返回值）
 * @retval 无
 */
static void tcp_note_closed_response(const char *resp)
{
  if ((resp != NULL) &&
      ((strstr(resp, "CLOSED") != NULL) || (strstr(resp, "link is not") != NULL))) {
    bsp_esp8266_notify_tcp_closed();  /* 由 esp8266_app 后台任务执行重连 */
  }
}

/**
 * @brief  尝试配置 TCP 主动接收模式（+IPD 推送）
 *
 * 发送 AT+CIPRECVMODE=0 使模块在收到 TCP 数据时主动推送 +IPD 头+payload，
 * 而非被动 AT+CIPRECV 拉取。部分旧固件不支持此命令，失败时沿用模块默认行为。
 *
 * @retval 无
 * @note  配置失败仅打 WARN，不阻断后续 CIPSTART
 */
static void tcp_try_configure_recv_mode(void)
{
  if (!bsp_esp8266_at_send_and_wait("AT+CIPRECVMODE=0", "OK", ESP8266_CIPRECVMODE_TIMEOUT_MS)) {
    LOG_W(ESP8266_TCP_TAG, "CIPRECVMODE skipped, use module default (+IPD)");
    return;
  }
  LOG_I(ESP8266_TCP_TAG, "TCP recv mode: active (+IPD)");
}

/**
 * @brief  启用主动 +IPD 接收模式
 *
 * 置位 s_active_mode 后 feed_uart 才开始入队；同时重置 +IPD 解析器，
 * 确保新连接从干净状态开始收包。
 *
 * @retval 无
 */
static void tcp_enable_active_rx(void)
{
  s_passive_ready = 0U;   /* OTA 当前不使用被动 CIPRECV 模式 */
  s_active_mode = 1U;     /* 允许 ISR feed_uart 入队 */
  tcp_reset_ipd_parser(); /* 清除可能残留的行缓冲与 payload 计数 */
}

/**
 * @brief  查询 TCP 链路是否已建立
 *
 * 通过 AT+CIPSTATUS 获取 STATUS 码，与 BSP_ESP8266_STATUS_TCP_CONNECTED 比较。
 * connect 前用于跳过重复连接；CIPSTART 超时后用于二次确认链路是否实际已通。
 *
 * @retval 1 已连接；0 未连接或 AT 查询失败
 */
static uint8_t tcp_link_is_up(void)
{
  int status = bsp_esp8266_tcp_query_link_status();

  return (status == BSP_ESP8266_STATUS_TCP_CONNECTED) ? 1U : 0U;
}

/**
 * @brief  向 pending 缓冲追加 1 字节 +IPD payload
 *
 * +IPD 头解析完成后，后续 UART 字节为纯二进制 TCP 数据，
 * 逐字节追加到 s_pending，供 try_recv_byte 供 SOIP 层消费。
 *
 * @param  ch  payload 字节
 * @retval 无
 * @note  pending 满时丢弃并限频打 ERROR，避免 OTA 大包撑爆缓冲
 */
static void tcp_ipd_push_payload_byte(uint8_t ch)
{
  if (s_pending_len >= ESP8266_TCP_RX_CHUNK) {
    static uint32_t s_drop_log_tick;

    /* 限频日志：持续丢字节时最多 2 秒打一次，避免日志本身阻塞系统 */
    if ((HAL_GetTick() - s_drop_log_tick) > 2000U) {
      s_drop_log_tick = HAL_GetTick();
      LOG_E(ESP8266_TCP_TAG, "+IPD pending full (%u), dropping byte",
            (unsigned)ESP8266_TCP_RX_CHUNK);
    }
    return;  /* 缓冲已满，丢弃本字节 */
  }

  s_pending[s_pending_len++] = ch;
}

/**
 * @brief  尝试从 +IPD 行缓冲解析 payload 长度头
 *
 * 当 s_ipd_line 累积到完整一行（含 ':'）时，从中提取 payload 长度，
 * 并切换 s_ipd_payload_remain 进入二进制接收态。
 *
 * 典型行格式：
 *   +IPD,128:          （单连接 CIPMUX=0）
 *   +IPD,0,128:        （多连接，link_id 在前）
 *
 * @retval 无
 */
static void tcp_ipd_try_parse_header(void)
{
  char *tag;
  char *colon;
  char *num;
  int payload_len;

  /* 模块主动推送连接关闭，优先处理并通知应用层重连 */
  if (strstr((char *)s_ipd_line, "CLOSED") != NULL) {
    tcp_note_closed_response((const char *)s_ipd_line);
    tcp_reset_ipd_parser();
    return;
  }

  /* 尚未识别到 +IPD 前缀，继续累积 */
  tag = strstr((char *)s_ipd_line, "+IPD,");
  if (tag == NULL) {
    return;
  }

  /* 冒号未出现说明行未收全（如只收到 "+IPD,0,12"），等待后续字节 */
  colon = strchr(tag, ':');
  if (colon == NULL) {
    return;
  }

  /* 长度数字位于最后一个逗号与冒号之间：",128:" → strtol("128", ...) */
  num = strrchr(tag, ',');
  if (num == NULL) {
    return;
  }

  payload_len = (int)strtol(num + 1, NULL, 10);
  if (payload_len <= 0) {
    return;  /* 非法长度，忽略本行 */
  }

  /* 进入 payload 接收态：后续字节不再当 AT 文本解析，直接入 pending */
  s_ipd_payload_remain = (uint32_t)payload_len;
#if (BSP_ESP8266_TCP_RX_DEBUG != 0U)
  s_dbg_pkt_len = 0U;
  LOG_I(ESP8266_TCP_TAG, "+IPD payload %d bytes incoming", payload_len);
#endif
  tcp_reset_ipd_line();  /* 头已解析完毕，清空行缓冲准备下一包 */
}

/**
 * @brief  喂入 UART 字节流并驱动 +IPD/CIPSEND 解析状态机
 *
 * 每个 UART 字节按优先级依次判断：
 *   1. CIPSEND '>' 提示符（单字节，最高优先级）
 *   2. +IPD 二进制 payload 接收态（s_ipd_payload_remain > 0）
 *   3. AT 文本行累积（+IPD 头、SEND OK、CLOSED 等）
 *
 * @param  data  待解析字节流
 * @param  len   字节数
 * @retval 无
 */
static void tcp_ipd_feed_bytes(const uint8_t *data, uint16_t len)
{
  uint16_t i;

  for (i = 0U; i < len; i++) {
    uint8_t ch = data[i];

    /* ── 分支 1：CIPSEND 阶段 1，'>' 可能单独到达，不经过行缓冲 ── */
    if ((s_cipsend_want == 1U) && (ch == '>')) {
      s_cipsend_got = 1U;
      s_cipsend_want = 0U;
      continue;  /* 本字节已消费，处理下一字节 */
    }

    /* ── 分支 2：+IPD payload 二进制接收态 ── */
    if (s_ipd_payload_remain > 0U) {
      tcp_ipd_push_payload_byte(ch);
#if (BSP_ESP8266_TCP_RX_DEBUG != 0U)
      if (s_dbg_pkt_len < BSP_ESP8266_TCP_RX_DUMP_MAX) {
        s_dbg_pkt[s_dbg_pkt_len++] = ch;
      }
#endif
      s_ipd_payload_remain--;
#if (BSP_ESP8266_TCP_RX_DEBUG != 0U)
      if (s_ipd_payload_remain == 0U) {
        s_dbg_ipd_packets++;
        tcp_log_rx_payload(s_dbg_pkt, s_dbg_pkt_len, "ipd");
        s_dbg_pkt_len = 0U;
      }
#endif
      if (s_ipd_payload_remain == 0U) {
        /* 一整包 +IPD payload 收满，唤醒 wait_rx 阻塞的 OTA 任务 */
#if (BSP_ESP8266_TCP_RX_DEBUG != 0U)
        LOG_I(ESP8266_TCP_TAG, "+IPD done pending=%u", (unsigned)s_pending_len);
#endif
        tcp_signal_rx();
      }
      continue;
    }

    /* ── 分支 3：AT 文本行累积（+IPD 头 / CIPSEND 应答 / CLOSED 等） ── */
    if (s_ipd_line_len < (sizeof(s_ipd_line) - 1U)) {
      /* 常规追加：缓冲未满，直接写入并维护 NUL 结尾 */
      s_ipd_line[s_ipd_line_len++] = ch;
      s_ipd_line[s_ipd_line_len] = '\0';
    } else {
      /* 滑动窗口：行过长时丢弃最旧 1 字节，保留最近 94 字节继续匹配 +IPD 头 */
      memmove(s_ipd_line, &s_ipd_line[1], (size_t)(sizeof(s_ipd_line) - 2U));
      s_ipd_line[sizeof(s_ipd_line) - 2U] = ch;
      s_ipd_line[sizeof(s_ipd_line) - 1U] = '\0';
      s_ipd_line_len = (uint16_t)(sizeof(s_ipd_line) - 2U);
    }

    /* 每收到一字节都尝试解析 +IPD 头与 CIPSEND 应答（行未收全时内部会 return） */
    tcp_ipd_try_parse_header();
    tcp_cipsend_scan_line();

    /* 行已超 48 字节且仍无 +IPD 前缀，视为 UART 噪声或无关 AT 文本，重置解析器 */
    if ((s_ipd_line_len > 48U) && (strstr((char *)s_ipd_line, "+IPD,") == NULL)) {
      tcp_reset_ipd_parser();
    }
  }
}

/**
 * @brief  初始化 TCP 接收通知信号量
 * @retval 无
 */
void bsp_esp8266_tcp_rx_notify_init(void)
{
  osKernelState_t state;

  if (s_tcp_rx_sem != NULL) {
    return;  /* 已初始化，避免重复创建 */
  }

  /* CMSIS-RTOS v2 要求 osSemaphoreNew 在 osKernelInitialize() 之后调用 */
  state = osKernelGetState();
  if (state == osKernelInactive) {
    return;  /* 内核尚未启动，由上层在合适时机再次调用 */
  }

  /* 计数信号量：max=32 允许连续 Release 不丢唤醒；初始 count=0 表示尚无数据 */
  s_tcp_rx_sem = osSemaphoreNew(ESP8266_TCP_RX_SEM_MAX, 0U, NULL);
}

/**
 * @brief  从中断/AT 层喂入 UART 原始字节（ISR 入队）
 * @param  data  数据指针
 * @param  len   字节数
 * @retval 无
 */
void bsp_esp8266_tcp_feed_uart(const uint8_t *data, uint16_t len)
{
  if ((data == NULL) || (len == 0U)) {
    return;
  }

  /* TCP 未连接或未 enable active 模式时丢弃，避免断链后 ISR 仍入队脏数据 */
  if (s_active_mode == 0U) {
    return;
  }

  /* ISR 上下文只做 O(1) 入队；+IPD 字符串解析留给任务侧 poll_rx */
  tcp_isr_ring_push(data, len);
  tcp_signal_rx();  /* 唤醒可能阻塞在 wait_rx 的 OTA 任务 */
}

/**
 * @brief  判断 CIPSEND 事务是否进行中
 * @retval 1 进行中；0 空闲
 */
uint8_t bsp_esp8266_tcp_cipsend_active(void)
{
  return (s_cipsend_want != 0U) ? 1U : 0U;
}

/**
 * @brief  发送 ESC 中止当前 CIPSEND 并轮询 RX
 *
 * CIPSEND 超时或 FAIL 时调用：向模块发送 0x1B 取消未完成的发送事务，
 * 随后 poll_rx 清掉可能残留的 '>' / SEND OK 等 AT 文本。
 *
 * @retval 无
 */
static void tcp_cipsend_abort(void)
{
  static const uint8_t esc = 0x1BU;

  (void)bsp_esp8266_at_send_bytes(&esc, 1U);
  osDelay(50U);  /* 等待模块处理 ESC 并返回 ERROR 等应答 */
  bsp_esp8266_tcp_poll_rx();
}

/**
 * @brief  等待 CIPSEND 状态机到达指定阶段
 *
 * 在 timeout_ms 内循环 poll_rx + wait_rx，直到 s_cipsend_got 等于 want_got。
 * 等待期间定期 kick 看门狗，防止 CIPSEND 长阻塞触发复位。
 *
 * @param  want_got     期望的 s_cipsend_got 值（1='>' 已收，2=SEND OK 已收）
 * @param  timeout_ms   超时毫秒
 * @retval 1 到达目标阶段；0 超时或收到 FAIL（got==0xFF）
 */
static uint8_t tcp_wait_cipsend_got(uint8_t want_got, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  for (;;) {
    /* 每轮先 poll，把 ISR 环和 UART FIFO 中的应答字节喂给 CIPSEND 扫描器 */
    bsp_esp8266_tcp_poll_rx();

    if (s_cipsend_got == want_got) {
      return 1U;  /* 目标阶段到达 */
    }

    if (s_cipsend_got == 0xFFU) {
      return 0U;  /* 模块返回 FAIL，无需继续等 */
    }

    if ((HAL_GetTick() - start) >= timeout_ms) {
      return 0U;  /* 整体超时 */
    }

    {
      /* 分段 wait_rx（最多 50ms 一片），兼顾响应性与看门狗喂狗机会 */
      uint32_t elapsed = HAL_GetTick() - start;
      uint32_t remain = timeout_ms - elapsed;
      uint32_t slice = (remain > 50U) ? 50U : remain;

      BSP_Watchdog_Kick(BSP_WDG_ESP8266);
      (void)bsp_esp8266_tcp_wait_rx(slice);
    }
  }
}

/**
 * @brief  处理 UART 原始字节并驱动 +IPD 解析
 * @param  data  数据指针
 * @param  len   字节数
 * @retval 无
 */
static void tcp_process_uart_bytes(const uint8_t *data, uint16_t len)
{
#if (BSP_ESP8266_TCP_RX_DEBUG != 0U)
  if (len > 0U) {
    s_dbg_uart_bytes += len;
    LOG_D(ESP8266_TCP_TAG, "uart rx %u bytes (total %lu)", (unsigned)len,
          (unsigned long)s_dbg_uart_bytes);
    tcp_log_hex_line(data, (uint16_t)((len > 32U) ? 32U : len));
  }
#endif

  /* 统一入口：将原始 UART 字节喂给 +IPD/CIPSEND 状态机 */
  tcp_ipd_feed_bytes(data, len);
}

/**
 * @brief  判断 ISR 环形缓冲是否有未读数据
 * @retval 1 有数据；0 环空（head == tail）
 */
static uint8_t tcp_isr_ring_has_data(void)
{
  return (s_isr_tail != s_isr_head) ? 1U : 0U;
}

/**
 * @brief  判断 pending payload 缓冲是否有未消费数据
 * @retval 1 有数据（pos < len）；0 已消费完毕
 */
static uint8_t tcp_pending_has_data(void)
{
  return (s_pending_pos < s_pending_len) ? 1U : 0U;
}

/**
 * @brief  AT 轮询读 UART 时旁路 +IPD，避免 CIPSEND 期间丢包
 * @param  data  数据指针
 * @param  len   字节数
 * @retval 无
 */
void bsp_esp8266_tcp_feed_raw_uart(const uint8_t *data, uint16_t len)
{
  if ((data == NULL) || (len == 0U) || (s_active_mode == 0U)) {
    return;
  }

  /* AT 层轮询读 UART 时的旁路入口：直接解析 +IPD，不经过 ISR 环 */
  tcp_process_uart_bytes(data, len);
  if ((tcp_pending_has_data() != 0U) || (tcp_isr_ring_has_data() != 0U)) {
    tcp_signal_rx();  /* 确保 wait_rx 侧能感知到新数据 */
  }
}

/**
 * @brief  轮询处理 ISR 环形缓冲与 UART FIFO 中的 +IPD 数据
 *
 * OTA 任务和 CIPSEND 等待循环的核心驱动函数，每次调用：
 *   1. 批量弹出 ISR 环数据并解析
 *   2. 若 AT 不忙（或正处于 CIPSEND 会话），逐字节读 UART FIFO 剩余数据
 *
 * @retval 无
 * @note  应在 try_recv_byte / wait_rx / CIPSEND 等待前调用，保证数据及时解析
 */
void bsp_esp8266_tcp_poll_rx(void)
{
  uint8_t batch[128];
  uint16_t n;
  uint8_t ch;

  /* 阶段 A：优先 drain ISR 环（UART 中断已入队、尚未解析的原始字节） */
  for (;;) {
    n = tcp_isr_ring_pop_batch(batch, (uint16_t)sizeof(batch));
    if (n == 0U) {
      break;  /* ISR 环已空 */
    }
    tcp_process_uart_bytes(batch, n);
  }

  if (s_active_mode == 0U) {
    return;  /* TCP 未启用主动接收，不再读 UART FIFO */
  }

  /*
   * 阶段 B：读 UART FIFO 中尚未被 ISR 捕获的字节。
   * 若 AT 命令正在执行且不是 CIPSEND 会话，则跳过——
   * 避免 poll 抢走 AT 层正在等待的 "OK" 等应答。
   */
  if ((bsp_esp8266_at_is_busy() != 0U) &&
      (bsp_esp8266_tcp_cipsend_active() == 0U) &&
      (bsp_esp8266_at_in_cipsend_session() == 0U)) {
    return;
  }

  /* 非阻塞逐字节读取，直到 FIFO 为空 */
  while (bsp_esp8266_at_uart_try_read(&ch) != 0U) {
    tcp_process_uart_bytes(&ch, 1U);
  }
}

/**
 * @brief  阻塞等待 TCP 接收数据或超时
 * @param  timeout_ms  超时毫秒
 * @retval 0 有数据就绪；-1 超时
 */
int bsp_esp8266_tcp_wait_rx(uint32_t timeout_ms)
{
  /* 进入等待前先 poll 一次，避免「数据已到但未 poll」的假超时 */
  bsp_esp8266_tcp_poll_rx();

  /* 快速路径：pending 或 ISR 环中已有数据，立即返回 */
  if ((tcp_pending_has_data() != 0U) || (tcp_isr_ring_has_data() != 0U)) {
    return 0;
  }

  if (s_tcp_rx_sem == NULL) {
    /* 信号量未创建（内核未就绪），退化为纯 delay + poll */
    osDelay(timeout_ms);
    bsp_esp8266_tcp_poll_rx();
    return -1;
  }

  /* 阻塞等 UART ISR 释放的信号量；被唤醒后再 poll 一次取净 ISR 环数据 */
  if (osSemaphoreAcquire(s_tcp_rx_sem, timeout_ms) == osOK) {
    bsp_esp8266_tcp_poll_rx();
    return 0;
  }

  return -1;  /* 超时且期间无新数据到达 */
}

/**
 * @brief  非阻塞取 1 字节 TCP payload
 * @param  byte  输出字节指针
 * @retval 0 成功；-1 无数据或参数错误
 */
int bsp_esp8266_tcp_try_recv_byte(uint8_t *byte)
{
  if (byte == NULL) {
    return -1;
  }

  /* 取字节前先 poll，把 ISR 环中未解析的数据转为 pending payload */
  bsp_esp8266_tcp_poll_rx();

  /* s_pending 存放已剥离 +IPD 头的纯 TCP 字节流，SOIP 协议层逐字节消费 */
  if (s_pending_pos < s_pending_len) {
    *byte = s_pending[s_pending_pos++];
    if (s_pending_pos >= s_pending_len) {
      /* 当前 +IPD 包全部消费完毕，重置指针；下一包 poll 后会重新填充 */
      s_pending_len = 0U;
      s_pending_pos = 0U;
    }
    return 0;
  }

  return -1;  /* pending 为空，调用方可 wait_rx 后重试 */
}

/**
 * @brief  判断是否应启用后台 +IPD 接收路由
 * @retval 1 启用；0 禁用
 */
uint8_t bsp_esp8266_tcp_wants_background_rx(void)
{
  return s_active_mode;
}

/**
 * @brief  AT 事务结束时恢复后台接收
 * @retval 无
 */
void bsp_esp8266_tcp_on_at_transaction_end(void)
{
  if (s_active_mode != 0U) {
    /* AT 命令结束（如 CIPSTART 完成）后恢复 UART 空闲中断，继续接收 +IPD 推送 */
    bsp_esp8266_at_resume_background_rx();
  }
}

/**
 * @brief  查询模块 TCP 接收缓冲待读长度
 * @retval 待读字节数；-1 表示 AT 失败
 */
static int tcp_query_recv_len(void)
{
  const char *resp;
  const char *tag;
  char *endptr;

  if (!bsp_esp8266_at_send_and_wait("AT+CIPRECVLEN?", "OK", BSP_ESP8266_AT_TIMEOUT_MS)) {
    resp = bsp_esp8266_at_last_response();
    tcp_note_closed_response(resp);  /* 查询失败可能是 TCP 已断开 */
    return -1;
  }

  resp = bsp_esp8266_at_last_response();
  tcp_note_closed_response(resp);
  tag = strstr(resp, "+CIPRECVLEN:");
  if (tag == NULL) {
    return 0;  /* 应答格式异常，视为无待读数据 */
  }

  /* 解析 "+CIPRECVLEN:128" 中的数字部分 */
  return (int)strtol(tag + 12, &endptr, 10);
}

/**
 * @brief  被动模式下拉取一块 TCP 数据到 pending 缓冲
 *
 * OTA 当前使用主动 +IPD 模式，此函数为被动 CIPRECV 备用路径。
 * 流程：CIPRECVLEN? 查可用字节 → CIPRECV=n 拉取 → 解析 +CIPRECV 头 → 读剩余 payload。
 *
 * @retval 拉取字节数；0 无数据；-1 AT 失败或解析错误
 * @note  调用期间持有 AT 锁，与 CIPSEND 互斥
 */
static int tcp_fetch_pending_chunk(void)
{
  char cmd[32];
  int avail;
  const char *hdr;
  const char *colon;
  int payload_len;
  int copied = 0;

  /* 若 pending 中仍有未消费数据，直接返回剩余长度 */
  if (s_pending_pos < s_pending_len) {
    return s_pending_len - s_pending_pos;
  }

  s_pending_len = 0U;
  s_pending_pos = 0U;

  avail = tcp_query_recv_len();
  if (avail <= 0) {
    return avail;  /* 0=无数据，-1=查询失败 */
  }

  /* 单次拉取不超过 pending 缓冲容量 */
  if (avail > (int)ESP8266_TCP_RX_CHUNK) {
    avail = (int)ESP8266_TCP_RX_CHUNK;
  }

  (void)snprintf(cmd, sizeof(cmd), "AT+CIPRECV=%d", avail);

  bsp_esp8266_at_lock();
  bsp_esp8266_at_flush();  /* 清 UART 残留，避免旧字节干扰 +CIPRECV 头解析 */
  (void)bsp_esp8266_at_send(cmd);

  /* 轮询 UART 直到收到 "+CIPRECV," 头（最长约 2 秒） */
  hdr = NULL;
  for (uint32_t t = 0U; t < 2000U; t += 10U) {
    uint8_t ch;

    if (bsp_esp8266_at_uart_read(&ch, 10U) == 0U) {
      osDelay(1U);
      continue;
    }

    if (s_pending_len < (ESP8266_TCP_RX_CHUNK - 1U)) {
      s_pending[s_pending_len++] = ch;
      s_pending[s_pending_len] = '\0';  /* 临时当字符串用，便于 strstr 找头 */
    }

    hdr = strstr((char *)s_pending, "+CIPRECV,");
    if (hdr != NULL) {
      break;
    }
  }

  if (hdr == NULL) {
    bsp_esp8266_at_unlock();
    return -1;  /* 超时未收到 +CIPRECV 头 */
  }

  colon = strchr(hdr, ':');
  if (colon == NULL) {
    bsp_esp8266_at_unlock();
    return -1;
  }

  /* "+CIPRECV,128:" → payload_len=128 */
  payload_len = (int)strtol(hdr + 9, NULL, 10);
  if (payload_len <= 0) {
    bsp_esp8266_at_unlock();
    return -1;
  }

  /* 头后可能已有部分 payload 随 +CIPRECV 行一起到达，先计入 copied */
  copied = (int)(s_pending_len - (uint16_t)((colon + 1U) - (char *)s_pending));
  if (copied < 0) {
    copied = 0;
  }

  if (copied > 0) {
    /* 将已到达的 payload 移到 s_pending 起始位置 */
    memmove(s_pending, colon + 1U, (size_t)copied);
  }

  /* 继续读 UART 直到凑满 payload_len 字节 */
  while (copied < payload_len) {
    uint8_t ch;

    if (bsp_esp8266_at_uart_read(&ch, 50U) == 0U) {
      BSP_Watchdog_Kick(BSP_WDG_ESP8266);  /* 长读期间喂狗 */
      continue;
    }

    s_pending[copied++] = ch;
  }

  s_pending_len = (uint16_t)payload_len;
  s_pending_pos = 0U;
  bsp_esp8266_at_unlock();
  return payload_len;
}

/**
 * @brief  建立 TCP 连接到指定主机与端口
 * @param  host  目标 IP 或域名
 * @param  port  目标端口
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_tcp_connect(const char *host, uint16_t port)
{
  char cmd[96];

  if ((host == NULL) || (host[0] == '\0') || (port == 0U)) {
    return 0U;
  }

  /* 已连接时跳过 CIPSTART，仅确保 active 模式已启用 */
  if (tcp_link_is_up() != 0U) {
    LOG_I(ESP8266_TCP_TAG, "TCP already connected, skip reconnect");
    if (s_active_mode == 0U) {
      tcp_try_configure_recv_mode();
      tcp_enable_active_rx();
    }
    bsp_esp8266_tcp_on_at_transaction_end();
    return 1U;
  }

  /*
   * 新连接前清空全部 RX 状态：
   * 防止上一次会话残留的 +IPD 字节污染 SOIP 帧同步。
   */
  tcp_reset_pending();
  tcp_reset_ipd_parser();
  tcp_reset_isr_ring();
  tcp_drain_rx_sem();

  (void)bsp_esp8266_tcp_close();  /* 确保模块侧无残留连接 */

  /* 单连接模式：OTA 仅使用一条 TCP link，简化 +IPD 头格式 */
  if (!bsp_esp8266_at_send_and_wait("AT+CIPMUX=0", "OK", BSP_ESP8266_AT_TIMEOUT_MS)) {
    LOG_E(ESP8266_TCP_TAG, "CIPMUX failed");
    return 0U;
  }

  tcp_try_configure_recv_mode();

  (void)snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, (unsigned)port);
  if (!bsp_esp8266_at_send_and_wait_markers(cmd, BSP_ESP8266_TCP_CONNECT_TIMEOUT_MS,
                                            "CONNECT", "ALREADY CONNECTED")) {
    /* CIPSTART 超时不一定失败——模块可能已连上但未及时回 CONNECT */
    if (tcp_link_is_up() == 0U) {
      LOG_E(ESP8266_TCP_TAG, "CIPSTART failed -> %s:%u", host, (unsigned)port);
      return 0U;
    }
    LOG_W(ESP8266_TCP_TAG, "CIPSTART timeout but link is up");
  }

  tcp_enable_active_rx();
  bsp_esp8266_at_resume_background_rx();  /* 恢复 UART 空闲中断，开始 +IPD 推送 */
  LOG_I(ESP8266_TCP_TAG, "connected to OTA host %s:%u (mode=active)", host, (unsigned)port);
  return 1U;
}

/**
 * @brief  关闭当前 TCP 连接
 * @retval 1 成功或本就未连接；0 AT 失败
 */
uint8_t bsp_esp8266_tcp_close(void)
{
  const char *resp;

  /* 先禁用接收路由并清空缓冲，防止 close 期间 ISR 仍入队 */
  s_passive_ready = 0U;
  s_active_mode = 0U;
  tcp_reset_pending();
  tcp_reset_ipd_parser();
  tcp_reset_isr_ring();
  tcp_drain_rx_sem();

  if (!bsp_esp8266_at_send_and_wait("AT+CIPCLOSE", "OK", BSP_ESP8266_AT_CLOSE_TIMEOUT_MS)) {
    resp = bsp_esp8266_at_last_response();
    /*
     * 模块返回 ERROR 常见于「本无连接」场景，
     * 视为已处于断开状态，不算失败。
     */
    if ((resp != NULL) &&
        ((strstr(resp, "ERROR") != NULL) || (strstr(resp, "CLOSED") != NULL))) {
      return 1U;
    }
    return 0U;
  }

  return 1U;
}

/**
 * @brief  单次 CIPSEND 发送尝试（三阶段握手）
 *
 * 完整流程：
 *   1. AT+CIPSEND=n → 等 '>'
 *   2. 发送 n 字节二进制 payload
 *   3. 等 SEND OK（或 Recv xxx bytes）
 *
 * 任一步失败则 ESC 中止并返回 0。成功/失败路径均 poll_rx 并 signal，
 * 防止 CIPSEND 期间积累的 +IPD 应答丢失。
 *
 * @param  data         待发送数据
 * @param  len          字节数
 * @param  timeout_ms   每阶段等待超时（毫秒）
 * @retval 1 成功；0 失败
 */
static uint8_t tcp_send_one_attempt(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
  char cmd[32];

  if ((data == NULL) || (len == 0U)) {
    return 0U;
  }

  bsp_esp8266_at_lock();
  bsp_esp8266_at_begin_cipsend_keep_dma();  /* 保持 DMA RX 运行，避免 +IPD 在发送期间丢失 */

  s_cipsend_got = 0U;
  s_cipsend_want = 1U;  /* 阶段 1：等待模块返回 '>' 提示符 */

  (void)snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)len);
  if (bsp_esp8266_at_send(cmd) != HAL_OK) {
    LOG_E(ESP8266_TCP_TAG, "CIPSEND command TX failed len=%u", (unsigned)len);
    goto abort_out;
  }

  if (tcp_wait_cipsend_got(1U, timeout_ms) == 0U) {
    LOG_E(ESP8266_TCP_TAG, "CIPSEND prompt timeout len=%u", (unsigned)len);
    goto abort_out;
  }

  /* 阶段 2：'>' 已收到，发送纯二进制 payload（非 AT 文本，不含 CR/LF 后缀） */
  if (bsp_esp8266_at_send_bytes(data, len) != HAL_OK) {
    LOG_E(ESP8266_TCP_TAG, "CIPSEND payload TX failed len=%u", (unsigned)len);
    goto abort_out;
  }

  s_cipsend_got = 0U;
  s_cipsend_want = 2U;  /* 阶段 3：等待 SEND OK */

  if (tcp_wait_cipsend_got(2U, timeout_ms) == 0U) {
    LOG_E(ESP8266_TCP_TAG, "CIPSEND SEND OK timeout len=%u", (unsigned)len);
    goto abort_out;
  }

  /* 发送成功：清理 CIPSEND 状态，poll 可能积累的 +IPD 并唤醒 RX 等待者 */
  s_cipsend_want = 0U;
  s_cipsend_got = 0U;
  bsp_esp8266_at_end_cipsend_transaction();
  bsp_esp8266_tcp_poll_rx();
  if ((tcp_pending_has_data() != 0U) || (tcp_isr_ring_has_data() != 0U)) {
    tcp_signal_rx();
  }
  return 1U;

abort_out:
  /* 失败路径：ESC 中止模块侧未完成事务，同样 poll 清 RX */
  tcp_cipsend_abort();
  s_cipsend_want = 0U;
  s_cipsend_got = 0U;
  bsp_esp8266_at_end_cipsend_transaction();
  bsp_esp8266_tcp_poll_rx();
  if ((tcp_pending_has_data() != 0U) || (tcp_isr_ring_has_data() != 0U)) {
    tcp_signal_rx();
  }
  return 0U;
}

/**
 * @brief  单次 CIPSEND 发送（供 OTA poll 非阻塞重试）
 * @param  data         待发送数据
 * @param  len          字节数
 * @param  timeout_ms   超时毫秒
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_tcp_send_once(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
  return tcp_send_one_attempt(data, len, timeout_ms);
}

/**
 * @brief  通过 CIPSEND 发送 TCP 数据（含重试）
 * @param  data  待发送数据
 * @param  len   字节数
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_tcp_send(const uint8_t *data, uint16_t len)
{
  uint8_t attempt;
  uint8_t ok = 0U;
  /* 小包用 OTA 专用超时（通常更短），大包用通用 AT 超时 */
  const uint32_t timeout_ms =
      (len <= 128U) ? BSP_ESP8266_OTA_CIPSEND_TIMEOUT_MS : BSP_ESP8266_AT_TIMEOUT_MS;

  if ((data == NULL) || (len == 0U)) {
    return 0U;
  }

  for (attempt = 0U; attempt < BSP_ESP8266_CIPSEND_RETRIES; attempt++) {
    if (attempt > 0U) {
      LOG_W(ESP8266_TCP_TAG, "CIPSEND retry %u/%u len=%u pending=%u",
            (unsigned)attempt, (unsigned)BSP_ESP8266_CIPSEND_RETRIES,
            (unsigned)len, (unsigned)s_pending_len);
      /*
       * 重试前先 poll：CIPSEND 等待期间模块可能已推送 +IPD，
       * 若不 poll 直接重发会导致 SOIP 应答丢失或乱序。
       */
      bsp_esp8266_tcp_poll_rx();
      osDelay(40U);  /* 短暂间隔，给模块从 FAIL 状态恢复的时间 */
    }

    if (tcp_send_one_attempt(data, len, timeout_ms) != 0U) {
      ok = 1U;
      break;
    }
  }

  return ok;
}

/**
 * @brief  OTA 期间 CIPSEND 连续失败时关闭并重连 PC Server
 * @retval 1 重连成功；0 失败
 */
uint8_t bsp_esp8266_tcp_recover_ota_link(void)
{
  LOG_W(ESP8266_TCP_TAG, "OTA TCP recover: close + reconnect %s:%u",
        BSP_ESP8266_OTA_HOST, (unsigned)BSP_ESP8266_OTA_PORT);

  /* 第一步：关闭当前 TCP，清模块侧发送/接收状态 */
  (void)bsp_esp8266_tcp_close();
  osDelay(300U);  /* 等待模块完成 CIPCLOSE 内部清理 */
  bsp_esp8266_tcp_poll_rx();

  /* 第二步：再次清空 MCU 侧全部 RX 状态，确保重连后 SOIP 从干净态开始 */
  tcp_reset_pending();
  tcp_reset_ipd_parser();
  tcp_reset_isr_ring();
  tcp_drain_rx_sem();

  /* 第三步：重新 CIPSTART 到 OTA Server */
  if (bsp_esp8266_tcp_connect(BSP_ESP8266_OTA_HOST, BSP_ESP8266_OTA_PORT) == 0U) {
    LOG_E(ESP8266_TCP_TAG, "OTA TCP recover failed");
    return 0U;
  }

  LOG_I(ESP8266_TCP_TAG, "OTA TCP recover OK");
  return 1U;
}

/**
 * @brief  阻塞接收 1 字节 TCP payload
 * @param  byte         输出字节指针
 * @param  timeout_ms   超时毫秒
 * @retval 0 成功；-1 超时或参数错误
 */
int bsp_esp8266_tcp_recv_byte(uint8_t *byte, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  if (byte == NULL) {
    return -1;
  }

  for (;;) {
    /* 非阻塞尝试取 1 字节；成功则立即返回 */
    if (bsp_esp8266_tcp_try_recv_byte(byte) == 0) {
      return 0;
    }

    /* 被动模式备用路径：主动 +IPD 模式下 s_passive_ready 恒为 0，不会进入 */
    if (s_passive_ready != 0U) {
      if (tcp_fetch_pending_chunk() > 0) {
        continue;  /* 拉到新数据，回到循环头部再 try_recv_byte */
      }
    }

    if ((HAL_GetTick() - start) >= timeout_ms) {
      return -1;  /* 整体超时 */
    }

    {
      uint32_t elapsed = HAL_GetTick() - start;
      uint32_t remain = timeout_ms - elapsed;

      BSP_Watchdog_Kick(BSP_WDG_ESP8266);
      if (bsp_esp8266_tcp_wait_rx(remain) != 0) {
        /* wait_rx 超时：最后再 try 一次，区分「刚好到达」与「真超时」 */
        if (bsp_esp8266_tcp_try_recv_byte(byte) == 0) {
          return 0;
        }
        return -1;
      }
    }
  }
}

/**
 * @brief  发送 AT+CIPSTATUS 并解析 STATUS 码
 * @param  out_status  输出 STATUS 值指针
 * @retval 0 成功；-1 失败
 */
static int esp8266_query_cipstatus(int *out_status)
{
  const char *resp;
  const char *status;

  if (out_status == NULL) {
    return -1;
  }

  /* 最多重试 2 次：偶发 UART 噪声可能导致第一次 CIPSTATUS 无 STATUS 字段 */
  for (uint32_t attempt = 0U; attempt < 2U; attempt++) {
    if (!bsp_esp8266_at_send_and_wait("AT+CIPSTATUS", "STATUS:",
                                      ESP8266_CIPSTATUS_TIMEOUT_MS)) {
      resp = bsp_esp8266_at_last_response();
      tcp_note_closed_response(resp);
      if (attempt == 0U) {
        osDelay(50U);
        continue;  /* 首次失败，短暂等待后重试 */
      }
      return -1;
    }

    resp = bsp_esp8266_at_last_response();
    status = strstr(resp, "STATUS:");
    if (status != NULL) {
      /*
       * STATUS 码含义（ESP-AT 标准）：
       *   2 = Got IP（WiFi 已连接）
       *   3 = TCP/UDP 连接已建立  ← OTA 所需
       *   4 = 断开连接
       *   5 = 未获得 IP
       */
      *out_status = (int)strtol(status + 7, NULL, 10);
      tcp_note_closed_response(resp);
      return 0;
    }

    if (attempt == 0U) {
      osDelay(50U);
    }
  }

  return -1;
}

/**
 * @brief  查询 ESP-AT 链路状态码
 * @retval STATUS 码（2..5）；-1 表示 AT 超时或错误
 */
int bsp_esp8266_tcp_query_link_status(void)
{
  int value = 0;

  if (esp8266_query_cipstatus(&value) != 0) {
    return -1;
  }

  return value;
}

/**
 * @brief  判断 TCP 是否处于已连接状态
 * @retval 1 已连接；0 未连接或查询失败
 */
uint8_t bsp_esp8266_tcp_is_connected(void)
{
  int value;

  if (esp8266_query_cipstatus(&value) != 0) {
    return 0U;  /* AT 查询失败，保守返回未连接 */
  }

  /* STATUS==3 表示 TCP/UDP 传输层已建立，与 ESP8266_STATUS_TCP_UP 一致 */
  return (value == ESP8266_STATUS_TCP_UP) ? 1U : 0U;
}
