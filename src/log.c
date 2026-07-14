#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <string.h>

static void ensure_dirs(void) {
    sceIoMkdir("ux0:/data", 0777);
    sceIoMkdir("ux0:/data/CAUSTIC3", 0777);
}

void debug_log(const char *msg) {
    ensure_dirs();
    int fd = sceIoOpen("ux0:/data/CAUSTIC3/debug.log",
                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (int)strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}