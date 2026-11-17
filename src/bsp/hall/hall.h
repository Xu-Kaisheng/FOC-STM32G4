/**
 * @file    hall.h
 * @brief   霍尔传感器读取（PC6/PC7/PC8，120 度安装）
 *
 * 三个霍尔共给出 6 种合法状态（000/111 为非法），每态对应 60 度电角度。
 * 本电机 4 极 = 2 对极，机械转一圈会看到该序列重复两遍。
 */
#ifndef HALL_H
#define HALL_H

#include <stdint.h>

/* ---------------- 公共类型 ---------------- */

/* 霍尔状态：1~6，0 表示非法组合（接线错误或传感器故障） */
typedef enum
{
    HALL_INVALID = 0,
    HALL_S1 = 1,
    HALL_S2 = 2,
    HALL_S3 = 3,
    HALL_S4 = 4,
    HALL_S5 = 5,
    HALL_S6 = 6
} hall_state_t;

/* ---------------- init ---------------- */

/**
 * @brief  初始化：把三路霍尔配置为上拉输入（CubeMX 已配，这里只做校验）
 */
void bsp_hall_init(void);

/* ---------------- read ---------------- */

/**
 * @brief  读一次霍尔状态
 * @retval 1~6 合法状态，0 非法（000 或 111）
 */
hall_state_t bsp_hall_read(void);

/**
 * @brief  状态对应的相对扇区起始角度（S1 为零点）
 * @param  s: 霍尔状态
 * @retval 角度，单位度；非法状态返回 0
 * @note   按 1→3→2→6→4→5 映射；实际方向和电角度偏移须标定。
 *         使用前应先排除 HALL_INVALID，返回 0 也可能是合法 S1。
 */
float bsp_hall_get_sector_deg(hall_state_t s);

/**
 * @brief  状态转字符串，打印用
 * @param  s: 霍尔状态
 * @retval "S1".."S6" 或 "INV"
 */
const char *bsp_hall_get_state_str(hall_state_t s);

#endif /* HALL_H */
