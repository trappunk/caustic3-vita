#include <psp2/audioout.h>
#include <psp2/audioin.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/touch.h>
#include <vitaGL.h>
#include <kubridge.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <FalsoJNI/FalsoJNI.h>
#include <so_util/so_util.h>

#include "dynlib.h"
#include "apk_extract.h"
#include "log.h"

#define SO_PATH "app0:/lib/libcaustic.so"
#define DATA_PATH "ux0:/data/CAUSTIC3/"
#define LOAD_ADDRESS 0x98000000
#define WIDTH 960
#define HEIGHT 544
#define AUDIO_IN_FRAMES 256
#define AUDIO_OUT_FRAMES 256
#define MIC_INPUT_FRAMES 768
#define MIC_CAUSTIC_FRAMES 256

/* Front-panel touches use IDs 0..7. Keep synthetic controller pointers out of
 * that range so physical multitouch and button controls can coexist. */
#define PAD_POINTER_ID SCE_TOUCH_MAX_REPORT
#define RACK_POINTER_ID (SCE_TOUCH_MAX_REPORT + 1)
#define MACHINE_MANAGEMENT_X 31
#define MACHINE_MANAGEMENT_Y 519
#define TRANSPORT_PLAY_X 700
#define TRANSPORT_STOP_X 798
#define TRANSPORT_Y 519
#define RACK_SWIPE_X 32
#define RACK_SWIPE_CENTER_Y (HEIGHT / 2)
#define RACK_SWIPE_MIN_Y 72
#define RACK_SWIPE_MAX_Y (HEIGHT - 72)

int _newlib_heap_size_user = 192 * 1024 * 1024;
int sceLibcHeapSize = 4 * 1024 * 1024;

so_module so_mod;

static volatile int handling_exception;

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* Draw the controller focus without depending on Caustic's shaders. glClear
 * obeys the scissor rectangle, making this robust even when the app changes
 * its GLES program or vertex format between machines. */
static void draw_controller_focus(int x, int y, int grabbed) {
    GLint old_scissor[4];
    GLfloat old_clear[4];
    GLboolean old_mask[4];
    GLboolean had_scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, old_scissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);
    glGetBooleanv(GL_COLOR_WRITEMASK, old_mask);

    int left = clamp_int(x - 10, 0, WIDTH - 1);
    int right = clamp_int(x + 10, 0, WIDTH - 1);
    int top = clamp_int(y - 10, 0, HEIGHT - 1);
    int bottom = clamp_int(y + 10, 0, HEIGHT - 1);
    int box_width = right - left + 1;
    int box_height = bottom - top + 1;
    int gl_bottom = HEIGHT - bottom - 1;

    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (grabbed)
        glClearColor(0.15f, 1.0f, 0.25f, 1.0f);
    else
        glClearColor(1.0f, 0.85f, 0.05f, 1.0f);
    glScissor(left, gl_bottom, box_width, 2);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(left, gl_bottom + box_height - 2, box_width, 2);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(left, gl_bottom, 2, box_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(left + box_width - 2, gl_bottom, 2, box_height);
    glClear(GL_COLOR_BUFFER_BIT);

    glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
    glColorMask(old_mask[0], old_mask[1], old_mask[2], old_mask[3]);
    glScissor(old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3]);
    if (!had_scissor)
        glDisable(GL_SCISSOR_TEST);
}

static void caustic_exception_handler(KuKernelExceptionContext *ctx) {
    if (handling_exception)
        sceKernelExitProcess(-1);
    handling_exception = 1;

    char report[1400];
    unsigned relative_pc =
        (ctx->pc >= LOAD_ADDRESS && ctx->pc < LOAD_ADDRESS + 0x05000000)
            ? ctx->pc - LOAD_ADDRESS : 0;
    int length = sceClibSnprintf(
        report, sizeof(report),
        "\nCAUSTIC CRASH: type=%u PC=0x%08X module+0x%08X LR=0x%08X SP=0x%08X FAR=0x%08X FSR=0x%08X SPSR=0x%08X\n"
        "r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X\n"
        "r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X\n",
        ctx->exceptionType, ctx->pc, relative_pc, ctx->lr, ctx->sp,
        ctx->FAR, ctx->FSR, ctx->SPSR,
        ctx->r0, ctx->r1, ctx->r2, ctx->r3, ctx->r4, ctx->r5, ctx->r6, ctx->r7,
        ctx->r8, ctx->r9, ctx->r10, ctx->r11, ctx->r12);
    if (length > 0) {
        SceUID fd = sceIoOpen(DATA_PATH "crash.log",
                              SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
        if (fd >= 0) {
            sceIoWrite(fd, report, (SceSize)length);
            sceIoClose(fd);
        }
    }
    sceKernelExitProcess(-1);
}

static void install_exception_handlers(void) {
    KuKernelExceptionHandler old_handler = NULL;
    int data_result = kuKernelRegisterExceptionHandler(
        KU_KERNEL_EXCEPTION_TYPE_DATA_ABORT, caustic_exception_handler,
        &old_handler, NULL);
    int prefetch_result = kuKernelRegisterExceptionHandler(
        KU_KERNEL_EXCEPTION_TYPE_PREFETCH_ABORT, caustic_exception_handler,
        &old_handler, NULL);
    int undefined_result = kuKernelRegisterExceptionHandler(
        KU_KERNEL_EXCEPTION_TYPE_UNDEFINED_INSTRUCTION, caustic_exception_handler,
        &old_handler, NULL);
    char line[160];
    snprintf(line, sizeof(line),
             "Caustic crash handlers: data=%d prefetch=%d undefined=%d",
             data_result, prefetch_result, undefined_result);
    debug_log(line);
}

#ifdef CAUSTIC_FULL_322
static void log_relocation_check(const char *stage) {
    uintptr_t got_value = *(volatile uintptr_t *)(so_mod.text_base + 0x0031c838);
    char line[176];
    snprintf(line, sizeof(line),
             "Caustic relocation check (%s): GOT[31c838]=0x%08lX expected=0x%08lX",
             stage, (unsigned long)got_value,
             (unsigned long)(so_mod.text_base + 0x00358508));
    debug_log(line);
}
#endif

void fatal_error(const char *fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    debug_log(line);
    sceKernelExitProcess(1);
}

typedef void (*jni_void_i)(JNIEnv *, jclass, jint);
typedef void (*jni_void_z)(JNIEnv *, jclass, jboolean);
typedef void (*jni_void_s)(JNIEnv *, jclass, jstring);
typedef void (*jni_void_v)(JNIEnv *, jclass);
typedef jint (*jni_int_v)(JNIEnv *, jclass);
typedef jint (*jni_int_ii)(JNIEnv *, jclass, jint, jint);
typedef jboolean (*jni_bool_v)(JNIEnv *, jclass);
typedef jint (*jni_render_v)(JNIEnv *, jclass);
typedef void (*jni_void_ii)(JNIEnv *, jclass, jint, jint);
typedef void (*jni_touch)(JNIEnv *, jclass, jint, jint, jint);
typedef void (*jni_audio)(JNIEnv *, jclass, jbyteArray);

typedef struct {
    jni_audio process;
    jni_audio record_mono;
    volatile int running;
    volatile int mic_running;
} audio_state;

static audio_state g_audio;

static so_hook zip_stat_hook;
static so_hook zip_open_hook;
static so_hook zip_fopen_hook;
static so_hook zip_fread_hook;
static so_hook texture_bind_hook;
static so_hook resource_lookup_hook;
static so_hook resource_fallback_hook;
static so_hook zip_stat_full_hook;
static void *pending_apk_archive;

#ifdef CAUSTIC_FULL_322
static void texture_bind_debug(unsigned texture) {
    char line[112];
    snprintf(line, sizeof(line), "Caustic texture helper: begin texture=%u", texture);
    debug_log(line);
    uintptr_t original = texture_bind_hook.addr;
    so_unhook(&texture_bind_hook);
    ((void (*)(unsigned))original)(texture);
    texture_bind_hook = hook_arm(original, (uintptr_t)texture_bind_debug);
    kuKernelFlushCaches((void *)original, sizeof(texture_bind_hook.patch_instr));
    debug_log("Caustic texture helper: returned");
}

static uintptr_t resource_lookup_debug(uintptr_t owner, uintptr_t name,
                                       uintptr_t pattern, uintptr_t output) {
    char line[224];
    snprintf(line, sizeof(line),
             "Caustic resource lookup: owner=0x%08lX name=0x%08lX pattern=0x%08lX out=0x%08lX",
             (unsigned long)owner, (unsigned long)name,
             (unsigned long)pattern, (unsigned long)output);
    debug_log(line);
    uintptr_t original = resource_lookup_hook.addr;
    so_unhook(&resource_lookup_hook);
    uintptr_t result = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))original)(
        owner, name, pattern, output);
    resource_lookup_hook = hook_arm(original, (uintptr_t)resource_lookup_debug);
    snprintf(line, sizeof(line), "Caustic resource lookup: returned=0x%08lX",
             (unsigned long)result);
    debug_log(line);
    return result;
}

static uintptr_t resource_fallback_debug(uintptr_t owner, uintptr_t name,
                                         uintptr_t pattern, uintptr_t output) {
    char line[224];
    snprintf(line, sizeof(line),
             "Caustic resource fallback: owner=0x%08lX name=0x%08lX pattern=0x%08lX out=0x%08lX",
             (unsigned long)owner, (unsigned long)name,
             (unsigned long)pattern, (unsigned long)output);
    debug_log(line);
    uintptr_t original = resource_fallback_hook.addr;
    so_unhook(&resource_fallback_hook);
    uintptr_t result = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))original)(
        owner, name, pattern, output);
    resource_fallback_hook = hook_arm(original, (uintptr_t)resource_fallback_debug);
    const char *built_path = (const char *)(owner + 0x262);
    snprintf(line, sizeof(line),
             "Caustic resource fallback: returned=0x%08lX path=%s",
             (unsigned long)result, built_path);
    debug_log(line);
    return result;
}

static int zip_stat_full_debug(uintptr_t archive, const char *name,
                               unsigned flags, uintptr_t stat_out) {
    char line[384];
    snprintf(line, sizeof(line),
             "Caustic full ZIP stat: archive=0x%08lX name=%s flags=0x%X",
             (unsigned long)archive, name ? name : "(null)", flags);
    debug_log(line);
    uintptr_t original = zip_stat_full_hook.addr;
    so_unhook(&zip_stat_full_hook);
    int result = ((int (*)(uintptr_t, const char *, unsigned, uintptr_t))original)(
        archive, name, flags, stat_out);
    zip_stat_full_hook = hook_arm(original, (uintptr_t)zip_stat_full_debug);
    snprintf(line, sizeof(line), "Caustic full ZIP stat: returned=%d", result);
    debug_log(line);
    return result;
}
#endif

static void *zip_open_debug(const char *path, int flags, int *errorp) {
    char line[384];
    snprintf(line, sizeof(line), "Caustic ZIP: open begin path=%s",
             (path && *path) ? path : "(empty)");
    debug_log(line);

    const char *actual_path = (path && *path) ? path : "app0:/caustic.apk";
    uintptr_t original = zip_open_hook.addr;
    so_unhook(&zip_open_hook);
    void *result = ((void *(*)(const char *, int, int *))original)(actual_path, flags, errorp);
    if (!result && strcmp(actual_path, "app0:/caustic.apk") != 0) {
        debug_log("Caustic ZIP: requested path failed; retrying app0:/caustic.apk");
        if (errorp) *errorp = 0;
        result = ((void *(*)(const char *, int, int *))original)("app0:/caustic.apk", flags, errorp);
    }

    if (result && strcmp(actual_path, "app0:/caustic.apk") == 0) {
        pending_apk_archive = result;
    } else if (!result && strcmp(actual_path, "app0:/caustic.apk") == 0 &&
               pending_apk_archive) {
        /* CreateMachines opens the APK while constructing UISkinManager, then
           InitGraphics redundantly opens it again. Vita's libc bridge rejects
           that concurrent second stream without setting a libzip error. The
           first archive is precisely the handle the skin reload needs. */
        result = pending_apk_archive;
        pending_apk_archive = NULL;
        debug_log("Caustic ZIP: reusing archive retained by CreateMachines");
    }
    zip_open_hook = hook_arm(original, (uintptr_t)zip_open_debug);
    kuKernelFlushCaches((void *)original, sizeof(zip_open_hook.patch_instr));

    snprintf(line, sizeof(line), "Caustic ZIP: open returned %p error=%d",
             result, errorp ? *errorp : 0);
    debug_log(line);
    return result;
}

static int zip_stat_debug(void *archive, const char *name, int flags, void *statbuf) {
    char line[384];
    snprintf(line, sizeof(line), "Caustic ZIP: stat begin archive=%p name=%s",
             archive, name ? name : "(null)");
    debug_log(line);
    uintptr_t original = zip_stat_hook.addr;
    so_unhook(&zip_stat_hook);
    int result = ((int (*)(void *, const char *, int, void *))original)(archive, name, flags, statbuf);
    zip_stat_hook = hook_arm(original, (uintptr_t)zip_stat_debug);
    kuKernelFlushCaches((void *)original, sizeof(zip_stat_hook.patch_instr));
    snprintf(line, sizeof(line), "Caustic ZIP: stat returned %d", result);
    debug_log(line);
    return result;
}

static void *zip_fopen_debug(void *archive, const char *name, int flags) {
    char line[384];
    snprintf(line, sizeof(line), "Caustic ZIP: fopen begin archive=%p name=%s",
             archive, name ? name : "(null)");
    debug_log(line);
    uintptr_t original = zip_fopen_hook.addr;
    so_unhook(&zip_fopen_hook);
    void *result = ((void *(*)(void *, const char *, int))original)(archive, name, flags);
    zip_fopen_hook = hook_arm(original, (uintptr_t)zip_fopen_debug);
    kuKernelFlushCaches((void *)original, sizeof(zip_fopen_hook.patch_instr));
    snprintf(line, sizeof(line), "Caustic ZIP: fopen returned %p", result);
    debug_log(line);
    return result;
}

static long long zip_fread_debug(void *file, void *buffer, unsigned long long length) {
    char line[192];
    snprintf(line, sizeof(line), "Caustic ZIP: fread begin file=%p bytes=%llu", file, length);
    debug_log(line);
    uintptr_t original = zip_fread_hook.addr;
    so_unhook(&zip_fread_hook);
    long long result = ((long long (*)(void *, void *, unsigned long long))original)(file, buffer, length);
    zip_fread_hook = hook_arm(original, (uintptr_t)zip_fread_debug);
    kuKernelFlushCaches((void *)original, sizeof(zip_fread_hook.patch_instr));
    snprintf(line, sizeof(line), "Caustic ZIP: fread returned %lld", result);
    debug_log(line);
    return result;
}

typedef struct {
    const char *name;
    uintptr_t offset;
} caustic_export;

static const caustic_export caustic_exports[] = {
#ifdef CAUSTIC_FULL_322
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetOSVersion", 0x0025cb20 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_CanUseOpenSL", 0x0025d1ec },
    { "Java_com_singlecellsoftware_caustic_CausticNative_HasMicrophone", 0x0025d344 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetMIDISupported", 0x0025cb9c },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetGetMoreSupported", 0x0025cb3c },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetBillingSupported", 0x0025cb50 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_HasStoragePermission", 0x0025cb64 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_HasRecordPermission", 0x0025cb80 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetAPKPath", 0x0025cd3c },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetRootPath", 0x0025ccd0 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetLaunchFile", 0x0025ccf4 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetIPAddress", 0x0025cd18 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetScreenSize", 0x0025cc10 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetLatency", 0x0025d1d8 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetProcessBufferSize", 0x0025d198 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetPrefs", 0x0025cbf8 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_IsUnlocked", 0x0025cc5c },
    { "Java_com_singlecellsoftware_caustic_CausticNative_CreateMachines", 0x0025ccb0 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_OnStart", 0x0025cb10 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_ConsumeReturnCode", 0x0025cc38 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeInitGraphics", 0x0025d0d0 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeResize", 0x0025d0e0 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeRender", 0x0025d188 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchBegin", 0x0025d0f8 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchWhile", 0x0025d114 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchEnd", 0x0025d130 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_ProcessAudioTrack", 0x0025d210 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_ProcessAudioRecordMono", 0x0025d358 },
#else
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetOSVersion", 0x001d3284 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_CanUseOpenSL", 0x001d368c },
    { "Java_com_singlecellsoftware_caustic_CausticNative_HasMicrophone", 0x001d3794 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetMIDISupported", 0x001d32a8 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetGetMoreSupported", 0x001d3298 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetAPKPath", 0x001d3390 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetRootPath", 0x001d3330 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetLaunchFile", 0x001d3350 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetIPAddress", 0x001d3370 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetScreenSize", 0x001d32e8 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetLatency", 0x001d3684 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetProcessBufferSize", 0x001d3650 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_SetPrefs", 0x001d32dc },
    { "Java_com_singlecellsoftware_caustic_CausticNative_IsUnlocked", 0x001d3310 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_CreateMachines", 0x001d3328 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_OnStart", 0x001d3280 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_ConsumeReturnCode", 0x001d3304 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeInitGraphics", 0x001d35f4 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeResize", 0x001d35f8 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeRender", 0x001d364c },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchBegin", 0x001d3604 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchWhile", 0x001d3614 },
    { "Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchEnd", 0x001d3624 },
    { "Java_com_singlecellsoftware_caustic_CausticNative_ProcessAudioTrack", 0x001d36a8 },
#endif
};

static void *required_symbol(const char *name) {
    for (unsigned i = 0; i < sizeof(caustic_exports) / sizeof(caustic_exports[0]); i++) {
        if (strcmp(caustic_exports[i].name, name) == 0)
            return (void *)(so_mod.text_base + caustic_exports[i].offset);
    }
    char line[256];
    snprintf(line, sizeof(line), "missing required symbol: %s", name);
    debug_log(line);
    sceKernelExitProcess(1);
    return NULL;
}

static int audio_thread(SceSize argc, void *argv) {
    (void)argc;
    (void)argv;
    /* Caustic mixes natively at 44.1 kHz.  The Vita BGM port accepts that
     * rate directly, avoiding both resampling coloration and the extra work
     * that can make a real-time audio port underrun during heavy rendering. */
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                   AUDIO_OUT_FRAMES, 44100,
                                   SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        debug_log("audio: sceAudioOutOpenPort failed");
        return port;
    }
    sceAudioOutSetAlcMode(SCE_AUDIO_ALC_OFF);

    jbyteArray array = jni->NewByteArray(&jni, AUDIO_IN_FRAMES * 4);
    jboolean copied = JNI_FALSE;
    int16_t *input = (int16_t *)jni->GetByteArrayElements(&jni, array, &copied);
    static int16_t output[AUDIO_OUT_FRAMES * 2] __attribute__((aligned(64)));
    int first_process = 1;
    int output_error_reported = 0;

    while (g_audio.running) {
        if (first_process)
            debug_log("Caustic audio: first ProcessAudioTrack begin");
        g_audio.process(&jni, NULL, array);
        if (first_process) {
            debug_log("Caustic audio: first ProcessAudioTrack returned");
            first_process = 0;
        }
        memcpy(output, input, sizeof(output));
        int result = sceAudioOutOutput(port, output);
        if (result < 0 && !output_error_reported) {
            debug_log("audio: sceAudioOutOutput failed");
            output_error_reported = 1;
        }
    }

    jni->ReleaseByteArrayElements(&jni, array, (jbyte *)input, JNI_ABORT);
    sceAudioOutReleasePort(port);
    return 0;
}

static int microphone_thread(SceSize argc, void *argv) {
    (void)argc;
    (void)argv;

    /* VitaSDK's validated built-in/headset microphone path is VOICE. RAW can
     * open successfully yet return silence on retail hardware, so retain it
     * only as a fallback. */
    int port = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_VOICE,
                                  MIC_INPUT_FRAMES, 48000,
                                  SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
    if (port < 0) {
        port = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_RAW,
                                  MIC_INPUT_FRAMES, 48000,
                                  SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
    }
    if (port < 0) {
        char line[96];
        snprintf(line, sizeof(line),
                 "microphone: sceAudioInOpenPort failed 0x%08X", port);
        debug_log(line);
        return port;
    }
    debug_log("Caustic microphone: 48 kHz mono input opened");
    {
        char line[96];
        int muted = sceAudioInGetStatus(SCE_AUDIO_IN_GETSTATUS_MUTE);
        snprintf(line, sizeof(line),
                 "Caustic microphone: initial mute status=0x%08X", muted);
        debug_log(line);
    }

    jbyteArray array = jni->NewByteArray(&jni, MIC_CAUSTIC_FRAMES * 2);
    jboolean copied = JNI_FALSE;
    int16_t *caustic_input = (int16_t *)
        jni->GetByteArrayElements(&jni, array, &copied);
    static int16_t vita_input[MIC_INPUT_FRAMES] __attribute__((aligned(64)));
    static int16_t resample_input[MIC_INPUT_FRAMES + 1];
    static int16_t queue[2048];
    unsigned queued = 0;
    uint32_t phase = 0;
    int have_previous = 0;
    int signal_logged = 0;
    unsigned diagnostic_buffers = 0;

    while (g_audio.mic_running) {
        int result = sceAudioInInput(port, vita_input);
        if (result < 0) {
            char line[96];
            snprintf(line, sizeof(line),
                     "microphone: sceAudioInInput failed 0x%08X", result);
            debug_log(line);
            break;
        }

        if (!signal_logged) {
            int peak = 0;
            for (unsigned i = 0; i < MIC_INPUT_FRAMES; ++i) {
                int sample = vita_input[i];
                int magnitude = sample < 0 ? -sample : sample;
                if (magnitude > peak) peak = magnitude;
            }
            if (peak > 32) {
                char line[96];
                snprintf(line, sizeof(line),
                         "Caustic microphone: first signal peak=%d", peak);
                debug_log(line);
                signal_logged = 1;
            }
            if (diagnostic_buffers < 8) {
                char line[96];
                snprintf(line, sizeof(line),
                         "Caustic microphone: input buffer %u peak=%d",
                         diagnostic_buffers, peak);
                debug_log(line);
                diagnostic_buffers++;
            }
        }

        /* Convert Vita's 48 kHz capture to Caustic's native 44.1 kHz rate.
         * The preceding sample and persistent phase keep the stream smooth
         * at every 768-frame input boundary. */
        if (!have_previous) {
            resample_input[0] = vita_input[0];
            have_previous = 1;
        }
        memcpy(resample_input + 1, vita_input, sizeof(vita_input));
        while (phase < MIC_INPUT_FRAMES * 44100u && queued < 2048) {
            unsigned source = phase / 44100u;
            unsigned fraction = phase % 44100u;
            int32_t a = resample_input[source];
            int32_t b = resample_input[source + 1];
            queue[queued++] = (int16_t)(
                a + ((int64_t)(b - a) * fraction) / 44100);
            phase += 48000u;
        }
        phase -= MIC_INPUT_FRAMES * 44100u;
        resample_input[0] = vita_input[MIC_INPUT_FRAMES - 1];

        while (queued >= MIC_CAUSTIC_FRAMES) {
            memcpy(caustic_input, queue,
                   MIC_CAUSTIC_FRAMES * sizeof(int16_t));
            memmove(queue, queue + MIC_CAUSTIC_FRAMES,
                    (queued - MIC_CAUSTIC_FRAMES) * sizeof(int16_t));
            queued -= MIC_CAUSTIC_FRAMES;
            g_audio.record_mono(&jni, NULL, array);
        }
    }

    jni->ReleaseByteArrayElements(&jni, array,
                                  (jbyte *)caustic_input, JNI_ABORT);
    sceAudioInReleasePort(port);
    return 0;
}

static void create_data_dirs(void) {
    sceIoMkdir("ux0:/data", 0777);
    sceIoMkdir(DATA_PATH, 0777);
    sceIoMkdir(DATA_PATH "caustic", 0777);
    sceIoMkdir(DATA_PATH "caustic/songs", 0777);
    sceIoMkdir(DATA_PATH "caustic/samples", 0777);
}

int main(void) {
    create_data_dirs();
    install_exception_handlers();
    debug_log("Caustic3 Vita: boot");

    int startup_demo_pending = 0;

#ifdef CAUSTIC_FULL_322
    extract_caustic_demo_assets("app0:/caustic.apk",
                                DATA_PATH "caustic/");
    ensure_caustic_config_line(
        DATA_PATH "caustic/config",
        "B WANT_FILL_LANDSCAPE ",
        "B WANT_FILL_LANDSCAPE TRUE\n");
#ifdef CAUSTIC_BUNDLED_EXTRAS
    /* The original Echo BMFont pair is not rendered correctly by the Vita
     * graphics path. Version 00.33 deliberately substitutes Caustic's known-
     * working stock machine font while retaining all other Echo artwork.
     * Remove Echo's scale overrides so stock font metrics are used. */
    sceIoRemove(DATA_PATH "caustic/skins/newskin/machinetext.cfg");
    install_caustic_extra_bundle(
        "app0:/extra/skins/newskin",
        DATA_PATH "caustic/skins/newskin",
        DATA_PATH ".extra_skin_newskin_v5.ready",
        "newskin stock machine-font compatibility test", 0);
    install_caustic_extra_bundle(
        "app0:/extra/presets",
        DATA_PATH "caustic/presets",
        DATA_PATH ".extra_presets_definitive_v1.ready",
        "Definitive preset pack", 1);
    install_caustic_extra_bundle(
        "app0:/extra/songs/demo",
        DATA_PATH "caustic/songs/demo",
        DATA_PATH ".extra_demo_vita_means_life_instructions_v4.ready",
        "Vita Means Life welcome project", 0);
    SceIoStat startup_marker_stat;
    startup_demo_pending =
        sceIoGetstat(DATA_PATH ".startup_vita_means_life_final_v1.ready",
                     &startup_marker_stat) < 0;
    if (startup_demo_pending) {
        /* Present the welcome project with Caustic's complete, known-good
         * original UI. This is one-shot; later user skin choices persist. */
        ensure_caustic_config_line(
            DATA_PATH "caustic/config", "S SKIN ", "S SKIN default\n");
    }
#endif
#endif

    int rc = so_file_load(&so_mod, SO_PATH, LOAD_ADDRESS);
    if (rc < 0) {
        debug_log("Caustic3 Vita: ELF load failed");
        sceKernelExitProcess(1);
    }
    debug_log("Caustic3 Vita: ELF mapped");
    so_relocate(&so_mod);
    debug_log("Caustic3 Vita: relocations applied");
#ifdef CAUSTIC_FULL_322
    log_relocation_check("relocate");
#endif
    resolve_imports(&so_mod);
    debug_log("Caustic3 Vita: imports resolved");
#ifdef CAUSTIC_FULL_322
    log_relocation_check("imports");
#endif

#ifndef CAUSTIC_FULL_322
    /* RenderGame contains Android's active frame limiter at RVA 0x1cce30
       (`bmi` back to a gettimeofday polling loop). It does not converge with
       the Vita timing environment and stalls before the first GL command.
       VitaGL's display queue already paces swaps, so fall through instead. */
    const uint32_t arm_nop = 0xE1A00000;
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001cce30),
                                  &arm_nop, sizeof(arm_nop));
    debug_log("Caustic3 Vita: Android frame limiter bypassed");

    /* One-run diagnostics for the lazy skin load performed by the first
       rendered frame. These are internal libzip functions in Caustic 3.3.2.0. */
    zip_stat_hook = hook_arm(so_mod.text_base + 0x0021d9f8,
                             (uintptr_t)zip_stat_debug);
    zip_open_hook = hook_arm(so_mod.text_base + 0x0021c410,
                             (uintptr_t)zip_open_debug);
    zip_fopen_hook = hook_arm(so_mod.text_base + 0x0021b560,
                              (uintptr_t)zip_fopen_debug);
    zip_fread_hook = hook_arm(so_mod.text_base + 0x0021b9a0,
                              (uintptr_t)zip_fread_debug);
    debug_log("Caustic3 Vita: ZIP diagnostics installed");
#else
    debug_log("Caustic3 Vita: full322 profile selected");
    const uint32_t arm_nop = 0xE1A00000;
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x00256f74),
                                  &arm_nop, sizeof(arm_nop));
    debug_log("Caustic3 Vita: full322 frame limiter bypassed");
    debug_log("Caustic3 Vita: full322 diagnostics idle");
#endif
    so_flush_caches(&so_mod);
    so_initialize(&so_mod);
    debug_log("Caustic3 Vita: constructors completed");
#ifdef CAUSTIC_FULL_322
    log_relocation_check("constructors");
#endif
    jni_init();
    debug_log("Caustic3 Vita: FalsoJNI initialized");
#ifdef CAUSTIC_FULL_322
    log_relocation_check("falsojni");
#endif

    jni_void_i set_os = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetOSVersion");
    jni_void_z can_opensl = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_CanUseOpenSL");
    jni_void_z has_mic = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_HasMicrophone");
    jni_void_z set_midi = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetMIDISupported");
    jni_void_z set_more = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetGetMoreSupported");
#ifdef CAUSTIC_FULL_322
    jni_void_z set_billing = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetBillingSupported");
    jni_void_z has_storage = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_HasStoragePermission");
    jni_void_z has_record = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_HasRecordPermission");
#endif
    jni_void_s set_apk = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetAPKPath");
    jni_void_s set_root = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetRootPath");
    jni_void_s set_launch = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetLaunchFile");
    jni_void_s set_ip = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetIPAddress");
    jni_void_i set_screen = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetScreenSize");
    jni_void_i set_latency = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetLatency");
    jni_void_i set_buffer = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetProcessBufferSize");
    jni_int_ii set_prefs = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_SetPrefs");
    jni_bool_v is_unlocked = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_IsUnlocked");
    jni_void_v create_machines = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_CreateMachines");
    jni_void_v on_start = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_OnStart");
    jni_int_v consume_code = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_ConsumeReturnCode");
    jni_void_v init_graphics = required_symbol("Java_com_singlecellsoftware_caustic_CausticRenderer_nativeInitGraphics");
    jni_void_ii resize = required_symbol("Java_com_singlecellsoftware_caustic_CausticRenderer_nativeResize");
    jni_render_v render = required_symbol("Java_com_singlecellsoftware_caustic_CausticRenderer_nativeRender");
    jni_touch touch_begin = required_symbol("Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchBegin");
    jni_touch touch_move = required_symbol("Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchWhile");
    jni_touch touch_end = required_symbol("Java_com_singlecellsoftware_caustic_CausticRenderer_nativeTouchEnd");
    g_audio.process = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_ProcessAudioTrack");
#ifdef CAUSTIC_FULL_322
    g_audio.record_mono = required_symbol("Java_com_singlecellsoftware_caustic_CausticNative_ProcessAudioRecordMono");
#endif

    debug_log("Caustic lifecycle: SetOSVersion begin");
    set_os(&jni, NULL, 24);
    debug_log("Caustic lifecycle: SetOSVersion returned");
    can_opensl(&jni, NULL, JNI_FALSE);
    debug_log("Caustic lifecycle: CanUseOpenSL returned");
    has_mic(&jni, NULL, JNI_TRUE);
    debug_log("Caustic lifecycle: HasMicrophone returned");
    set_midi(&jni, NULL, JNI_FALSE);
    debug_log("Caustic lifecycle: SetMIDISupported returned");
    set_more(&jni, NULL, JNI_FALSE);
    debug_log("Caustic lifecycle: SetGetMoreSupported returned");
#ifdef CAUSTIC_FULL_322
    set_billing(&jni, NULL, JNI_FALSE);
    debug_log("Caustic lifecycle: SetBillingSupported returned");
    has_storage(&jni, NULL, JNI_TRUE);
    debug_log("Caustic lifecycle: HasStoragePermission returned");
    has_record(&jni, NULL, JNI_TRUE);
    debug_log("Caustic lifecycle: HasRecordPermission returned");
#endif
    set_apk(&jni, NULL, jni->NewStringUTF(&jni, "app0:/caustic.apk"));
    debug_log("Caustic lifecycle: SetAPKPath returned");
    set_root(&jni, NULL, jni->NewStringUTF(&jni, DATA_PATH));
    debug_log("Caustic lifecycle: SetRootPath returned");
    if (startup_demo_pending) {
        set_launch(
            &jni, NULL,
            jni->NewStringUTF(
                &jni,
                DATA_PATH "caustic/songs/demo/Vita Means Life.caustic"));
        debug_log("Caustic lifecycle: SetLaunchFile returned");
    }
    set_ip(&jni, NULL, jni->NewStringUTF(&jni, "0.0.0.0"));
    debug_log("Caustic lifecycle: SetIPAddress returned");
    set_prefs(&jni, NULL, 2, 0x43535443); /* stable Vita device identity */
    debug_log("Caustic lifecycle: SetPrefs identity returned");
#ifndef CAUSTIC_FULL_322
    set_prefs(&jni, NULL, 3, 0);          /* no Android unlock token */
#endif
    (void)is_unlocked(&jni, NULL);
    debug_log("Caustic lifecycle: IsUnlocked returned");

    SceIoStat apk_stat;
    memset(&apk_stat, 0, sizeof(apk_stat));
    int apk_stat_result = sceIoGetstat("app0:/caustic.apk", &apk_stat);
    char apk_line[192];
    snprintf(apk_line, sizeof(apk_line),
             "Caustic APK: stat=%d size=%llu",
             apk_stat_result, (unsigned long long)apk_stat.st_size);
    debug_log(apk_line);

    set_screen(&jni, NULL, 5000);
    create_machines(&jni, NULL);
    debug_log("Caustic3 Vita: machines created");
    if (startup_demo_pending) {
        install_caustic_extra_bundle(
            "app0:/extra/songs/demo",
            DATA_PATH "caustic/songs/demo",
            DATA_PATH ".startup_vita_means_life_final_v1.ready",
            "first-launch Vita Means Life welcome", 1);
        debug_log("Caustic startup: welcome project loaded");
    }

    /* Varying used between Caustic's built-in GLES2 shader stages. */
    vglAddSemanticBinding("vTexCoord", 0, VGL_TYPE_TEXCOORD);
    vglInitExtended(0, WIDTH, HEIGHT, 8 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    /* The Android limiter is bypassed above because its busy wait is not a
     * good fit for the Vita. Explicit VBlank pacing prevents UI transitions
     * from presenting several intermediate rack/menu frames in one refresh. */
    vglWaitVblankStart(GL_TRUE);
    debug_log("Caustic3 Vita: vitaGL initialized");
    debug_log("Caustic3 Vita: nativeInitGraphics begin");
    init_graphics(&jni, NULL);
    debug_log("Caustic3 Vita: nativeInitGraphics returned");
    resize(&jni, NULL, WIDTH, HEIGHT);
    debug_log("Caustic3 Vita: renderer initialized");

    set_latency(&jni, NULL, AUDIO_IN_FRAMES);
    set_buffer(&jni, NULL, AUDIO_IN_FRAMES);
    g_audio.running = 1;
    SceUID audio_uid = sceKernelCreateThread("caustic_audio", audio_thread,
                                             0x10000100 - 10,
                                             256 * 1024, 0, 0, NULL);
    if (audio_uid >= 0)
        sceKernelStartThread(audio_uid, 0, NULL);
    else
        debug_log("Caustic3 Vita: audio thread creation failed");
#ifdef CAUSTIC_FULL_322
    g_audio.mic_running = 1;
    SceUID mic_uid = sceKernelCreateThread("caustic_microphone",
                                           microphone_thread,
                                           0x10000100 - 9,
                                           256 * 1024, 0, 0, NULL);
    if (mic_uid >= 0)
        sceKernelStartThread(mic_uid, 0, NULL);
    else
        debug_log("Caustic3 Vita: microphone thread creation failed");
#endif
    on_start(&jni, NULL);

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    SceTouchData touch = {0};
    struct {
        int active;
        int seen;
        uint8_t hardware_id;
        int x;
        int y;
    } physical_touches[SCE_TOUCH_MAX_REPORT] = {0};
    int physical_touch_count = 0;
    SceCtrlData pad = {0};
    uint32_t previous_buttons = 0;
    unsigned direction_hold[4] = {0, 0, 0, 0};
    int focus_x = WIDTH / 2;
    int focus_y = HEIGHT / 2;
    int drag_x = focus_x;
    int drag_y = focus_y;
    int pad_grabbed = 0;
    int pad_tap_pending_release = 0;
    int rack_swipe_active = 0;
    int rack_swipe_y = RACK_SWIPE_CENTER_Y;
    int controls_enabled = 0;
    int transport_playing = 0;
    debug_log("Caustic3 Vita: entering render loop");

    int first_frame = 1;
    for (;;) {
        /* vitaGL performs its VBlank wait in the asynchronous display queue
         * callback. Pace the producer thread too, otherwise it can render and
         * queue several rack-transition states before the first is visible. */
        sceDisplayWaitVblankStart();

        for (unsigned slot = 0; slot < SCE_TOUCH_MAX_REPORT; ++slot)
            physical_touches[slot].seen = 0;

        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        unsigned reports = touch.reportNum;
        if (reports > SCE_TOUCH_MAX_REPORT)
            reports = SCE_TOUCH_MAX_REPORT;
        for (unsigned report = 0; report < reports; ++report) {
            SceTouchReport *current = &touch.report[report];
            int slot = -1;
            for (unsigned candidate = 0;
                 candidate < SCE_TOUCH_MAX_REPORT; ++candidate) {
                if (physical_touches[candidate].active &&
                    physical_touches[candidate].hardware_id == current->id) {
                    slot = (int)candidate;
                    break;
                }
            }
            if (slot < 0) {
                for (unsigned candidate = 0;
                     candidate < SCE_TOUCH_MAX_REPORT; ++candidate) {
                    if (!physical_touches[candidate].active) {
                        slot = (int)candidate;
                        break;
                    }
                }
            }
            if (slot < 0)
                continue;

            int x = current->x * WIDTH / 1919;
            int y = current->y * HEIGHT / 1087;
            physical_touches[slot].seen = 1;
            physical_touches[slot].x = x;
            physical_touches[slot].y = y;
            if (!physical_touches[slot].active) {
                physical_touches[slot].active = 1;
                physical_touches[slot].hardware_id = current->id;
                physical_touch_count++;
                touch_begin(&jni, NULL, slot, x, y);
            } else {
                touch_move(&jni, NULL, slot, x, y);
            }
        }
        for (unsigned slot = 0; slot < SCE_TOUCH_MAX_REPORT; ++slot) {
            if (physical_touches[slot].active &&
                !physical_touches[slot].seen) {
                touch_end(&jni, NULL, slot,
                          physical_touches[slot].x,
                          physical_touches[slot].y);
                physical_touches[slot].active = 0;
                physical_touch_count--;
            }
        }

        /* A one-frame touch is long enough for Caustic to recognize transport
         * and navigation buttons while retaining normal button responsiveness. */
        if (pad_tap_pending_release) {
            touch_end(&jni, NULL, PAD_POINTER_ID, drag_x, drag_y);
            pad_tap_pending_release = 0;
        }
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t buttons = pad.buttons;
        uint32_t pressed = buttons & ~previous_buttons;
        previous_buttons = buttons;

        if (pressed & SCE_CTRL_TRIANGLE) {
            controls_enabled = !controls_enabled;
            if (!controls_enabled) {
                if (pad_grabbed) {
                    touch_end(&jni, NULL, PAD_POINTER_ID, drag_x, drag_y);
                    pad_grabbed = 0;
                }
                if (rack_swipe_active) {
                    touch_end(&jni, NULL, RACK_POINTER_ID,
                              RACK_SWIPE_X, rack_swipe_y);
                    rack_swipe_active = 0;
                }
                memset(direction_hold, 0, sizeof(direction_hold));
            }
        }

        if (controls_enabled) {
            /* The left stick performs a real vertical swipe on the rack's
             * non-interactive left rail. Re-anchor before reaching an edge
             * so a held stick can continue across multiple machines. */
            int rack_delta = 0;
            if (pad.ly < 80)
                rack_delta = -2 - (80 - pad.ly) / 24;
            else if (pad.ly > 176)
                rack_delta = 2 + (pad.ly - 176) / 24;

            if (rack_delta != 0 && physical_touch_count == 0 && !pad_grabbed) {
                if (!rack_swipe_active) {
                    rack_swipe_y = RACK_SWIPE_CENTER_Y;
                    touch_begin(&jni, NULL, RACK_POINTER_ID,
                                RACK_SWIPE_X, rack_swipe_y);
                    rack_swipe_active = 1;
                }
                int next_rack_y = rack_swipe_y + rack_delta;
                if (next_rack_y < RACK_SWIPE_MIN_Y ||
                    next_rack_y > RACK_SWIPE_MAX_Y) {
                    touch_end(&jni, NULL, RACK_POINTER_ID,
                              RACK_SWIPE_X, rack_swipe_y);
                    rack_swipe_y = RACK_SWIPE_CENTER_Y;
                    touch_begin(&jni, NULL, RACK_POINTER_ID,
                                RACK_SWIPE_X, rack_swipe_y);
                }
                rack_swipe_y = clamp_int(rack_swipe_y + rack_delta,
                                         RACK_SWIPE_MIN_Y,
                                         RACK_SWIPE_MAX_Y);
                touch_move(&jni, NULL, RACK_POINTER_ID,
                           RACK_SWIPE_X, rack_swipe_y);
            } else if (rack_swipe_active) {
                touch_end(&jni, NULL, RACK_POINTER_ID,
                          RACK_SWIPE_X, rack_swipe_y);
                rack_swipe_active = 0;
            }

        if (pressed & (SCE_CTRL_START | SCE_CTRL_SELECT)) {
            if (pad_grabbed) {
                touch_end(&jni, NULL, PAD_POINTER_ID, drag_x, drag_y);
                pad_grabbed = 0;
            }
            if (pressed & SCE_CTRL_START) {
                drag_x = transport_playing ? TRANSPORT_STOP_X : TRANSPORT_PLAY_X;
                drag_y = TRANSPORT_Y;
                touch_begin(&jni, NULL, PAD_POINTER_ID, drag_x, drag_y);
                pad_tap_pending_release = 1;
                transport_playing = !transport_playing;
            } else {
                drag_x = MACHINE_MANAGEMENT_X;
                drag_y = MACHINE_MANAGEMENT_Y;
                touch_begin(&jni, NULL, PAD_POINTER_ID, drag_x, drag_y);
                pad_tap_pending_release = 1;
                /* Start focus near the first machine slot on the management
                 * screen so it is immediately usable with the D-pad. */
                focus_x = 145;
                focus_y = 145;
            }
        } else {
            if (pressed & SCE_CTRL_CROSS) {
                if (pad_grabbed) {
                    touch_end(&jni, NULL, PAD_POINTER_ID, drag_x, drag_y);
                    pad_grabbed = 0;
                } else if (!pad_tap_pending_release) {
                    drag_x = focus_x;
                    drag_y = focus_y;
                    touch_begin(&jni, NULL, PAD_POINTER_ID, drag_x, drag_y);
                    pad_grabbed = 1;
                }
            }

            static const uint32_t direction_buttons[4] = {
                SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT
            };
            static const int direction_x[4] = {0, 0, -1, 1};
            static const int direction_y[4] = {-1, 1, 0, 0};
            int drag_moved = 0;
            for (unsigned direction = 0; direction < 4; ++direction) {
                if (buttons & direction_buttons[direction])
                    direction_hold[direction]++;
                else
                    direction_hold[direction] = 0;

                if (pad_grabbed && (buttons & direction_buttons[direction])) {
                    drag_x = clamp_int(drag_x + direction_x[direction] * 3,
                                       1, WIDTH - 2);
                    drag_y = clamp_int(drag_y + direction_y[direction] * 3,
                                       1, HEIGHT - 2);
                    drag_moved = 1;
                } else if (!pad_grabbed) {
                    unsigned held = direction_hold[direction];
                    int navigate = (pressed & direction_buttons[direction]) ||
                        (held > 12 && ((held - 12) % 4) == 0);
                    if (navigate) {
                        focus_x = clamp_int(focus_x + direction_x[direction] * 24,
                                            1, WIDTH - 2);
                        focus_y = clamp_int(focus_y + direction_y[direction] * 24,
                                            1, HEIGHT - 2);
                    }
                }
            }
            if (pad_grabbed && drag_moved)
                touch_move(&jni, NULL, PAD_POINTER_ID, drag_x, drag_y);
        }
        }

        int event;
        while ((event = consume_code(&jni, NULL)) != 0) {
            char line[96];
            snprintf(line, sizeof(line), "Caustic event: 0x%08X", event);
            debug_log(line);
        }
        if (first_frame) {
            debug_log("Caustic3 Vita: first render begin");
#ifdef CAUSTIC_FULL_322
            uintptr_t render_owner = *(volatile uintptr_t *)(so_mod.text_base + 0x0035a7cc);
            uintptr_t render_object = render_owner ? *(volatile uintptr_t *)render_owner : 0;
            uintptr_t render_vtable = render_object ? *(volatile uintptr_t *)render_object : 0;
            uintptr_t texture_manager = *(volatile uintptr_t *)(so_mod.text_base + 0x00358494);
            char pointer_line[160];
            snprintf(pointer_line, sizeof(pointer_line),
                     "Caustic texture manager pointer: 0x%08lX",
                     (unsigned long)texture_manager);
            debug_log(pointer_line);
            uintptr_t texture_root =
                (texture_manager >= 0x81000000 && texture_manager < LOAD_ADDRESS)
                    ? *(volatile uintptr_t *)texture_manager : 0;
            uintptr_t first_texture =
                (texture_root >= 0x81000000 && texture_root < LOAD_ADDRESS)
                    ? *(volatile uintptr_t *)(texture_root + 4) : 0;
            char state_line[256];
            snprintf(state_line, sizeof(state_line),
                     "Caustic render state: owner=0x%08lX object=0x%08lX vtable=0x%08lX texmgr=0x%08lX root=0x%08lX texture=0x%08lX",
                     (unsigned long)render_owner, (unsigned long)render_object,
                     (unsigned long)render_vtable, (unsigned long)texture_manager,
                     (unsigned long)texture_root, (unsigned long)first_texture);
            debug_log(state_line);
#endif
        }
        render(&jni, NULL);
        if (first_frame)
            debug_log("Caustic3 Vita: first render returned");
        if (controls_enabled)
            draw_controller_focus(pad_grabbed ? drag_x : focus_x,
                                  pad_grabbed ? drag_y : focus_y,
                                  pad_grabbed);
        vglSwapBuffers(GL_FALSE);
        if (first_frame) {
            debug_log("Caustic3 Vita: first buffer swap returned");
            first_frame = 0;
        }
    }
}
