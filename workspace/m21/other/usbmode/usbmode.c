#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <dirent.h>
#include <errno.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

#define BOOT_CFG_PATH "/mnt/SDCARD/.userdata/m21/usb_boot_mode.cfg"
#define NOSHARE_FLAG  "/mnt/SDCARD/.userdata/m21/usb_noshare"
#define GADGET_DIR    "/sys/kernel/config/usb_gadget/g1"
#define UDC_CTRL      "4100000.udc-controller"

enum {
    ITEM_STORAGE = 0,
    ITEM_ADB,
    ITEM_SERIAL,
    ITEM_NETWORK,
    ITEM_ROLE,
    ITEM_KEEP_BG,
    ITEM_BOOT_DEFAULT,
    ITEM_BOOT_NET,
    ITEM_EXIT,
    ITEM_COUNT
};

enum {
    STORAGE_OFF = 0,
    STORAGE_RW  = 1,
    STORAGE_RO  = 2,
};

enum {
    ROLE_DEVICE = 0,
    ROLE_HOST   = 1,
};

enum {
    BOOT_ADB_ONLY   = 0,
    BOOT_ADB_UMS_RW = 1,
    BOOT_ADB_UMS_RO = 2,
    BOOT_USB_OFF    = 3,
};

static int cur_storage = STORAGE_OFF;
static int cur_adb     = 1;
static int cur_serial  = 0;
static int cur_net     = 0;   /* 0=off, 1=on (+internet via PC), 2=on (local only) */
static int boot_net    = 0;
static int cur_role    = ROLE_DEVICE;
static int keep_bg     = 0;
static int boot_mode   = BOOT_ADB_ONLY;
static char status_msg[128] = "Ready - Use UP/DOWN & LEFT/RIGHT or A";


/* ---- OTG host: USB stick auto-mount (needs an externally powered hub: the port gives no 5V) ---- */
#define USB_MNT "/mnt/usb"
static int  usb_disk_mounted = 0;
static char usb_dev[32] = "";
static char usb_fs[16]  = "";
static uint32_t usb_seen_ms = 0;     /* when the block device first appeared */
static uint32_t usb_try_ms  = 0;

/* find /dev/sdX1 (preferred) or /dev/sdX */
static int find_usb_block(char *out, size_t n) {
    DIR *d = opendir("/dev");
    char whole[32] = "", part[32] = "";
    struct dirent *e;
    if (!d) return 0;
    while ((e = readdir(d))) {
        const char *nm = e->d_name;
        if (nm[0] == 's' && nm[1] == 'd' && nm[2] >= 'a' && nm[2] <= 'z') {
            if (nm[3] == 0) snprintf(whole, sizeof(whole), "/dev/%s", nm);
            else if (nm[3] == '1' && nm[4] == 0) snprintf(part, sizeof(part), "/dev/%s", nm);
        }
    }
    closedir(d);
    if (part[0]) { snprintf(out, n, "%s", part); return 1; }
    if (whole[0]) { snprintf(out, n, "%s", whole); return 1; }
    return 0;
}

/* Note: the stock firmware's hotplug script already mounts the stick rw at /mnt/exUDISK (outside our
 * chroot), so we must mount rw as well (a ro mount of the same device fails with EBUSY). */
static void usb_try_mount(void) {
    static const char *fss[] = { "vfat", "exfat", "ntfs", NULL };
    char dev[32];
    if (usb_disk_mounted || !find_usb_block(dev, sizeof(dev))) return;
    mkdir("/mnt", 0777);
    mkdir(USB_MNT, 0777);
    for (int i = 0; fss[i]; i++) {
        if (mount(dev, USB_MNT, fss[i], MS_NOATIME, NULL) == 0) {
            usb_disk_mounted = 1;
            snprintf(usb_dev, sizeof(usb_dev), "%s", dev);
            snprintf(usb_fs, sizeof(usb_fs), "%s", fss[i]);
            snprintf(status_msg, sizeof(status_msg), "USB disk %s mounted at " USB_MNT " (%s)", dev, fss[i]);
            return;
        }
    }
    snprintf(status_msg, sizeof(status_msg), "USB disk %s found, mount failed: %s", dev, strerror(errno));
}

static void usb_eject(void) {
    sync();
    if (usb_disk_mounted) {
        if (umount(USB_MNT) != 0) umount2(USB_MNT, MNT_DETACH);
        /* detach the SCSI device so the (stock, outside-chroot) automount can no longer write to it */
        const char *base = strrchr(usb_dev, '/');
        if (base) {
            char sd[8] = {0}, cmd[96];
            snprintf(sd, sizeof(sd), "%.3s", base + 1);
            snprintf(cmd, sizeof(cmd), "echo 1 > /sys/block/%s/device/delete 2>/dev/null", sd);
            system(cmd);
        }
    }
    usb_disk_mounted = 0;
    usb_dev[0] = 0;
    usb_seen_ms = 0;
}

static void run_net(const char *arg) {
    const char *dir = getenv("USBMODE_PAK_DIR");
    char cmd[512];
    if (!dir || !*dir) dir = "/mnt/SDCARD/Tools/m21/USB Mode.pak";
    snprintf(cmd, sizeof(cmd), "sh '%s/usbnet.sh' %s >/tmp/usbnet.log 2>&1", dir, arg);
    system(cmd);
}

static bool is_process_running(const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ps | grep -v grep | grep -q '%s'", name);
    return (system(cmd) == 0);
}

static bool is_link_present(const char *path) {
    return (access(path, F_OK) == 0);
}

static void detect_current_state(void) {
    if (is_link_present(GADGET_DIR "/configs/c.1/ffs.ums") && is_process_running("umsd.elf")) {
        if (is_process_running("umsd.elf.*--ro")) cur_storage = STORAGE_RO;
        else cur_storage = STORAGE_RW;
    } else {
        cur_storage = STORAGE_OFF;
    }
    cur_adb = is_link_present(GADGET_DIR "/configs/c.1/ffs.adb") ? 1 : 0;
    cur_serial = is_link_present(GADGET_DIR "/configs/c.1/gser.0") ? 1 : 0;
    cur_net = is_link_present(GADGET_DIR "/configs/c.1/ecm.usb0") ? (access(NOSHARE_FLAG, F_OK) == 0 ? 2 : 1) : 0;

    FILE *fp = fopen(BOOT_CFG_PATH, "r");
    if (fp) {
        char buf[64] = {0};
        if (fgets(buf, sizeof(buf), fp)) {
            if (strncmp(buf, "adb_ums_rw", 10) == 0) boot_mode = BOOT_ADB_UMS_RW;
            else if (strncmp(buf, "adb_ums_ro", 10) == 0) boot_mode = BOOT_ADB_UMS_RO;
            else if (strncmp(buf, "off", 3) == 0) boot_mode = BOOT_USB_OFF;
            else boot_mode = BOOT_ADB_ONLY;
        }
        if (fgets(buf, sizeof(buf), fp) && strncmp(buf, "net", 3) == 0) boot_net = 1;
        fclose(fp);
    }
}

static void save_boot_config(void) {
    mkdir("/mnt/SDCARD/.userdata", 0777);
    mkdir("/mnt/SDCARD/.userdata/m21", 0777);
    FILE *fp = fopen(BOOT_CFG_PATH, "w");
    if (!fp) return;
    if (boot_mode == BOOT_ADB_UMS_RW) fprintf(fp, "adb_ums_rw\n");
    else if (boot_mode == BOOT_ADB_UMS_RO) fprintf(fp, "adb_ums_ro\n");
    else if (boot_mode == BOOT_USB_OFF) fprintf(fp, "off\n");
    else fprintf(fp, "adb_only\n");
    if (boot_net && boot_mode != BOOT_USB_OFF) fprintf(fp, "net\n");
    fclose(fp);
    sync();
}

static void apply_usb_config(void) {
    if (usb_disk_mounted && cur_role != ROLE_HOST) usb_eject();
    if (cur_role == ROLE_HOST) {
        system("echo '' > " GADGET_DIR "/UDC 2>/dev/null");
        system("killall -TERM umsd.elf 2>/dev/null");
        system("cat /sys/devices/platform/soc@3000000/soc@3000000:usbc0@0/usb_null >/dev/null 2>&1");
        usleep(200000);
        system("cat /sys/devices/platform/soc@3000000/soc@3000000:usbc0@0/usb_host >/dev/null 2>&1");
        snprintf(status_msg, sizeof(status_msg), "OTG Host: use a POWERED hub (port has no 5V). Waiting for USB disk...");
        return;
    }

    /* Ensure USB Device Role */
    system("cat /sys/devices/platform/soc@3000000/soc@3000000:usbc0@0/usb_device >/dev/null 2>&1");

    /* Unbind UDC before reconfiguring functions */
    system("echo '' > " GADGET_DIR "/UDC 2>/dev/null");
    usleep(100000);

    system("rm -f " GADGET_DIR "/configs/c.1/ffs.adb "
                    GADGET_DIR "/configs/c.1/ffs.ums "
                    GADGET_DIR "/configs/c.1/gser.0 "
                    GADGET_DIR "/configs/c.1/ecm.usb0 2>/dev/null");
    run_net("off");

    /* Stop existing umsd.elf */
    system("killall -TERM umsd.elf 2>/dev/null");
    usleep(150000);

    /* Configure USB Storage if enabled */
    if (cur_storage != STORAGE_OFF) {
        system("sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null");
        system("mkdir -p " GADGET_DIR "/functions/ffs.ums /dev/usb-ffs/ums 2>/dev/null");
        system("grep -q '/dev/usb-ffs/ums' /proc/mounts || mount -t functionfs ums /dev/usb-ffs/ums");

        const char *disk_dev = "/dev/mmcblk0";
        if (access("/dev/mmcblk0p1", F_OK) == 0) disk_dev = "/dev/mmcblk0p1";

        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "/tmp/umsd.elf /dev/usb-ffs/ums %s --exit-on-eject %s >/tmp/umsd.log 2>&1 &",
                 disk_dev, (cur_storage == STORAGE_RO) ? "--ro" : "");
        system(cmd);
        usleep(600000);
        system("ln -sf " GADGET_DIR "/functions/ffs.ums " GADGET_DIR "/configs/c.1/ffs.ums 2>/dev/null");
    } else {
        system("sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null");
    }

    /* Configure ADB if enabled */
    if (cur_adb) {
        system("ln -sf " GADGET_DIR "/functions/ffs.adb " GADGET_DIR "/configs/c.1/ffs.adb 2>/dev/null");
    }

    /* Configure USB Serial (ttyGS0) if enabled */
    if (cur_serial) {
        system("mkdir -p " GADGET_DIR "/functions/gser.0 2>/dev/null");
        system("ln -sf " GADGET_DIR "/functions/gser.0 " GADGET_DIR "/configs/c.1/gser.0 2>/dev/null");
        if (!is_process_running("ttyGS0")) {
            system("(while :; do [ -c /dev/ttyGS0 ] && /bin/ash --login </dev/ttyGS0 >/dev/ttyGS0 2>&1; sleep 2; done) &");
        }
    }

    /* Configure USB Network (CDC-ECM) if enabled */
    int net_active = 0;
    if (cur_net) {
        run_net("on");
        net_active = is_link_present(GADGET_DIR "/configs/c.1/ecm.usb0");
        if (!net_active) cur_net = 0;
    }

    /* Bind UDC if at least one function is enabled */
    if (cur_storage != STORAGE_OFF || cur_adb || cur_serial || cur_net) {
        system("echo '0x18d1' > " GADGET_DIR "/idVendor 2>/dev/null");
        system("echo '0xD002' > " GADGET_DIR "/idProduct 2>/dev/null");
        system("echo '" UDC_CTRL "' > " GADGET_DIR "/UDC 2>/dev/null");
        if (cur_net) {
            usleep(300000);
            if (access(GADGET_DIR "/UDC", F_OK) == 0 &&
                system("[ -n \"$(cat " GADGET_DIR "/UDC)\" ]") != 0) {
                /* bind failed with ECM: drop it and rebind the rest */
                cur_net = 0;
                run_net("off");
                system("echo '" UDC_CTRL "' > " GADGET_DIR "/UDC 2>/dev/null");
            } else {
                run_net("up");
            }
        }
        snprintf(status_msg, sizeof(status_msg), "Active: Storage=%s | ADB=%s | Serial=%s | Net=%s",
                 cur_storage == STORAGE_RW ? "RW" : (cur_storage == STORAGE_RO ? "RO" : "OFF"),
                 cur_adb ? "ON" : "OFF",
                 cur_serial ? "ON" : "OFF",
                 cur_net == 1 ? "ECM 192.168.7.1 +NET" : (cur_net == 2 ? "ECM 192.168.7.1 local" : "OFF"));
    } else {
        snprintf(status_msg, sizeof(status_msg), "All USB Gadget Functions Disabled");
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    PWR_setCPUSpeed(CPU_SPEED_MENU);
    SDL_Surface *screen = GFX_init(MODE_MAIN);
    PAD_init();
    PWR_init();
    InitSettings();

    detect_current_state();

    int selected = 0;
    int quit = 0;
    int dirty = 1;
    int show_setting = 0;
    uint32_t last_poll_ms = SDL_GetTicks();

    while (!quit) {
        GFX_startFrame();
        PAD_poll();

        /* Periodically check if host ejected USB Storage while in menu */
        uint32_t now = SDL_GetTicks();
        if (now - last_poll_ms > 500) {
            last_poll_ms = now;
            if (cur_storage != STORAGE_OFF && !is_process_running("umsd.elf")) {
                cur_storage = STORAGE_OFF;
                apply_usb_config();
                snprintf(status_msg, sizeof(status_msg), "USB Storage Ejected by Computer");
                dirty = 1;
            }
        }

        if (cur_role == ROLE_HOST && !usb_disk_mounted) {
            char dev[32];
            if (find_usb_block(dev, sizeof(dev))) {
                if (!usb_seen_ms) usb_seen_ms = now;
                /* give the system hotplug 3 s, then retry every 2 s */
                if (now - usb_seen_ms > 3000 && now - usb_try_ms > 2000) {
                    usb_try_ms = now;
                    usb_try_mount();
                    dirty = 1;
                }
            } else {
                usb_seen_ms = 0;
            }
        }

        if (PAD_justRepeated(BTN_UP) || PAD_justPressed(BTN_UP)) {
            selected = (selected - 1 + ITEM_COUNT) % ITEM_COUNT;
            dirty = 1;
        } else if (PAD_justRepeated(BTN_DOWN) || PAD_justPressed(BTN_DOWN)) {
            selected = (selected + 1) % ITEM_COUNT;
            dirty = 1;
        } else if (PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT) || PAD_justPressed(BTN_A)) {
            int dir = PAD_justPressed(BTN_LEFT) ? -1 : 1;
            dirty = 1;

            switch (selected) {
                case ITEM_STORAGE:
                    cur_storage = (cur_storage + dir + 3) % 3;
                    cur_role = ROLE_DEVICE;
                    snprintf(status_msg, sizeof(status_msg), "Applying USB Storage Mode...");
                    GFX_clear(screen);
                    GFX_blitMessage(font.large, status_msg, screen, &(SDL_Rect){0, 0, screen->w, screen->h});
                    GFX_flip(screen);
                    apply_usb_config();
                    break;
                case ITEM_ADB:
                    cur_adb = !cur_adb;
                    cur_role = ROLE_DEVICE;
                    apply_usb_config();
                    break;
                case ITEM_SERIAL:
                    cur_serial = !cur_serial;
                    cur_role = ROLE_DEVICE;
                    apply_usb_config();
                    break;
                case ITEM_NETWORK:
                    cur_net = (cur_net + dir + 3) % 3;
                    if (cur_net == 2) {
                        mkdir("/mnt/SDCARD/.userdata", 0777);
                        mkdir("/mnt/SDCARD/.userdata/m21", 0777);
                        FILE *nf = fopen(NOSHARE_FLAG, "w");
                        if (nf) fclose(nf);
                    } else {
                        unlink(NOSHARE_FLAG);
                    }
                    cur_role = ROLE_DEVICE;
                    apply_usb_config();
                    break;
                case ITEM_ROLE:
                    cur_role = !cur_role;
                    if (cur_role == ROLE_HOST) cur_storage = STORAGE_OFF;
                    apply_usb_config();
                    break;
                case ITEM_KEEP_BG:
                    keep_bg = !keep_bg;
                    snprintf(status_msg, sizeof(status_msg),
                             keep_bg ? "Will keep USB Storage mounted after exiting"
                                     : "Will auto-disconnect USB Storage on exit");
                    break;
                case ITEM_BOOT_DEFAULT:
                    boot_mode = (boot_mode + dir + 4) % 4;
                    save_boot_config();
                    snprintf(status_msg, sizeof(status_msg), "Saved Boot Default USB Mode");
                    break;
                case ITEM_BOOT_NET:
                    boot_net = !boot_net;
                    save_boot_config();
                    snprintf(status_msg, sizeof(status_msg), "Saved Boot USB Network setting");
                    break;
                case ITEM_EXIT:
                    if (PAD_justPressed(BTN_A)) quit = 1;
                    break;
            }
        } else if (PAD_justPressed(BTN_B)) {
            quit = 1;
        }

        PWR_update(&dirty, &show_setting, NULL, NULL);

        if (dirty) {
            GFX_clear(screen);
            GFX_blitHardwareGroup(screen, show_setting, 0);

            /* Title pill */
            SDL_Surface *title = TTF_RenderUTF8_Blended(font.large, "USB Mode Manager", COLOR_GOLD);
            if (title) {
                SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), SCALE1(PADDING)});
                SDL_FreeSurface(title);
            }

            const char *labels[ITEM_COUNT];
            char row_buf[ITEM_COUNT][128];

            snprintf(row_buf[ITEM_STORAGE], sizeof(row_buf[0]), "USB Storage (SD Card): %s",
                     cur_storage == STORAGE_RW ? "< ON (Read-Write) >" :
                     (cur_storage == STORAGE_RO ? "< ON (Read-Only) >" : "< OFF >"));
            snprintf(row_buf[ITEM_ADB], sizeof(row_buf[0]), "ADB Debugging: %s",
                     cur_adb ? "< ON >" : "< OFF >");
            snprintf(row_buf[ITEM_SERIAL], sizeof(row_buf[0]), "USB Serial (ttyGS0): %s",
                     cur_serial ? "< ON >" : "< OFF >");
            snprintf(row_buf[ITEM_NETWORK], sizeof(row_buf[0]), "USB Network (192.168.7.x): %s",
                     cur_net == 1 ? "< ON (+Internet via PC) >" : (cur_net == 2 ? "< ON (Local only) >" : "< OFF >"));
            snprintf(row_buf[ITEM_BOOT_NET], sizeof(row_buf[0]), "Boot USB Network: %s",
                     boot_net ? "< ON >" : "< OFF >");
            snprintf(row_buf[ITEM_ROLE], sizeof(row_buf[0]), "USB Port Role: %s",
                     cur_role == ROLE_DEVICE ? "< Device (PC/Mac) >" : (usb_disk_mounted ? "< OTG Host: disk at /mnt/usb >" : "< OTG Host (powered hub) >"));
            snprintf(row_buf[ITEM_KEEP_BG], sizeof(row_buf[0]), "On Exit to MinUI: %s",
                     keep_bg ? "< Keep Storage Mounted >" : "< Auto Eject Storage >");
            snprintf(row_buf[ITEM_BOOT_DEFAULT], sizeof(row_buf[0]), "Boot Default: %s",
                     boot_mode == BOOT_ADB_ONLY ? "< ADB Only >" :
                     (boot_mode == BOOT_ADB_UMS_RW ? "< ADB + Storage (RW) >" :
                     (boot_mode == BOOT_ADB_UMS_RO ? "< ADB + Storage (RO) >" : "< USB Off >")));
            snprintf(row_buf[ITEM_EXIT], sizeof(row_buf[0]), "[ Return to MinUI ]");

            for (int i = 0; i < ITEM_COUNT; i++) {
                labels[i] = row_buf[i];
                int y = SCALE1(PADDING + (i + 1) * PILL_SIZE);
                int avail_w = screen->w - SCALE1(PADDING * 2);
                char disp[128];
                int tw = GFX_truncateText(font.medium, labels[i], disp, avail_w, SCALE1(BUTTON_PADDING * 2));
                int mw = MIN(avail_w, tw);

                SDL_Color col = COLOR_WHITE;
                if (i == selected) {
                    GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){
                        SCALE1(PADDING), y, mw, SCALE1(PILL_SIZE)
                    });
                    col = COLOR_BLACK;
                }
                SDL_Surface *txt = TTF_RenderUTF8_Blended(font.medium, disp, col);
                if (txt) {
                    SDL_BlitSurface(txt, NULL, screen, &(SDL_Rect){
                        SCALE1(PADDING + BUTTON_PADDING),
                        y + (SCALE1(PILL_SIZE) - txt->h) / 2
                    });
                    SDL_FreeSurface(txt);
                }
            }

            /* Status footer bar above button hints */
            SDL_Surface *st_txt = TTF_RenderUTF8_Blended(font.small, status_msg, COLOR_LIGHT_TEXT);
            if (st_txt) {
                SDL_BlitSurface(st_txt, NULL, screen, &(SDL_Rect){
                    SCALE1(PADDING + BUTTON_PADDING),
                    screen->h - SCALE1(PADDING + PILL_SIZE + 22)
                });
                SDL_FreeSurface(st_txt);
            }

            GFX_blitButtonGroup((char*[]){ "U/D", "SELECT", "L/R/A", "CHANGE", NULL }, 0, screen, 0, 0);
            GFX_blitButtonGroup((char*[]){ "B", "BACK", NULL }, 1, screen, 1, 0);

            GFX_flip(screen);
            dirty = 0;
        } else {
            GFX_sync();
        }
    }

    if (!keep_bg && cur_storage != STORAGE_OFF) {
        cur_storage = STORAGE_OFF;
        apply_usb_config();
    } else {
        system("sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null");
    }

    QuitSettings();
    PWR_quit();
    PAD_quit();
    GFX_quit();

    return 0;
}
