#include "math_bridge.h"
#include <math_neon.h>

/* Android's libcaustic uses base AAPCS/softfp.  These entry points must not
 * resolve to VitaSDK's hardfp libm functions.  Caustic's DSP is primarily
 * single precision; double imports are adapted through the NEON float
 * implementation to preserve the calling convention on first-stage ports. */
double bridge_acos(double x) { return (double)acosf_neon_sfp((float)x); }
double bridge_asin(double x) { return (double)asinf_neon_sfp((float)x); }
float bridge_atanf(float x) {
    const float half_pi = 1.57079632679489661923f;
    float sign = x < 0.0f ? -1.0f : 1.0f;
    float a = x < 0.0f ? -x : x;
    float result;
    if (a > 1.0f) {
        float inverse = 1.0f / a;
        result = half_pi - inverse *
            (0.7853981634f + 0.273f * (1.0f - inverse));
    } else {
        result = a * (0.7853981634f + 0.273f * (1.0f - a));
    }
    return sign * result;
}

float bridge_atan2f(float y, float x) {
    const float pi = 3.14159265358979323846f;
    const float half_pi = 1.57079632679489661923f;
    if (x > 0.0f) return bridge_atanf(y / x);
    if (x < 0.0f)
        return bridge_atanf(y / x) + (y >= 0.0f ? pi : -pi);
    if (y > 0.0f) return half_pi;
    if (y < 0.0f) return -half_pi;
    return 0.0f;
}

double bridge_atan(double x) { return (double)bridge_atanf((float)x); }
double bridge_atan2(double y, double x) { return (double)bridge_atan2f((float)y, (float)x); }
double bridge_ceil(double x) { return (double)ceilf_neon_sfp((float)x); }
static float reduce_angle(float x) {
    const float pi = 3.14159265358979323846f;
    const float two_pi = 6.28318530717958647692f;
    union { float f; unsigned u; } bits = { x };
    if ((bits.u & 0x7f800000u) == 0x7f800000u)
        return 0.0f;
    /* Keep range reduction constant-time. PCMSynth's wheel can briefly send
     * extremely large angles while wrapping. */
    if (x > 1000000.0f || x < -1000000.0f)
        return 0.0f;
    int turns = (int)(x / two_pi);
    x -= (float)turns * two_pi;
    if (x > pi) x -= two_pi;
    else if (x < -pi) x += two_pi;
    return x;
}

float bridge_sinf(float x) {
    const float half_pi = 1.57079632679489661923f;
    const float pi = 3.14159265358979323846f;
    x = reduce_angle(x);
    if (x > half_pi) x = pi - x;
    else if (x < -half_pi) x = -pi - x;
    float x2 = x * x;
    return x * (1.0f + x2 * (-0.1666666716f + x2 *
           (0.0083333477f + x2 * -0.0001984090f)));
}

float bridge_cosf(float x) {
    return bridge_sinf(x + 1.57079632679489661923f);
}

void bridge_sincosf(float x, float *s, float *c) {
    *s = bridge_sinf(x);
    *c = bridge_cosf(x);
}

void bridge_sincos(double x, double *s, double *c) {
    float sf, cf;
    bridge_sincosf((float)x, &sf, &cf);
    *s = (double)sf;
    *c = (double)cf;
}

double bridge_cos(double x) { return (double)bridge_cosf((float)x); }
double bridge_difftime(long end, long beginning) { return (double)(end - beginning); }
double bridge_exp(double x) { return (double)expf_neon_sfp((float)x); }
double bridge_exp2(double x) { return (double)powf_neon_sfp(2.0f, (float)x); }
float bridge_exp2f(float x) { return powf_neon_sfp(2.0f, x); }
double bridge_floor(double x) { return (double)floorf_neon_sfp((float)x); }
double bridge_fmod(double x, double y) { return (double)fmodf_neon_sfp((float)x, (float)y); }
double bridge_ldexp(double x, int exp) { return (double)ldexpf_neon_sfp((float)x, exp); }
double bridge_log(double x) { return (double)logf_neon_sfp((float)x); }
double bridge_log10(double x) { return (double)log10f_neon_sfp((float)x); }
long bridge_lrint(double x) { return (long)(x >= 0.0 ? x + 0.5 : x - 0.5); }
long bridge_lrintf(float x) { return (long)(x >= 0.0f ? x + 0.5f : x - 0.5f); }
float bridge_modff(float x, float *integer_part) {
    int whole = (int)x;
    *integer_part = (float)whole;
    return x - *integer_part;
}
double bridge_pow(double x, double y) { return (double)powf_neon_sfp((float)x, (float)y); }
double bridge_rint(double x) { return (double)bridge_lrint(x); }
float bridge_rintf(float x) { return (float)bridge_lrintf(x); }
double bridge_sin(double x) { return (double)bridge_sinf((float)x); }
double bridge_sqrt(double x) { return (double)sqrtf_neon_sfp((float)x); }
double bridge_tan(double x) { return (double)tanf_neon_sfp((float)x); }
