#include "math_bridge.h"

/*
 * libcaustic.so uses Android ARMv7 base AAPCS (softfp), while VitaSDK libm
 * uses the VFP calling convention. These bridges accept Android-style calls
 * and use explicit aapcs-vfp declarations for the accurate Vita libm calls.
 *
 * The old bridges used limited-range approximations. In particular, the
 * approximate tanf could produce invalid filter coefficients near pi/2 and
 * drive Caustic's recursive synth filters to full scale.
 */
#define VITA_LIBM(name, symbol, result, args) \
    extern result name args __asm__(symbol) __attribute__((pcs("aapcs-vfp")))

VITA_LIBM(vita_acos,   "acos",   double, (double));
VITA_LIBM(vita_acosf,  "acosf",  float,  (float));
VITA_LIBM(vita_asin,   "asin",   double, (double));
VITA_LIBM(vita_asinf,  "asinf",  float,  (float));
VITA_LIBM(vita_atan,   "atan",   double, (double));
VITA_LIBM(vita_atanf,  "atanf",  float,  (float));
VITA_LIBM(vita_atan2,  "atan2",  double, (double, double));
VITA_LIBM(vita_atan2f, "atan2f", float,  (float, float));
VITA_LIBM(vita_ceil,   "ceil",   double, (double));
VITA_LIBM(vita_ceilf,  "ceilf",  float,  (float));
VITA_LIBM(vita_cos,    "cos",    double, (double));
VITA_LIBM(vita_cosf,   "cosf",   float,  (float));
VITA_LIBM(vita_exp,    "exp",    double, (double));
VITA_LIBM(vita_expf,   "expf",   float,  (float));
VITA_LIBM(vita_exp2,   "exp2",   double, (double));
VITA_LIBM(vita_exp2f,  "exp2f",  float,  (float));
VITA_LIBM(vita_floor,  "floor",  double, (double));
VITA_LIBM(vita_floorf, "floorf", float,  (float));
VITA_LIBM(vita_fmod,   "fmod",   double, (double, double));
VITA_LIBM(vita_fmodf,  "fmodf",  float,  (float, float));
VITA_LIBM(vita_ldexp,  "ldexp",  double, (double, int));
VITA_LIBM(vita_ldexpf, "ldexpf", float,  (float, int));
VITA_LIBM(vita_log,    "log",    double, (double));
VITA_LIBM(vita_logf,   "logf",   float,  (float));
VITA_LIBM(vita_log10,  "log10",  double, (double));
VITA_LIBM(vita_log10f, "log10f", float,  (float));
VITA_LIBM(vita_lrint,  "lrint",  long,   (double));
VITA_LIBM(vita_lrintf, "lrintf", long,   (float));
VITA_LIBM(vita_modff,  "modff",  float,  (float, float *));
VITA_LIBM(vita_pow,    "pow",    double, (double, double));
VITA_LIBM(vita_powf,   "powf",   float,  (float, float));
VITA_LIBM(vita_rint,   "rint",   double, (double));
VITA_LIBM(vita_rintf,  "rintf",  float,  (float));
VITA_LIBM(vita_sin,    "sin",    double, (double));
VITA_LIBM(vita_sinf,   "sinf",   float,  (float));
VITA_LIBM(vita_sqrt,   "sqrt",   double, (double));
VITA_LIBM(vita_sqrtf,  "sqrtf",  float,  (float));
VITA_LIBM(vita_tan,    "tan",    double, (double));
VITA_LIBM(vita_tanf,   "tanf",   float,  (float));

double bridge_acos(double x) { return vita_acos(x); }
float bridge_acosf(float x) { return vita_acosf(x); }
double bridge_asin(double x) { return vita_asin(x); }
float bridge_asinf(float x) { return vita_asinf(x); }
double bridge_atan(double x) { return vita_atan(x); }
float bridge_atanf(float x) { return vita_atanf(x); }
double bridge_atan2(double y, double x) { return vita_atan2(y, x); }
float bridge_atan2f(float y, float x) { return vita_atan2f(y, x); }
double bridge_ceil(double x) { return vita_ceil(x); }
float bridge_ceilf(float x) { return vita_ceilf(x); }
double bridge_cos(double x) { return vita_cos(x); }
float bridge_cosf(float x) { return vita_cosf(x); }
double bridge_difftime(long end, long beginning) { return (double)(end - beginning); }
double bridge_exp(double x) { return vita_exp(x); }
float bridge_expf(float x) { return vita_expf(x); }
double bridge_exp2(double x) { return vita_exp2(x); }
float bridge_exp2f(float x) { return vita_exp2f(x); }
double bridge_floor(double x) { return vita_floor(x); }
float bridge_floorf(float x) { return vita_floorf(x); }
double bridge_fmod(double x, double y) { return vita_fmod(x, y); }
float bridge_fmodf(float x, float y) { return vita_fmodf(x, y); }
double bridge_ldexp(double x, int exponent) { return vita_ldexp(x, exponent); }
float bridge_ldexpf(float x, int exponent) { return vita_ldexpf(x, exponent); }
double bridge_log(double x) { return vita_log(x); }
float bridge_logf(float x) { return vita_logf(x); }
double bridge_log10(double x) { return vita_log10(x); }
float bridge_log10f(float x) { return vita_log10f(x); }
long bridge_lrint(double x) { return vita_lrint(x); }
long bridge_lrintf(float x) { return vita_lrintf(x); }
float bridge_modff(float x, float *integer_part) { return vita_modff(x, integer_part); }
double bridge_pow(double x, double y) { return vita_pow(x, y); }
float bridge_powf(float x, float y) { return vita_powf(x, y); }
double bridge_rint(double x) { return vita_rint(x); }
float bridge_rintf(float x) { return vita_rintf(x); }
double bridge_sin(double x) { return vita_sin(x); }
float bridge_sinf(float x) { return vita_sinf(x); }
void bridge_sincos(double x, double *s, double *c) {
    *s = vita_sin(x);
    *c = vita_cos(x);
}
void bridge_sincosf(float x, float *s, float *c) {
    *s = vita_sinf(x);
    *c = vita_cosf(x);
}
double bridge_sqrt(double x) { return vita_sqrt(x); }
float bridge_sqrtf(float x) { return vita_sqrtf(x); }
double bridge_tan(double x) { return vita_tan(x); }
float bridge_tanf(float x) { return vita_tanf(x); }
