/* 纯算法回归：用独立三相 RL 模型验证坐标、调制和电流收敛。 */
#include "alg_foc.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846f

/** @brief 从三相占空比重建相电压，驱动独立三相 RL 模型。 */
static void test_tracking(void)
{
    const float buses[] = {8.0f, 12.0f, 24.0f, 30.0f};
    const alg_foc_config_t config = {0.5f, 0.005f, 1.4f};
    for (uint32_t bus = 0u; bus < 4u; bus++)
    {
        for (uint32_t sector = 0u; sector < 24u; sector++)
        {
            float angle = (float)sector * TEST_PI / 12.0f;
            alg_foc_state_t state = {0};
            alg_foc_input_t input = {0.0f, 0.0f, 0.0f, 0.4f, -0.3f,
                                     sinf(angle), cosf(angle), 1.0f / buses[bus]};
            alg_foc_output_t out = {0};
            for (uint32_t sample = 0u; sample < 10000u; sample++)
            {
                float mean;
                assert(alg_foc_step_current(&config, &state, &input, &out) == 1u);
                assert(out.du >= 0.05f && out.du <= 0.92f);
                assert(out.dv >= 0.05f && out.dv <= 0.92f);
                assert(out.dw >= 0.05f && out.dw <= 0.92f);
                mean = (out.du + out.dv + out.dw) / 3.0f;
                // R=1 ohm、L=2 mH、dt=50 us，使用三相端电压独立积分。
                input.iu += 0.025f * ((out.du - mean) * buses[bus] - input.iu);
                input.iv += 0.025f * ((out.dv - mean) * buses[bus] - input.iv);
                input.iw += 0.025f * ((out.dw - mean) * buses[bus] - input.iw);
            }
            assert(fabsf(out.id - input.id_ref) < 0.002f);
            assert(fabsf(out.iq - input.iq_ref) < 0.002f);
            assert(fabsf(input.iu + input.iv + input.iw) < 0.001f);
        }
    }
}

/** @brief 验证已知坐标变换、饱和退积分、非有限拒绝及重启清零。 */
static void test_limits(void)
{
    const alg_foc_config_t config = {0.5f, 0.005f, 1.4f};
    alg_foc_state_t state = {0};
    alg_foc_input_t input = {1.0f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f / 24.0f};
    alg_foc_output_t out;
    assert(alg_foc_step_current(&config, &state, &input, &out) == 1u);
    assert(fabsf(out.id - 1.0f) < 0.00001f && fabsf(out.iq) < 0.00001f);
    assert(out.du == 0.485f && out.dv == 0.485f && out.dw == 0.485f);
    input.sin_angle = 1.0f; input.cos_angle = 0.0f;
    assert(alg_foc_step_current(&config, &state, &input, &out) == 1u);
    assert(fabsf(out.id) < 0.00001f && fabsf(out.iq + 1.0f) < 0.00001f);
    alg_foc_reset_state(&state);
    input = (alg_foc_input_t){0.0f, 0.0f, 0.0f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f / 24.0f};
    for (uint32_t i = 0u; i < 5000u; i++)
    {
        assert(alg_foc_step_current(&config, &state, &input, &out) == 1u);
        assert(out.vd == config.axis_limit && state.integral_d == 0.0f);
    }
    input.id_ref = -0.2f;
    assert(alg_foc_step_current(&config, &state, &input, &out) == 1u && out.vd < 0.0f);
    input.iu = NAN;
    {
        float integral = state.integral_d;
        float previous = out.du;
        assert(alg_foc_step_current(&config, &state, &input, &out) == 0u);
        assert(state.integral_d == integral && out.du == previous);
    }
    alg_foc_reset_state(&state);
    assert(state.integral_d == 0.0f && state.integral_q == 0.0f);
}

/** @brief 算法测试入口。 */
int32_t main(void)
{
    test_tracking();
    test_limits();
    puts("PASS: FOC coordinates, SVPWM, RL tracking, saturation recovery, non-finite input");
    return 0;
}
