/*
 * USB host (OTG) support for USB Mode.pak: device enumeration + storage auto-mount.
 * No SDL / UI code in here so it can also be driven from the command line (usbmode.elf --host-test).
 *
 * Hardware note: the Type-C port does NOT supply 5V (no VBUS control in the device tree), so a
 * self-powered hub is required.  The stock rootfs hotplug script also mounts the first stick rw at
 * /mnt/exUDISK (outside our chroot, same global mount table), so we always mount rw to share the
 * superblock (a ro mount of an already rw-mounted device fails with EBUSY).
 */
#ifndef USBHOST_H
#define USBHOST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>

#define UH_MNT_ROOT   "/mnt/SDCARD/USB"     /* visible both inside the chroot and from the stock side */
#define UH_MAX_DEV    12
#define UH_MAX_MNT    8
#define UH_SYS_USB    "/sys/bus/usb/devices"

enum { UC_OTHER = 0, UC_STORAGE, UC_HUB, UC_HID, UC_NET, UC_AUDIO, UC_INTERNAL };

typedef struct {
    char path[24];        /* e.g. "3-1.1" */
    char vidpid[12];      /* "0781:5591" */
    char name[72];        /* manufacturer + product */
    char speed[8];        /* Mbps */
    int  cls;             /* UC_* */
    char disk[8];         /* "sda" if storage */
} UhDev;

typedef struct {
    int  active;
    char dev[16];         /* "sda1" */
    char disk[8];         /* "sda" */
    char mnt[64];         /* mount point */
    char fs[12];          /* vfat / exfat / ntfs */
    char err[64];         /* last failure */
    int  mounted;
    int  tries;
    unsigned long long mb; /* size in MB */
    unsigned first_ms;    /* when first seen */
    unsigned last_try_ms;
} UhMnt;

static UhDev  uh_devs[UH_MAX_DEV];
static int    uh_ndev = 0;
static UhMnt  uh_mnts[UH_MAX_MNT];

/* ---------- small helpers ---------- */
static int uh_read_str(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) { out[0] = 0; return 0; }
    if (!fgets(out, (int)n, f)) out[0] = 0;
    fclose(f);
    size_t l = strlen(out);
    while (l && (out[l-1] == '\n' || out[l-1] == '\r' || out[l-1] == ' ')) out[--l] = 0;
    return out[0] != 0;
}

static int uh_is_usb_devname(const char *nm) {   /* "3-1", "3-1.2", not "usb3", not "3-1:1.0" */
    if (!(nm[0] >= '0' && nm[0] <= '9') || nm[1] != '-') return 0;
    for (const char *p = nm; *p; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return 0;
    return 1;
}

/* does /sys/block/<disk> belong to usb device <path>?  (match "/<path>:" = its interface dir) */
static int uh_disk_belongs(const char *disk, const char *path) {
    char link[PATH_MAX], real[PATH_MAX], key[40];
    snprintf(link, sizeof(link), "/sys/block/%s", disk);
    if (!realpath(link, real)) return 0;
    snprintf(key, sizeof(key), "/%s:", path);
    return strstr(real, key) != NULL;
}

static int uh_classify(const char *path) {
    char p[96], v[16];
    int cls = UC_OTHER;
    snprintf(p, sizeof(p), UH_SYS_USB "/%s:1.0/bInterfaceClass", path);
    if (!uh_read_str(p, v, sizeof(v))) {
        snprintf(p, sizeof(p), UH_SYS_USB "/%s/bDeviceClass", path);
        uh_read_str(p, v, sizeof(v));
    }
    unsigned c = (unsigned)strtoul(v, NULL, 16);
    switch (c) {
        case 0x08: cls = UC_STORAGE; break;
        case 0x09: cls = UC_HUB; break;
        case 0x03: cls = UC_HID; break;
        case 0x02: case 0x0a: case 0xe0: cls = UC_NET; break;
        case 0x01: cls = UC_AUDIO; break;
        default: cls = UC_OTHER;
    }
    return cls;
}

/* known vendor-class network adapters (class 0xff) */
static int uh_vendor_net(const char *vidpid) {
    return !strcmp(vidpid, "0bda:8152") || !strcmp(vidpid, "0bda:8153") || !strcmp(vidpid, "0bda:8156") ||
           !strcmp(vidpid, "0b95:1790") || !strcmp(vidpid, "0b95:772b") || !strcmp(vidpid, "2357:0601");
}

/* ---------- enumeration ---------- */
static void uh_enumerate(void) {
    DIR *d = opendir(UH_SYS_USB);
    struct dirent *e;
    uh_ndev = 0;
    if (!d) return;
    /* collect block disks once */
    char disks[8][8]; int ndisks = 0;
    DIR *bd = opendir("/sys/block");
    if (bd) {
        struct dirent *be;
        while ((be = readdir(bd)) && ndisks < 8)
            if (be->d_name[0] == 's' && be->d_name[1] == 'd' && be->d_name[2] >= 'a' && be->d_name[2] <= 'z' && be->d_name[3] == 0)
                snprintf(disks[ndisks++], 8, "%s", be->d_name);
        closedir(bd);
    }
    while ((e = readdir(d)) && uh_ndev < UH_MAX_DEV) {
        if (!uh_is_usb_devname(e->d_name)) continue;
        UhDev *u = &uh_devs[uh_ndev];
        char p[96], mf[40], pr[48], vid[8], pid[8];
        memset(u, 0, sizeof(*u));
        snprintf(u->path, sizeof(u->path), "%s", e->d_name);
        snprintf(p, sizeof(p), UH_SYS_USB "/%s/idVendor", e->d_name);  uh_read_str(p, vid, sizeof(vid));
        snprintf(p, sizeof(p), UH_SYS_USB "/%s/idProduct", e->d_name); uh_read_str(p, pid, sizeof(pid));
        snprintf(u->vidpid, sizeof(u->vidpid), "%s:%s", vid, pid);
        snprintf(p, sizeof(p), UH_SYS_USB "/%s/manufacturer", e->d_name); uh_read_str(p, mf, sizeof(mf));
        snprintf(p, sizeof(p), UH_SYS_USB "/%s/product", e->d_name);      uh_read_str(p, pr, sizeof(pr));
        snprintf(u->name, sizeof(u->name), "%s%s%s", mf, (mf[0] && pr[0]) ? " " : "", pr[0] ? pr : (mf[0] ? "" : u->vidpid));
        snprintf(p, sizeof(p), UH_SYS_USB "/%s/speed", e->d_name);        uh_read_str(p, u->speed, sizeof(u->speed));
        u->cls = uh_classify(e->d_name);
        if (u->cls == UC_OTHER && uh_vendor_net(u->vidpid)) u->cls = UC_NET;
        /* devices on the second (internal) host port (ehci1/ohci1) = built-in gamepad: not on the OTG port */
        {
            char lp[96], rp[PATH_MAX];
            snprintf(lp, sizeof(lp), UH_SYS_USB "/%s", e->d_name);
            if (realpath(lp, rp) && (strstr(rp, "ehci1-controller") || strstr(rp, "ohci1-controller")))
                u->cls = (u->cls == UC_HUB) ? UC_HUB : UC_INTERNAL;
        }
        if (u->cls == UC_STORAGE)
            for (int i = 0; i < ndisks; i++)
                if (uh_disk_belongs(disks[i], u->path)) { snprintf(u->disk, sizeof(u->disk), "%s", disks[i]); break; }
        uh_ndev++;
    }
    closedir(d);
    /* stable order: by path */
    for (int i = 0; i < uh_ndev; i++)
        for (int j = i + 1; j < uh_ndev; j++)
            if (strcmp(uh_devs[i].path, uh_devs[j].path) > 0) { UhDev t = uh_devs[i]; uh_devs[i] = uh_devs[j]; uh_devs[j] = t; }
}

static const char *uh_cls_name(int c) {
    switch (c) {
        case UC_STORAGE: return "Storage";
        case UC_HUB: return "Hub";
        case UC_HID: return "Input";
        case UC_NET: return "Network";
        case UC_AUDIO: return "Audio";
        case UC_INTERNAL: return "Internal";
        default: return "Other";
    }
}

/* ---------- filesystem probing / mounting ---------- */
static const char *uh_probe_fs(const char *dev, char *why, size_t whyn) {
    unsigned char b[2048];
    int fd = open(dev, O_RDONLY);
    why[0] = 0;
    if (fd < 0) { snprintf(why, whyn, "open: %s", strerror(errno)); return NULL; }
    ssize_t n = read(fd, b, sizeof(b));
    close(fd);
    if (n < 1100) { snprintf(why, whyn, "read: %s", n < 0 ? strerror(errno) : "short"); return NULL; }
    if (!memcmp(b + 3, "EXFAT   ", 8)) return "exfat";
    if (!memcmp(b + 3, "NTFS    ", 8)) return "ntfs";
    if (!memcmp(b + 0x52, "FAT32   ", 8) || !memcmp(b + 0x36, "FAT", 3) || (b[510] == 0x55 && b[511] == 0xaa && b[0] == 0xeb && b[2] == 0x90 && b[0x0d] != 0))
        return "vfat";
    if (b[1024 + 0x38] == 0x53 && b[1024 + 0x39] == 0xef) { snprintf(why, whyn, "ext2/3/4 not supported"); return NULL; }
    snprintf(why, whyn, "unknown filesystem");
    return NULL;
}

static unsigned long long uh_sectors(const char *disk, const char *dev) {
    char p[96], v[32];
    if (strcmp(disk, dev) == 0) snprintf(p, sizeof(p), "/sys/block/%s/size", disk);
    else snprintf(p, sizeof(p), "/sys/block/%s/%s/size", disk, dev);
    uh_read_str(p, v, sizeof(v));
    return strtoull(v, NULL, 10);
}

static UhMnt *uh_find_mnt(const char *dev) {
    for (int i = 0; i < UH_MAX_MNT; i++) if (uh_mnts[i].active && !strcmp(uh_mnts[i].dev, dev)) return &uh_mnts[i];
    return NULL;
}
static UhMnt *uh_new_mnt(const char *dev, const char *disk, unsigned now) {
    for (int i = 0; i < UH_MAX_MNT; i++)
        if (!uh_mnts[i].active) {
            UhMnt *m = &uh_mnts[i];
            memset(m, 0, sizeof(*m));
            m->active = 1; m->first_ms = now;
            snprintf(m->dev, sizeof(m->dev), "%s", dev);
            snprintf(m->disk, sizeof(m->disk), "%s", disk);
            snprintf(m->mnt, sizeof(m->mnt), UH_MNT_ROOT "/%s", dev);
            m->mb = uh_sectors(disk, dev) / 2048;
            return m;
        }
    return NULL;
}

static void uh_umount_one(UhMnt *m) {
    if (m->mounted) {
        if (umount(m->mnt) != 0) umount2(m->mnt, MNT_DETACH);
        rmdir(m->mnt);
    }
    m->mounted = 0;
}

static void uh_try_mount(UhMnt *m, unsigned now) {
    char dev[32], why[64];
    m->last_try_ms = now;
    m->tries++;
    snprintf(dev, sizeof(dev), "/dev/%s", m->dev);
    const char *fs = uh_probe_fs(dev, why, sizeof(why));
    if (!fs) { snprintf(m->err, sizeof(m->err), "%s", why); return; }
    mkdir("/mnt", 0777); mkdir("/mnt/SDCARD", 0777); mkdir(UH_MNT_ROOT, 0777); mkdir(m->mnt, 0777);
    if (mount(dev, m->mnt, fs, MS_NOATIME, NULL) == 0 ||
        (errno != EBUSY && mount(dev, m->mnt, fs, MS_NOATIME | MS_RDONLY, NULL) == 0)) {
        m->mounted = 1; snprintf(m->fs, sizeof(m->fs), "%s", fs); m->err[0] = 0;
        return;
    }
    snprintf(m->err, sizeof(m->err), "mount %s: %s", fs, strerror(errno));
}

/* One refresh step: enumerate, create/maintain mounts for every partition of every storage disk,
 * lazily unmount things whose block device is gone.  Call about once per second. */
static void uh_refresh(unsigned now) {
    uh_enumerate();
    /* GC: block device vanished (unplugged) */
    for (int i = 0; i < UH_MAX_MNT; i++) {
        UhMnt *m = &uh_mnts[i];
        char p[96];
        if (!m->active) continue;
        if (strcmp(m->disk, m->dev) == 0) snprintf(p, sizeof(p), "/sys/block/%s", m->disk);
        else snprintf(p, sizeof(p), "/sys/block/%s/%s", m->disk, m->dev);
        if (access(p, F_OK) != 0) { uh_umount_one(m); m->active = 0; }
    }
    for (int i = 0; i < uh_ndev; i++) {
        UhDev *u = &uh_devs[i];
        if (u->cls != UC_STORAGE || !u->disk[0]) continue;
        char dp[48]; int nparts = 0;
        snprintf(dp, sizeof(dp), "/sys/block/%s", u->disk);
        DIR *d = opendir(dp);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                size_t dl = strlen(u->disk);
                if (strncmp(e->d_name, u->disk, dl) == 0 && e->d_name[dl] >= '0' && e->d_name[dl] <= '9') {
                    nparts++;
                    if (!uh_find_mnt(e->d_name)) uh_new_mnt(e->d_name, u->disk, now);
                }
            }
            closedir(d);
        }
        if (!nparts && !uh_find_mnt(u->disk)) uh_new_mnt(u->disk, u->disk, now);   /* superfloppy */
    }
    /* try mounting: wait 3 s after first sight (stock hotplug), then retry every 2 s, max 5 tries */
    for (int i = 0; i < UH_MAX_MNT; i++) {
        UhMnt *m = &uh_mnts[i];
        if (m->active && !m->mounted && m->tries < 5 && now - m->first_ms > 3000 && now - m->last_try_ms > 2000)
            uh_try_mount(m, now);
    }
}

/* Safe eject: unmount everything on that disk, flush, detach the SCSI device. */
static void uh_eject_disk(const char *disk) {
    sync();
    for (int i = 0; i < UH_MAX_MNT; i++)
        if (uh_mnts[i].active && !strcmp(uh_mnts[i].disk, disk)) { uh_umount_one(&uh_mnts[i]); uh_mnts[i].active = 0; }
    sync();
    char p[96];
    snprintf(p, sizeof(p), "/sys/block/%s/device/delete", disk);
    int fd = open(p, O_WRONLY);
    if (fd >= 0) { if (write(fd, "1", 1) < 0) { /* ignore */ } close(fd); }
}

static void uh_eject_all(void) {
    char done[UH_MAX_MNT][8]; int nd = 0;
    for (int i = 0; i < UH_MAX_MNT; i++) {
        if (!uh_mnts[i].active) continue;
        int seen = 0;
        for (int j = 0; j < nd; j++) if (!strcmp(done[j], uh_mnts[i].disk)) seen = 1;
        if (!seen) { snprintf(done[nd++], 8, "%s", uh_mnts[i].disk); }
    }
    for (int j = 0; j < nd; j++) uh_eject_disk(done[j]);
}

/* ---------- host mode switch (the Type-C port is shared with the USB gadget / ADB!) ---------- */
#define UH_OTG_SYS "/sys/devices/platform/soc@3000000/soc@3000000:usbc0@0"
static void uh_read_file_trigger(const char *name) {      /* the otg manager switches role when the file is read */
    char p[128], buf[32];
    snprintf(p, sizeof(p), UH_OTG_SYS "/%s", name);
    uh_read_str(p, buf, sizeof(buf));
}
static void uh_set_host(void) {
    system("echo '' > /sys/kernel/config/usb_gadget/g1/UDC 2>/dev/null");
    system("killall -TERM umsd.elf 2>/dev/null");
    uh_read_file_trigger("usb_null");
    usleep(200000);
    uh_read_file_trigger("usb_host");
}

#endif
