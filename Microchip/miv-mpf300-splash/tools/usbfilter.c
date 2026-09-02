/* usbfilter.c
 *
 * LD_PRELOAD shim that hides USB devices from directory enumeration so
 * Microchip's FlashPro host stack only sees the FlashPro programmer.
 *
 * Why: libfpcomm (shared by SoftConsole's fpServer and Libero/FlashPro
 * Express) corrupts memory during FlashPro port enumeration when the host
 * exposes many FTDI-family USB channels, then segfaults at a varying
 * downstream point (vector push_back in EnumeratePorts, or a NULL handle
 * in FpcommFp6Programmer::FlushUsbBuffer). Hiding every USB device except
 * the FlashPro and its parent hub keeps the enumeration small and the
 * stack stable.
 *
 * How: the bundled D2XX and libusb copies inside fpServer/libfpcomm scan
 * /sys/bus/usb/devices and /dev/bus/usb with opendir/readdir. Their FT_*
 * symbols bind inside the executable and cannot be interposed, but their
 * libc calls resolve through the PLT, so an LD_PRELOAD of opendir/readdir
 * filters what they can discover.
 *
 * Build: gcc -shared -fPIC -O2 -o usbfilter.so usbfilter.c -ldl -lpthread
 *
 * Env (set by fp5-jtag.sh; unset variables mean "no filtering"):
 *   USBFILTER_SYS  comma list of /sys/bus/usb/devices entries to allow
 *                  (entry allowed on exact match, or for its interface
 *                  children: a list item followed by ':'). "usbN" root
 *                  hubs always pass; list hub children explicitly.
 *   USBFILTER_DEV  comma list of device file names allowed inside
 *                  /dev/bus/usb/<bus> dirs (e.g. "001,026,074").
 *   USBFILTER_BUS  comma list of bus dirs allowed in /dev/bus/usb ("001").
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>

static DIR *(*real_opendir)(const char *);
static struct dirent *(*real_readdir)(DIR *);
static struct dirent64 *(*real_readdir64)(DIR *);
static int (*real_closedir)(DIR *);

#define MAXTRACK 128
static struct { DIR *d; int kind; } track[MAXTRACK];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;

/* POSIX dlsym-to-function-pointer pattern: assign through a void** to
 * avoid the undefined object-to-function pointer conversion */
static void init_real_once(void)
{
    *(void **)(&real_opendir)   = dlsym(RTLD_NEXT, "opendir");
    *(void **)(&real_readdir)   = dlsym(RTLD_NEXT, "readdir");
    *(void **)(&real_readdir64) = dlsym(RTLD_NEXT, "readdir64");
    *(void **)(&real_closedir)  = dlsym(RTLD_NEXT, "closedir");
    if (real_opendir == NULL || real_readdir == NULL ||
        real_readdir64 == NULL || real_closedir == NULL) {
        fprintf(stderr, "usbfilter: dlsym(RTLD_NEXT) failed\n");
        abort();
    }
}

static void init_real(void)
{
    pthread_once(&init_once, init_real_once);
}

static int in_list(const char *env, const char *name, int prefix_ok)
{
    const char *p = getenv(env);
    size_t nl = strlen(name);
    if (p == NULL) {
        return 1; /* no filter configured */
    }
    while (*p != '\0') {
        const char *c = strchr(p, ',');
        size_t l = (c != NULL) ? (size_t)(c - p) : strlen(p);
        if (l == nl && strncmp(p, name, l) == 0) {
            return 1;
        }
        if (prefix_ok && nl > l && strncmp(p, name, l) == 0 &&
            name[l] == ':') {
            return 1;
        }
        if (c == NULL) {
            break;
        }
        p = c + 1;
    }
    return 0;
}

/* 0 = not a filtered dir, 1 = sysfs devices, 2 = /dev/bus/usb/<bus>,
 * 3 = /dev/bus/usb */
static int classify(const char *path)
{
    char buf[512];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, path, n + 1);
    while (n > 1 && buf[n - 1] == '/') {
        buf[--n] = '\0';
    }
    if (strcmp(buf, "/sys/bus/usb/devices") == 0) {
        return 1;
    }
    if (strcmp(buf, "/dev/bus/usb") == 0) {
        return 3;
    }
    if (strncmp(buf, "/dev/bus/usb/", 13) == 0 &&
        strchr(buf + 13, '/') == NULL) {
        return 2;
    }
    return 0;
}

static int allowed(int kind, const char *name)
{
    if (name[0] == '.') {
        return 1;
    }
    switch (kind) {
    case 1:
        if (strncmp(name, "usb", 3) == 0) {
            return 1;
        }
        return in_list("USBFILTER_SYS", name, 1);
    case 2:
        return in_list("USBFILTER_DEV", name, 0);
    case 3:
        return in_list("USBFILTER_BUS", name, 0);
    }
    return 1;
}

DIR *opendir(const char *path)
{
    DIR *d;
    int kind, i;
    init_real();
    d = real_opendir(path);
    if (d != NULL && (kind = classify(path)) != 0) {
        pthread_mutex_lock(&lock);
        for (i = 0; i < MAXTRACK; i++) {
            if (track[i].d == NULL) {
                track[i].d = d;
                track[i].kind = kind;
                break;
            }
        }
        /* a full table means this DIR* is enumerated unfiltered; warn so
         * the silent loss of filtering is diagnosable */
        if (i == MAXTRACK) {
            fprintf(stderr, "usbfilter: track table full, %s unfiltered\n",
                    path);
        }
        pthread_mutex_unlock(&lock);
    }
    return d;
}

static int kind_of(DIR *d)
{
    int i, k = 0;
    pthread_mutex_lock(&lock);
    for (i = 0; i < MAXTRACK; i++) {
        if (track[i].d == d) {
            k = track[i].kind;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return k;
}

struct dirent *readdir(DIR *d)
{
    struct dirent *e;
    int kind;
    init_real();
    kind = kind_of(d);
    while ((e = real_readdir(d)) != NULL) {
        if (kind == 0 || allowed(kind, e->d_name)) {
            return e;
        }
    }
    return NULL;
}

struct dirent64 *readdir64(DIR *d)
{
    struct dirent64 *e;
    int kind;
    init_real();
    kind = kind_of(d);
    while ((e = real_readdir64(d)) != NULL) {
        if (kind == 0 || allowed(kind, e->d_name)) {
            return e;
        }
    }
    return NULL;
}

int closedir(DIR *d)
{
    int i;
    init_real();
    pthread_mutex_lock(&lock);
    for (i = 0; i < MAXTRACK; i++) {
        if (track[i].d == d) {
            track[i].d = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return real_closedir(d);
}
