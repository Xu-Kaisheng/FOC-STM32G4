/**
 * @file    hall.c
 * @brief   霍尔传感器读取实现
 */
#include "hall.h"
#include "main.h"

/* 霍尔引脚由各板 CubeMX 的 HALL_U/V/W 标签定义。 */

/* 每个霍尔状态覆盖 60 度电角度 */
#define HALL_SECTOR_DEG  60.0f

/* 正点原子样例按本工程 U/V/W 位序换算后为 1→3→2→6→4→5。
 * 表按原始编码索引，S1 暂作相对零点；实际转向和电角度偏移须实测标定。 */
static const uint8_t s_hall_sector_index[HALL_S6 + 1u] =
{
    [HALL_INVALID] = 0u,
    [HALL_S1] = 0u,
    [HALL_S2] = 2u,
    [HALL_S3] = 1u,
    [HALL_S4] = 4u,
    [HALL_S5] = 5u,
    [HALL_S6] = 3u
};

/* ---------------- init ---------------- */

/**
 * @brief  初始化霍尔接口
 * @note   引脚已由 CubeMX 的 MX_GPIO_Init 配成上拉输入，这里不做重复配置
 */
void bsp_hall_init(void)
{
    /* 预留：后续若要加 EXTI 边沿捕获，在这里使能中断 */
}

/* ---------------- read ---------------- */

/**
 * @brief  读一次霍尔状态
 * @retval 1~6 合法状态，0 非法
 */
hall_state_t bsp_hall_read(void)
{
    uint8_t u = (HAL_GPIO_ReadPin(HALL_U_GPIO_Port, HALL_U_Pin) == GPIO_PIN_SET) ? 1u : 0u;
    uint8_t v = (HAL_GPIO_ReadPin(HALL_V_GPIO_Port, HALL_V_Pin) == GPIO_PIN_SET) ? 1u : 0u;
    uint8_t w = (HAL_GPIO_ReadPin(HALL_W_GPIO_Port, HALL_W_Pin) == GPIO_PIN_SET) ? 1u : 0u;

    // 三个 bit 拼成状态号：U 为最高位
    uint8_t code = (uint8_t)((u << 2) | (v << 1) | w);

    switch (code)
    {
        case 1u: return HALL_S1;
        case 2u: return HALL_S2;
        case 3u: return HALL_S3;
        case 4u: return HALL_S4;
        case 5u: return HALL_S5;
        case 6u: return HALL_S6;
        default: break;   /* 0 与 7：非法组合 */
    }
    return HALL_INVALID;
}

/**
 * @brief  状态对应的相对扇区起始角度（S1 为零点）
 * @param  s: 霍尔状态
 * @retval 角度（度）
 * @note   方向及电角度零点未标定前，不可直接作为 FOC 转子角度。
 */
float bsp_hall_get_sector_deg(hall_state_t s)
{
    if ((s < HALL_S1) || (s > HALL_S6))
    {
        return 0.0f;
    }
    // 原始编码不按扇区顺序递增，先查表再换算，避免角度跳跃和反向。
    return (float)s_hall_sector_index[(uint32_t)s] * HALL_SECTOR_DEG;
}

/**
 * @brief  状态转字符串
 * @param  s: 霍尔状态
 * @retval 可打印字符串
 */
const char *bsp_hall_get_state_str(hall_state_t s)
{
    switch (s)
    {
        case HALL_S1: return "S1";
        case HALL_S2: return "S2";
        case HALL_S3: return "S3";
        case HALL_S4: return "S4";
        case HALL_S5: return "S5";
        case HALL_S6: return "S6";
        default:      return "INV";
    }
}
