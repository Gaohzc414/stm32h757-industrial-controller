/**
 * @file    bsp_modbus_coil_nv.c
 * @brief   Modbus 线圈非易失性存储实现（SD 卡二进制文件）
 */
#include "bsp_modbus_coil_nv.h"

#include "bsp_modbus_app.h"
#include "bsp_driver_sd.h"
#include "cmsis_os.h"
#include "fatfs.h"

#include <stddef.h>
#include <string.h>

#define MB_COIL_NV_MAGIC     0x4D434F4Cu /* 'MCOL' */
#define MB_COIL_NV_VERSION   1U
#define MB_COIL_NV_FILE      "0:/mb_coils.bin"
#define MB_COIL_NV_LOCK_MS   5000U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint8_t coils[MODBUS_COIL_COUNT];
    uint16_t crc;
} mb_coil_nv_file_t;

static bool s_mounted;
static uint8_t s_last_saved[MODBUS_COIL_COUNT];
static bool s_have_last_saved;
static FIL s_nv_fil;
static osMutexId_t s_nv_mutex;

/**
 * @brief  获取 NV 文件访问互斥锁
 * @retval true  加锁成功；false 失败
 */
static bool mb_coil_nv_lock(void)
{
    if (s_nv_mutex == NULL) {
        return false;
    }

    return (osMutexAcquire(s_nv_mutex, MB_COIL_NV_LOCK_MS) == osOK);
}

/**
 * @brief  释放 NV 文件访问互斥锁
 */
static void mb_coil_nv_unlock(void)
{
    if (s_nv_mutex != NULL) {
        (void)osMutexRelease(s_nv_mutex);
    }
}

/**
 * @brief  计算 Modbus 风格 CRC16
 * @param  data  数据指针
 * @param  len   数据长度
 * @retval CRC16 值
 */
static uint16_t mb_coil_nv_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/**
 * @brief  将线圈数组打包为 NV 文件结构
 * @param  file   输出文件结构
 * @param  coils  线圈数据
 * @param  count  线圈数量
 */
static void mb_coil_nv_pack(mb_coil_nv_file_t *file, const uint8_t *coils, uint16_t count)
{
    uint16_t n = count;

    if (n > MODBUS_COIL_COUNT) {
        n = MODBUS_COIL_COUNT;
    }

    memset(file, 0, sizeof(*file));
    file->magic = MB_COIL_NV_MAGIC;
    file->version = MB_COIL_NV_VERSION;
    file->count = n;

    for (uint16_t i = 0U; i < n; i++) {
        file->coils[i] = coils[i] ? 1U : 0U;
    }

    file->crc = mb_coil_nv_crc16((const uint8_t *)file,
                                 offsetof(mb_coil_nv_file_t, crc));
}

/**
 * @brief  校验并解包 NV 文件到线圈数组
 * @param  file   文件结构
 * @param  coils  输出线圈缓冲区
 * @param  count  期望线圈数量
 * @retval true  解包成功；false 校验失败
 */
static bool mb_coil_nv_unpack(const mb_coil_nv_file_t *file, uint8_t *coils, uint16_t count)
{
    uint16_t expect_crc;
    uint16_t n;

    if ((file->magic != MB_COIL_NV_MAGIC) || (file->version != MB_COIL_NV_VERSION)) {
        return false;
    }

    if (file->count == 0U || file->count > MODBUS_COIL_COUNT) {
        return false;
    }

    expect_crc = mb_coil_nv_crc16((const uint8_t *)file,
                                    offsetof(mb_coil_nv_file_t, crc));
    if (file->crc != expect_crc) {
        return false;
    }

    n = count;
    if (n > file->count) {
        n = file->count;
    }

    for (uint16_t i = 0U; i < n; i++) {
        coils[i] = file->coils[i] ? 1U : 0U;
    }

    return true;
}

/**
 * @brief  检查 SD 卡介质是否就绪
 * @retval true  SD 卡已插入；false 未就绪
 */
bool BSP_Modbus_CoilNv_MediaReady(void)
{
    return (BSP_SD_IsDetected() == SD_PRESENT);
}

/**
 * @brief  初始化线圈 NV 存储（挂载 FatFS、创建互斥量）
 * @retval true  成功；false 失败
 */
bool BSP_Modbus_CoilNv_Init(void)
{
    FRESULT fr;

    if (!BSP_Modbus_CoilNv_MediaReady()) {
        return false;
    }

    if (s_nv_mutex == NULL) {
        s_nv_mutex = osMutexNew(NULL);
        if (s_nv_mutex == NULL) {
            return false;
        }
    }

    if (s_mounted) {
        return true;
    }

    if (!mb_coil_nv_lock()) {
        return false;
    }

    fr = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
    mb_coil_nv_unlock();

    if (fr != FR_OK) {
        return false;
    }

    s_mounted = true;
    return true;
}

/**
 * @brief  从 SD 卡加载线圈状态
 * @param  coils  输出缓冲区
 * @param  count  线圈数量
 * @retval true  加载成功；false 失败
 */
bool BSP_Modbus_CoilNv_Load(uint8_t *coils, uint16_t count)
{
    mb_coil_nv_file_t file;
    UINT br;
    FRESULT fr;
    bool ok = false;

    if ((coils == NULL) || (count == 0U) || (count > MODBUS_COIL_COUNT)) {
        return false;
    }

    if (!s_mounted && !BSP_Modbus_CoilNv_Init()) {
        return false;
    }

    if (!mb_coil_nv_lock()) {
        return false;
    }

    fr = f_open(&s_nv_fil, MB_COIL_NV_FILE, FA_READ);
    if (fr != FR_OK) {
        mb_coil_nv_unlock();
        return false;
    }

    fr = f_read(&s_nv_fil, &file, sizeof(file), &br);
    f_close(&s_nv_fil);

    if ((fr == FR_OK) && (br == sizeof(file)) && mb_coil_nv_unpack(&file, coils, count)) {
        memcpy(s_last_saved, coils, count);
        s_have_last_saved = true;
        ok = true;
    }

    mb_coil_nv_unlock();
    return ok;
}

/**
 * @brief  将线圈状态保存到 SD 卡
 * @param  coils  线圈数据
 * @param  count  线圈数量
 * @retval true  保存成功；false 失败
 */
bool BSP_Modbus_CoilNv_Save(const uint8_t *coils, uint16_t count)
{
    mb_coil_nv_file_t file;
    UINT bw;
    FRESULT fr;
    uint16_t n;
    bool ok = false;

    if ((coils == NULL) || (count == 0U) || (count > MODBUS_COIL_COUNT)) {
        return false;
    }

    n = count;
    if (n > MODBUS_COIL_COUNT) {
        n = MODBUS_COIL_COUNT;
    }

    if (s_have_last_saved && (memcmp(s_last_saved, coils, n) == 0)) {
        return true;
    }

    if (!s_mounted && !BSP_Modbus_CoilNv_Init()) {
        return false;
    }

    if (!mb_coil_nv_lock()) {
        return false;
    }

    mb_coil_nv_pack(&file, coils, n);

    fr = f_open(&s_nv_fil, MB_COIL_NV_FILE, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        fr = f_write(&s_nv_fil, &file, sizeof(file), &bw);
        f_close(&s_nv_fil);

        if ((fr == FR_OK) && (bw == sizeof(file))) {
            memcpy(s_last_saved, coils, n);
            s_have_last_saved = true;
            ok = true;
        }
    }

    mb_coil_nv_unlock();
    return ok;
}
