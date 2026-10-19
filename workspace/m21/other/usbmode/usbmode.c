#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "usbhost.h"

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
    cur_role = uh_role_is_host() ? ROLE_HOST : ROLE_DEVICE;
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
    if (cur_role != ROLE_HOST) { uh_daemon_stop(); uh_eject_all_proc(); }
    if (cur_role == ROLE_HOST) {
        uh_set_host();
        uh_daemon_start();
        snprintf(status_msg, sizeof(status_msg), "USB Host ON (ADB/USB gadget unavailable while Host is on)");
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


/* ===================== USB Host (OTG) sub page ===================== */
static void host_set_enabled(int on) {
    if (on) { cur_role = ROLE_HOST; cur_storage = STORAGE_OFF; }
    else    { cur_role = ROLE_DEVICE; }
    memset(uh_mnts, 0, sizeof(uh_mnts));
    uh_ndev = 0;
    apply_usb_config();
}

/* one text line describing a device row */
static void host_dev_line(const UhDev *u, char *out, size_t n) {
    char extra[64] = "";
    if (u->cls == UC_STORAGE) {
        int nm = 0, mounted = 0, failed = 0;
        unsigned long long mb = 0;
        for (int i = 0; i < UH_MAX_MNT; i++)
            if (uh_mnts[i].active && !strcmp(uh_mnts[i].disk, u->disk)) {
                nm++; mounted += uh_mnts[i].mounted; mb += uh_mnts[i].mb;
                if (!uh_mnts[i].mounted && uh_mnts[i].tries >= 5) failed++;
            }
        if (!nm) snprintf(extra, sizeof(extra), " - reading...");
        else if (mounted == nm) snprintf(extra, sizeof(extra), " - mounted (%llu MB)", mb);
        else if (failed) snprintf(extra, sizeof(extra), " - mount failed");
        else snprintf(extra, sizeof(extra), " - mounting...");
    }
    snprintf(out, n, "[%s] %s%s", uh_cls_name(u->cls), u->name, extra);
}

/* detail text for the footer (for the selected device) */
static void host_dev_detail(const UhDev *u, char *out, size_t n) {
    snprintf(out, n, "%s  %s  %s Mbps", u->path, u->vidpid, u->speed[0] ? u->speed : "?");
    if (u->cls == UC_STORAGE)
        for (int i = 0; i < UH_MAX_MNT; i++)
            if (uh_mnts[i].active && !strcmp(uh_mnts[i].disk, u->disk)) {
                size_t l = strlen(out);
                if (uh_mnts[i].mounted) snprintf(out + l, n - l, " | %s:%s -> %s", uh_mnts[i].dev, uh_mnts[i].fs, uh_mnts[i].mnt);
                else if (uh_mnts[i].err[0]) snprintf(out + l, n - l, " | %s: %s", uh_mnts[i].dev, uh_mnts[i].err);
                break;
            }
}

static void run_host_page(SDL_Surface *screen) {
    int sel = 0, quit = 0, dirty = 1, show_setting = 0;
    uint32_t last_ms = 0;
    char sig_prev[1024] = "";
    char hint[160] = "Needs a SELF-POWERED hub: this port gives no 5V";
    if (cur_role == ROLE_HOST) uh_daemon_start();

    while (!quit) {
        GFX_startFrame();
        PAD_poll();
        uint32_t now = SDL_GetTicks();

        if (cur_role == ROLE_HOST && (now - last_ms > 1000 || last_ms == 0)) {
            last_ms = now ? now : 1;
            uh_enumerate();
            uh_state_read();
            char sig[1024] = "";
            for (int i = 0; i < uh_ndev; i++) { char ln[160]; host_dev_line(&uh_devs[i], ln, sizeof(ln)); strncat(sig, ln, sizeof(sig) - strlen(sig) - 2); strcat(sig, "|"); }
            if (strcmp(sig, sig_prev)) { snprintf(sig_prev, sizeof(sig_prev), "%s", sig); dirty = 1; }
        }

        int nrows = 1 + (cur_role == ROLE_HOST ? uh_ndev : 0) + 1;   /* toggle, devices, back */
        if (sel >= nrows) sel = nrows - 1;
        int is_dev_row = (sel >= 1 && sel <= (cur_role == ROLE_HOST ? uh_ndev : 0));

        if (PAD_justRepeated(BTN_UP) || PAD_justPressed(BTN_UP)) { sel = (sel - 1 + nrows) % nrows; dirty = 1; }
        else if (PAD_justRepeated(BTN_DOWN) || PAD_justPressed(BTN_DOWN)) { sel = (sel + 1) % nrows; dirty = 1; }
        else if (PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT) || PAD_justPressed(BTN_A)) {
            dirty = 1;
            if (sel == 0) {
                int on = (cur_role != ROLE_HOST);
                snprintf(hint, sizeof(hint), on ? "Switching to USB Host..." : "Ejecting and switching back to device mode...");
                GFX_clear(screen);
                GFX_blitMessage(font.large, hint, screen, &(SDL_Rect){0, 0, screen->w, screen->h});
                GFX_flip(screen);
                host_set_enabled(on);
                last_ms = 0;
                snprintf(hint, sizeof(hint), on ? "Host ON. Plug a self-powered hub / device." : "Host OFF (device mode restored)");
            } else if (sel == nrows - 1) {
                if (PAD_justPressed(BTN_A)) quit = 1;
            } else if (is_dev_row) {
                const UhDev *u = &uh_devs[sel - 1];
                if (u->cls == UC_STORAGE && u->disk[0]) {
                    int any = 0;
                    for (int i = 0; i < UH_MAX_MNT; i++)
                        if (uh_mnts[i].active && !strcmp(uh_mnts[i].disk, u->disk)) any |= uh_mnts[i].mounted;
                    char cmd[40];
                    if (any) {
                        snprintf(cmd, sizeof(cmd), "eject %s", u->disk);
                        uh_cmd_send(cmd);
                        snprintf(hint, sizeof(hint), "Ejecting %s... safe to unplug when it disappears", u->disk);
                    } else {
                        snprintf(cmd, sizeof(cmd), "retry %s", u->disk);
                        uh_cmd_send(cmd);
                        snprintf(hint, sizeof(hint), "Retrying mount of %s...", u->disk);
                    }
                    last_ms = 0;
                } else {
                    snprintf(hint, sizeof(hint), "%s: no action (only storage devices are handled)", uh_cls_name(u->cls));
                }
            }
        } else if (PAD_justPressed(BTN_B)) {
            quit = 1;
        }

        PWR_update(&dirty, &show_setting, NULL, NULL);

        if (dirty) {
            GFX_clear(screen);
            GFX_blitHardwareGroup(screen, show_setting, 0);

            SDL_Surface *title = TTF_RenderUTF8_Blended(font.large, "USB Host (OTG)", COLOR_GOLD);
            if (title) {
                SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), SCALE1(PADDING)});
                SDL_FreeSurface(title);
            }

            char rows[2 + UH_MAX_DEV][160];
            int n = 0;
            snprintf(rows[n++], sizeof(rows[0]), "Host mode: %s", cur_role == ROLE_HOST ? "< ON >" : "< OFF >");
            if (cur_role == ROLE_HOST) {
                if (!uh_ndev) snprintf(rows[n++], sizeof(rows[0]), "(no devices - plug a powered hub)");
                else for (int i = 0; i < uh_ndev; i++) host_dev_line(&uh_devs[i], rows[n++], sizeof(rows[0]));
            }
            snprintf(rows[n++], sizeof(rows[0]), "[ Back ]");
            /* display row -> selectable row index (-1 = non selectable placeholder) */
            int placeholder = (cur_role == ROLE_HOST && !uh_ndev);
            for (int i = 0; i < n; i++) {
                int y = SCALE1(PADDING + (i + 1) * PILL_SIZE);
                int avail_w = screen->w - SCALE1(PADDING * 2);
                char disp[160];
                int tw = GFX_truncateText(font.medium, rows[i], disp, avail_w, SCALE1(BUTTON_PADDING * 2));
                int mw = MIN(avail_w, tw);
                int row_index = i;
                if (i == n - 1) row_index = nrows - 1;                 /* Back */
                else if (placeholder && i == 1) row_index = -1;        /* "(no devices)" */
                SDL_Color col = COLOR_WHITE;
                if (row_index == sel) {
                    GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, mw, SCALE1(PILL_SIZE)});
                    col = COLOR_BLACK;
                }
                SDL_Surface *txt = TTF_RenderUTF8_Blended(font.medium, disp, col);
                if (txt) {
                    SDL_BlitSurface(txt, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), y + (SCALE1(PILL_SIZE) - txt->h) / 2});
                    SDL_FreeSurface(txt);
                }
            }

            char foot[200];
            if (is_dev_row) host_dev_detail(&uh_devs[sel - 1], foot, sizeof(foot));
            else snprintf(foot, sizeof(foot), "%s", hint);
            char fdisp[200];
            GFX_truncateText(font.small, foot, fdisp, screen->w - SCALE1(PADDING * 2 + BUTTON_PADDING * 2), 0);
            SDL_Surface *st = TTF_RenderUTF8_Blended(font.small, fdisp, COLOR_LIGHT_TEXT);
            if (st) {
                SDL_BlitSurface(st, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), screen->h - SCALE1(PADDING + PILL_SIZE + 22)});
                SDL_FreeSurface(st);
            }

            GFX_blitButtonGroup((char*[]){ "U/D", "SELECT", "L/R/A", is_dev_row ? "EJECT" : "CHANGE", NULL }, 0, screen, 0, 0);
            GFX_blitButtonGroup((char*[]){ "B", "BACK", NULL }, 1, screen, 1, 0);
            GFX_flip(screen);
            dirty = 0;
        } else {
            GFX_sync();
        }
    }
}

/* command line self test (no UI): usbmode.elf --host-test <seconds>
 * Switches to host, lists devices / mounts storage every 2 s, ejects, restores device mode. */
static int host_test(int secs) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("host-test: switching to host for %ds\n", secs);
    host_set_enabled(1);
    unsigned t0 = 0;
    for (int t = 0; t < secs; t += 1) {
        sleep(1);
        t0 += 1000;
        uh_enumerate();
        uh_state_read();
        if (t % 2 == 0) {
            printf("[t=%ds] %d usb device(s)\n", t, uh_ndev);
            for (int i = 0; i < uh_ndev; i++) {
                char ln[160], dt[200];
                host_dev_line(&uh_devs[i], ln, sizeof(ln));
                host_dev_detail(&uh_devs[i], dt, sizeof(dt));
                printf("   %s\n      %s\n", ln, dt);
            }
            for (int i = 0; i < UH_MAX_MNT; i++)
                if (uh_mnts[i].active && uh_mnts[i].mounted) {
                    printf("   mounted %s -> %s (%s)\n", uh_mnts[i].dev, uh_mnts[i].mnt, uh_mnts[i].fs);
                    if (t >= 12 && t < 14) { char cmd[128]; snprintf(cmd, sizeof(cmd), "ls -la '%s' | head -12; df '%s'", uh_mnts[i].mnt, uh_mnts[i].mnt); system(cmd); }
                }
        }
    }
    printf("host-test: restoring device mode\n");
    host_set_enabled(0);
    printf("host-test: done\n");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && !strcmp(argv[1], "--host-daemon")) return uh_daemon_main();
    if (argc >= 2 && !strcmp(argv[1], "--host-test")) return host_test(argc >= 3 ? atoi(argv[2]) : 60);

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
                    run_host_page(screen);
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
            snprintf(row_buf[ITEM_ROLE], sizeof(row_buf[0]), "USB Host (OTG): %s",
                     cur_role == ROLE_HOST ? "< ON >  (A: devices)" : "< OFF >  (A: open)");
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
