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
#define NETTYPE_FILE  "/mnt/SDCARD/.userdata/m21/usb_nettype"

enum {
    ITEM_STORAGE = 0,
    ITEM_ADB,
    ITEM_SERIAL,
    ITEM_NETWORK,      /* A opens the USB Network sub page */
    ITEM_ROLE,
    ITEM_KEEP_BG,
    ITEM_BOOT,         /* A opens the Boot Defaults sub page */
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

static int cur_storage = STORAGE_OFF;
static int cur_adb     = 1;
static int cur_serial  = 0;
static int cur_net     = 0;   /* 0=off, 1=on (+internet via PC), 2=on (local only) */
static int net_type    = 0;   /* 0=ECM (Mac/Linux), 1=NCM (Mac/Linux/Win11), 2=RNDIS (Windows) */
/* boot defaults (usb_boot_mode.cfg): adb / storage 0=off 1=rw 2=ro / serial / network */
static int boot_adb     = 1;
static int boot_storage = 0;
static int boot_serial  = 0;
static int boot_net     = 0;
static int cur_role    = ROLE_DEVICE;
static int keep_bg     = 0;
static char status_msg[128] = "Ready - Use UP/DOWN & LEFT/RIGHT or A";


static const char *net_type_name(void) { return net_type == 1 ? "ncm" : (net_type == 2 ? "rndis" : "ecm"); }
static const char *net_type_label(void) {
    return net_type == 1 ? "NCM (Mac/Linux/Win11)" : (net_type == 2 ? "RNDIS (Windows)" : "ECM (Mac/Linux)");
}
static void save_net_type(void) {
    mkdir("/mnt/SDCARD/.userdata", 0777);
    mkdir("/mnt/SDCARD/.userdata/m21", 0777);
    FILE *f = fopen(NETTYPE_FILE, "w");
    if (f) { fprintf(f, "%s\n", net_type_name()); fclose(f); }
}

static void run_net(const char *arg) {
    const char *dir = getenv("USBMODE_PAK_DIR");
    char cmd[512];
    if (!dir || !*dir) dir = "/mnt/SDCARD/Tools/m21/USB Mode.pak";
    snprintf(cmd, sizeof(cmd), "sh '%s/usbnet.sh' %s >>/tmp/usbnet.log 2>&1", dir, arg);
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
    cur_serial = (is_link_present(GADGET_DIR "/configs/c.1/acm.0") || is_link_present(GADGET_DIR "/configs/c.1/gser.0")) ? 1 : 0;
    {   /* saved network type, then override with whatever is actually linked right now */
        char tb[16] = "";
        FILE *tf = fopen(NETTYPE_FILE, "r");
        if (tf) { if (fgets(tb, sizeof(tb), tf)) { if (!strncmp(tb, "ncm", 3)) net_type = 1; else if (!strncmp(tb, "rndis", 5)) net_type = 2; else net_type = 0; } fclose(tf); }
        int ln = -1;
        if (is_link_present(GADGET_DIR "/configs/c.1/ecm.usb0")) { ln = 0; net_type = 0; }
        else if (is_link_present(GADGET_DIR "/configs/c.1/ncm.usb0")) { ln = 1; net_type = 1; }
        else if (is_link_present(GADGET_DIR "/configs/c.1/rndis.usb0")) { ln = 2; net_type = 2; }
        cur_net = ln >= 0 ? (access(NOSHARE_FLAG, F_OK) == 0 ? 2 : 1) : 0;
    }

    FILE *fp = fopen(BOOT_CFG_PATH, "r");
    if (fp) {
        /* line 1: adb_only | adb_ums_rw | adb_ums_ro | ums_rw | ums_ro | off   line 2: net | -   line 3: serial | - */
        char l1[64] = "", l2[64] = "", l3[64] = "";
        if (fgets(l1, sizeof(l1), fp) && fgets(l2, sizeof(l2), fp)) fgets(l3, sizeof(l3), fp);
        fclose(fp);
        boot_adb = !(strncmp(l1, "off", 3) == 0 || strncmp(l1, "ums_r", 5) == 0);
        boot_storage = strstr(l1, "ums_rw") ? 1 : (strstr(l1, "ums_ro") ? 2 : 0);
        boot_net = strncmp(l2, "net", 3) == 0;
        boot_serial = strncmp(l3, "serial", 6) == 0;
        if (boot_net && boot_serial) boot_serial = 0;
    }
}

static void save_boot_config(void) {
    mkdir("/mnt/SDCARD/.userdata", 0777);
    mkdir("/mnt/SDCARD/.userdata/m21", 0777);
    FILE *fp = fopen(BOOT_CFG_PATH, "w");
    if (!fp) return;
    const char *mode;
    if (boot_adb) mode = boot_storage == 1 ? "adb_ums_rw" : (boot_storage == 2 ? "adb_ums_ro" : "adb_only");
    else          mode = boot_storage == 1 ? "ums_rw" : (boot_storage == 2 ? "ums_ro" : "off");
    fprintf(fp, "%s\n%s\n%s\n", mode, boot_net ? "net" : "-", boot_serial ? "serial" : "-");
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
                    GADGET_DIR "/configs/c.1/acm.0 2>/dev/null");
    run_net("off");   /* removes ecm/ncm/rndis links, dhcp server, default route */

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
        run_net("serial on");   /* CDC-ACM: driver built into Windows/macOS/Linux; shows up as ttyGS0 here */
        if (!is_process_running("ttyGS0")) {
            system("(while :; do [ -c /dev/ttyGS0 ] && /bin/ash --login </dev/ttyGS0 >/dev/ttyGS0 2>&1; sleep 2; done) &");
        }
    }

    /* Configure USB Network (CDC-ECM) if enabled */
    int net_active = 0;
    if (cur_net) {
        {
            char na[32], lp[96];
            snprintf(na, sizeof(na), "on %s", net_type_name());
            run_net(na);
            snprintf(lp, sizeof(lp), GADGET_DIR "/configs/c.1/%s.usb0", net_type_name());
            net_active = is_link_present(lp);
        }
        if (!net_active) cur_net = 0;
    }

    /* Bind UDC if at least one function is enabled */
    if (cur_storage != STORAGE_OFF || cur_adb || cur_serial || cur_net) {
        /* ACM / ECM / NCM / RNDIS all use interface association descriptors: tell Windows (class EF/02/01) */
        if (cur_serial || cur_net) {
            system("echo 0xEF > " GADGET_DIR "/bDeviceClass; echo 0x02 > " GADGET_DIR "/bDeviceSubClass; echo 0x01 > " GADGET_DIR "/bDeviceProtocol");
        } else {
            system("echo 0x00 > " GADGET_DIR "/bDeviceClass; echo 0x00 > " GADGET_DIR "/bDeviceSubClass; echo 0x00 > " GADGET_DIR "/bDeviceProtocol");
        }
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
                 cur_net == 1 ? "NET 192.168.7.1 +inet" : (cur_net == 2 ? "NET 192.168.7.1 local" : "OFF"));
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
        int nm = 0, mounted = 0, failed = 0, ejected = 0;
        unsigned long long mb = 0;
        for (int i = 0; i < UH_MAX_MNT; i++)
            if (uh_mnts[i].active && !strcmp(uh_mnts[i].disk, u->disk)) {
                nm++; mounted += uh_mnts[i].mounted; mb += uh_mnts[i].mb;
                if (!uh_mnts[i].mounted && !strcmp(uh_mnts[i].err, "ejected")) ejected++;
                else if (!uh_mnts[i].mounted && uh_mnts[i].tries >= 5) failed++;
            }
        if (!nm) snprintf(extra, sizeof(extra), " - reading...");
        else if (mounted == nm) snprintf(extra, sizeof(extra), " - mounted (%llu MB)", mb);
        else if (ejected && !failed) snprintf(extra, sizeof(extra), " - ejected (safe to unplug)");
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

/* generic list page: title, rows (selectable if sel_enabled), footer, A/B button hints.
 * Returns when `done` callback condition is handled by caller; this only draws. */
static void host_draw_list(SDL_Surface *screen, int show_setting, const char *title,
                           char rows[][200], int n, int sel, const char *foot, char **btn_a) {
    GFX_clear(screen);
    GFX_blitHardwareGroup(screen, show_setting, 0);
    SDL_Surface *t = TTF_RenderUTF8_Blended(font.large, title, COLOR_GOLD);
    if (t) {
        SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), SCALE1(PADDING)});
        SDL_FreeSurface(t);
    }
    for (int i = 0; i < n; i++) {
        int y = SCALE1(PADDING + (i + 1) * PILL_SIZE);
        int avail_w = screen->w - SCALE1(PADDING * 2);
        char disp[200];
        int tw = GFX_truncateText(font.medium, rows[i], disp, avail_w, SCALE1(BUTTON_PADDING * 2));
        int mw = MIN(avail_w, tw);
        SDL_Color col = COLOR_WHITE;
        if (i == sel) {
            GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, mw, SCALE1(PILL_SIZE)});
            col = COLOR_BLACK;
        }
        SDL_Surface *txt = TTF_RenderUTF8_Blended(font.medium, disp, col);
        if (txt) {
            SDL_BlitSurface(txt, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), y + (SCALE1(PILL_SIZE) - txt->h) / 2});
            SDL_FreeSurface(txt);
        }
    }
    if (foot && foot[0]) {
        char fdisp[200];
        GFX_truncateText(font.small, foot, fdisp, screen->w - SCALE1(PADDING * 2 + BUTTON_PADDING * 2), 0);
        SDL_Surface *st = TTF_RenderUTF8_Blended(font.small, fdisp, COLOR_LIGHT_TEXT);
        if (st) {
            SDL_BlitSurface(st, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), screen->h - SCALE1(PADDING + PILL_SIZE + 22)});
            SDL_FreeSurface(st);
        }
    }
    if (btn_a) GFX_blitButtonGroup(btn_a, 0, screen, 0, 0);
    GFX_blitButtonGroup((char*[]){ "B", "BACK", NULL }, 1, screen, 1, 0);
    GFX_flip(screen);
}

static const char *host_ifc_name(unsigned c) {
    switch (c) {
        case 0x01: return "Audio"; case 0x02: return "CDC"; case 0x03: return "HID"; case 0x07: return "Printer";
        case 0x08: return "Storage"; case 0x09: return "Hub"; case 0x0a: return "CDC-Data"; case 0x0e: return "Video";
        case 0xe0: return "Wireless"; case 0xff: return "Vendor"; default: return "Other";
    }
}

/* read a sysfs attribute of a USB device into out (trimmed), "-" if missing/empty */
static void host_attr(const char *path, const char *attr, char *out, size_t n) {
    char p[128];
    snprintf(p, sizeof(p), UH_SYS_USB "/%s/%s", path, attr);
    uh_read_str(p, out, n);
    if (!out[0]) snprintf(out, n, "-");
}

/* details sub page for any USB device (read-only list, A/B to go back) */
static void host_show_details(SDL_Surface *screen, const UhDev *u) {
    enum { MAXR = 9 };
    char rows[MAXR][200];
    int n = 0, quit = 0, dirty = 1, show_setting = 0;
    char ver[16], pwr[16], ser[48], mf[48], dcl[8], nif[8];
    host_attr(u->path, "version", ver, sizeof(ver));
    host_attr(u->path, "bMaxPower", pwr, sizeof(pwr));
    host_attr(u->path, "serial", ser, sizeof(ser));
    host_attr(u->path, "manufacturer", mf, sizeof(mf));
    host_attr(u->path, "bDeviceClass", dcl, sizeof(dcl));
    host_attr(u->path, "bNumInterfaces", nif, sizeof(nif));

    snprintf(rows[n++], sizeof(rows[0]), "%s", u->name);
    snprintf(rows[n++], sizeof(rows[0]), "Type: %s%s   ID: %s", uh_cls_name(u->cls), u->cls == UC_INTERNAL ? " (built-in)" : "", u->vidpid);
    snprintf(rows[n++], sizeof(rows[0]), "Port: %s   Speed: %s Mbps   USB %s", u->path, u->speed[0] ? u->speed : "?", ver);
    snprintf(rows[n++], sizeof(rows[0]), "Vendor: %s   Power: %s", mf, pwr);
    snprintf(rows[n++], sizeof(rows[0]), "Serial: %s", ser);

    /* interfaces + bound drivers */
    {
        char ln[200] = "";
        DIR *d = opendir(UH_SYS_USB);
        struct dirent *e;
        char pre[40]; snprintf(pre, sizeof(pre), "%s:", u->path);
        int cnt = 0;
        while (d && (e = readdir(d))) {
            if (strncmp(e->d_name, pre, strlen(pre))) continue;
            char p[160], c[8], lk[160], drv[40] = "none";
            snprintf(p, sizeof(p), UH_SYS_USB "/%s/bInterfaceClass", e->d_name);
            uh_read_str(p, c, sizeof(c));
            snprintf(p, sizeof(p), UH_SYS_USB "/%s/driver", e->d_name);
            ssize_t k = readlink(p, lk, sizeof(lk) - 1);
            if (k > 0) { lk[k] = 0; const char *b = strrchr(lk, '/'); snprintf(drv, sizeof(drv), "%s", b ? b + 1 : lk); }
            size_t l = strlen(ln);
            snprintf(ln + l, sizeof(ln) - l, "%s%s/%s", cnt ? ", " : "", host_ifc_name((unsigned)strtoul(c, NULL, 16)), drv);
            cnt++;
        }
        if (d) closedir(d);
        snprintf(rows[n++], sizeof(rows[0]), "Interfaces(%s): %s", nif, cnt ? ln : "-");
    }

    if (u->cls == UC_STORAGE) {
        int found = 0;
        for (int i = 0; i < UH_MAX_MNT && n < MAXR; i++) {
            if (!uh_mnts[i].active || strcmp(uh_mnts[i].disk, u->disk)) continue;
            found = 1;
            if (uh_mnts[i].mounted)
                snprintf(rows[n++], sizeof(rows[0]), "%s [%s %llu MB] -> %s", uh_mnts[i].dev, uh_mnts[i].fs, uh_mnts[i].mb, uh_mnts[i].mnt);
            else
                snprintf(rows[n++], sizeof(rows[0]), "%s: not mounted (%s)", uh_mnts[i].dev, uh_mnts[i].err[0] ? uh_mnts[i].err : "pending");
        }
        if (!found && n < MAXR) snprintf(rows[n++], sizeof(rows[0]), "no partitions detected yet");
    } else if (u->cls == UC_NET && n < MAXR) {
        snprintf(rows[n++], sizeof(rows[0]), "Network adapter: no driver for it in this kernel unless listed above");
    } else if (u->cls == UC_HUB && n < MAXR) {
        snprintf(rows[n++], sizeof(rows[0]), "Hub: devices behind it are listed on the previous page");
    }
    (void)dcl;
    while (!quit) {
        GFX_startFrame();
        PAD_poll();
        if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_A)) quit = 1;
        PWR_update(&dirty, &show_setting, NULL, NULL);
        if (dirty) {
            host_draw_list(screen, show_setting, "Device details", rows, n, -1, "", NULL);
            dirty = 0;
        } else GFX_sync();
    }
}

/* A on a storage row: small action menu. Returns 1 if the page should refresh immediately. */
static int host_storage_menu(SDL_Surface *screen, const UhDev *u, char *hint, size_t hint_n) {
    int sel = 0, quit = 0, dirty = 1, show_setting = 0, refresh = 0;
    while (!quit) {
        int mounted_n = 0, total_n = 0;
        for (int i = 0; i < UH_MAX_MNT; i++)
            if (uh_mnts[i].active && !strcmp(uh_mnts[i].disk, u->disk)) { total_n++; mounted_n += uh_mnts[i].mounted; }
        char rows[4][200];
        snprintf(rows[0], sizeof(rows[0]), "Eject (unmount, safe to unplug)%s", mounted_n ? "" : "  - nothing mounted");
        snprintf(rows[1], sizeof(rows[1]), "Mount / retry%s", (total_n && mounted_n == total_n) ? "  - already mounted" : "");
        snprintf(rows[2], sizeof(rows[2]), "Details (mount path, filesystem, size)");
        snprintf(rows[3], sizeof(rows[3]), "Cancel");

        GFX_startFrame();
        PAD_poll();
        if (PAD_justRepeated(BTN_UP) || PAD_justPressed(BTN_UP)) { sel = (sel + 3) % 4; dirty = 1; }
        else if (PAD_justRepeated(BTN_DOWN) || PAD_justPressed(BTN_DOWN)) { sel = (sel + 1) % 4; dirty = 1; }
        else if (PAD_justPressed(BTN_B)) quit = 1;
        else if (PAD_justPressed(BTN_A)) {
            if (sel == 0) {
                if (mounted_n) {
                    char cmd[40]; snprintf(cmd, sizeof(cmd), "eject %s", u->disk); uh_cmd_send(cmd);
                    snprintf(hint, hint_n, "Ejected %s - safe to unplug (A > Mount to reuse)", u->disk);
                    refresh = 1; quit = 1;
                } else snprintf(hint, hint_n, "%s: nothing is mounted", u->disk), quit = 1;
            } else if (sel == 1) {
                char cmd[40]; snprintf(cmd, sizeof(cmd), "retry %s", u->disk); uh_cmd_send(cmd);
                snprintf(hint, hint_n, "Mounting %s...", u->disk);
                refresh = 1; quit = 1;
            } else if (sel == 2) {
                host_show_details(screen, u);
                dirty = 1;
            } else quit = 1;
        }
        PWR_update(&dirty, &show_setting, NULL, NULL);
        if (dirty) {
            char title[80]; snprintf(title, sizeof(title), "%s", u->name);
            host_draw_list(screen, show_setting, title, rows, 4, sel, "", (char*[]){ "U/D", "SELECT", "A", "OK", NULL });
            dirty = 0;
        } else GFX_sync();
    }
    return refresh;
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
                    UhDev ucopy = *u;   /* list may refresh while the menu is open */
                    if (host_storage_menu(screen, &ucopy, hint, sizeof(hint))) last_ms = 0;
                    dirty = 1;
                } else {
                    UhDev ucopy = *u;
                    host_show_details(screen, &ucopy);
                    dirty = 1;
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

            GFX_blitButtonGroup((char*[]){ "U/D", "SELECT", "L/R/A", is_dev_row ? (uh_devs[sel - 1].cls == UC_STORAGE ? "ACTIONS" : "DETAILS") : "CHANGE", NULL }, 0, screen, 0, 0);
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

/* ------------------------------------------------------------------------- */
/* Generic scrolling list page + sub pages (USB Network, Boot Defaults, Help) */
/* ------------------------------------------------------------------------- */

extern int TTF_FontLineSkip(const TTF_Font *font);   /* not declared by the bundled SDL_ttf header */

#define UI_VIS      5     /* rows that fit between the title and the button hints (320x240 logical) */
#define UI_ROW_LEN  160

static void ui_draw_list(SDL_Surface *screen, int show_setting, const char *title_txt,
                         char rows[][UI_ROW_LEN], int n, int sel, int *top,
                         const char *foot, const char *a_label) {
    if (sel < *top) *top = sel;
    if (sel >= *top + UI_VIS) *top = sel - UI_VIS + 1;
    if (*top > n - UI_VIS) *top = n - UI_VIS;
    if (*top < 0) *top = 0;

    GFX_clear(screen);
    GFX_blitHardwareGroup(screen, show_setting, 0);

    char tt[96];
    if (n > UI_VIS) snprintf(tt, sizeof(tt), "%s  (%d/%d)", title_txt, sel + 1, n);
    else snprintf(tt, sizeof(tt), "%s", title_txt);
    SDL_Surface *title = TTF_RenderUTF8_Blended(font.large, tt, COLOR_GOLD);
    if (title) {
        SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), SCALE1(PADDING)});
        SDL_FreeSurface(title);
    }

    for (int k = 0; k < UI_VIS && *top + k < n; k++) {
        int i = *top + k;
        int y = SCALE1(PADDING + (k + 1) * PILL_SIZE);
        int avail_w = screen->w - SCALE1(PADDING * 2);
        char disp[UI_ROW_LEN];
        int tw = GFX_truncateText(font.medium, rows[i], disp, avail_w, SCALE1(BUTTON_PADDING * 2));
        int mw = MIN(avail_w, tw);
        SDL_Color col = COLOR_WHITE;
        if (i == sel) {
            GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, mw, SCALE1(PILL_SIZE)});
            col = COLOR_BLACK;
        }
        SDL_Surface *txt = TTF_RenderUTF8_Blended(font.medium, disp, col);
        if (txt) {
            SDL_BlitSurface(txt, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), y + (SCALE1(PILL_SIZE) - txt->h) / 2});
            SDL_FreeSurface(txt);
        }
    }

    if (foot && *foot) {
        char fd[200];
        GFX_truncateText(font.tiny, foot, fd, screen->w - SCALE1(PADDING * 2 + BUTTON_PADDING * 2), 0);
        SDL_Surface *ft = TTF_RenderUTF8_Blended(font.tiny, fd, COLOR_LIGHT_TEXT);
        if (ft) {
            SDL_BlitSurface(ft, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), SCALE1(PADDING + (UI_VIS + 1) * PILL_SIZE + 1)});
            SDL_FreeSurface(ft);
        }
    }

    GFX_blitButtonGroup((char*[]){ "U/D", "SELECT", "L/R/A", (char*)a_label, NULL }, 0, screen, 0, 0);
    GFX_blitButtonGroup((char*[]){ "B", "BACK", NULL }, 1, screen, 1, 0);
    GFX_flip(screen);
}

/* word-wrap `text` ('\n' separated paragraphs, leading spaces kept as indent) */
static int wrap_text(TTF_Font *f, const char *text, int max_w, char lines[][200], int max_lines) {
    int nl = 0;
    const char *p = text;
    while (*p && nl < max_lines) {
        const char *e = strchr(p, '\n');
        int plen = e ? (int)(e - p) : (int)strlen(p);
        char para[400];
        if (plen > (int)sizeof(para) - 1) plen = sizeof(para) - 1;
        memcpy(para, p, plen); para[plen] = 0;
        p += (e ? (e - p) + 1 : plen);

        int ind = 0;
        while (para[ind] == ' ') ind++;
        char prefix[16] = "";
        if (ind > 12) ind = 12;
        memset(prefix, ' ', ind); prefix[ind] = 0;
        if (!para[ind]) { lines[nl][0] = 0; nl++; continue; }

        char cur[200]; snprintf(cur, sizeof(cur), "%s", prefix);
        char *save = NULL, *w = strtok_r(para + ind, " ", &save);
        int first = 1;
        while (w && nl < max_lines) {
            char cand[200];
            snprintf(cand, sizeof(cand), first ? "%s%s" : "%s %s", cur, w);
            int tw = 0;
            TTF_SizeUTF8(f, cand, &tw, NULL);
            if (tw > max_w && !first) {
                snprintf(lines[nl++], 200, "%s", cur);
                snprintf(cur, sizeof(cur), "%s  %s", prefix, w);   /* continuation indent */
                first = 0;
            } else {
                snprintf(cur, sizeof(cur), "%s", cand);
                first = 0;
            }
            w = strtok_r(NULL, " ", &save);
        }
        if (nl < max_lines) snprintf(lines[nl++], 200, "%s", cur);
    }
    return nl;
}

enum { OS_WINDOWS = 0, OS_MACOS = 1, OS_LINUX = 2, OS_COUNT };

static const char *os_name(int os) { return os == OS_WINDOWS ? "Windows" : (os == OS_MACOS ? "macOS" : "Linux"); }

static const char *help_text(int os) {
    if (os == OS_WINDOWS)
        return
        "Use Network Type NCM (Windows 11) or RNDIS (Windows 10/11). ECM has no Windows driver.\n"
        "Windows gets 192.168.7.2 automatically, the device is 192.168.7.1.\n"
        "\n"
        "Share the PC's Internet with the device:\n"
        " 1. Plug in USB, set USB Network to ON (+Internet).\n"
        " 2. Copy share-internet-windows.bat and share-internet-windows.ps1 from the SD card root to the PC (turn on USB Storage, or read the card directly).\n"
        " 3. Double-click share-internet-windows.bat and allow the administrator prompt. The script finds the USB adapter and shares your Wi-Fi/Ethernet with it.\n"
        " 4. On the device: adb shell, then ping 8.8.8.8 and ping www.baidu.com.\n"
        "\n"
        "Run the .bat again after changing Network Type or re-plugging the cable (Windows creates a new adapter). To stop: share-internet-windows.bat off\n"
        "\n"
        "If it does not work, run usb-diag-windows.bat (SD card root) and look at usb-diag.txt.\n"
        "\n"
        "USB Serial (ACM) appears as a COM port in Device Manager (Ports).";
    if (os == OS_MACOS)
        return
        "Use Network Type ECM or NCM. RNDIS is not supported on macOS.\n"
        "A new network interface appears (System Settings > Network). The Mac gets 192.168.7.2 automatically, the device is 192.168.7.1.\n"
        "\n"
        "Share the Mac's Internet with the device:\n"
        " 1. Plug in USB, set USB Network to ON (+Internet).\n"
        " 2. Copy share-internet-macos.sh from the SD card root to the Mac.\n"
        " 3. In Terminal run:\n"
        "      sudo sh share-internet-macos.sh on\n"
        "    (when the uplink is not auto-detected, e.g. with a VPN: sudo sh share-internet-macos.sh on en0)\n"
        " 4. On the device: adb shell, then ping 8.8.8.8\n"
        "\n"
        "Run it again after changing Network Type or re-plugging. To stop:\n"
        "      sudo sh share-internet-macos.sh off\n"
        "\n"
        "USB Serial (ACM) appears as /dev/cu.usbmodem*; use: screen /dev/cu.usbmodem* 115200";
    return
        "Use Network Type ECM or NCM (RNDIS also works). The PC gets 192.168.7.2 automatically, the device is 192.168.7.1.\n"
        "\n"
        "Share the PC's Internet with the device:\n"
        " 1. Plug in USB, set USB Network to ON (+Internet).\n"
        " 2. Copy share-internet-linux.sh from the SD card root to the PC.\n"
        " 3. In a terminal run:\n"
        "      sudo sh share-internet-linux.sh on\n"
        "    (or name the uplink interface: sudo sh share-internet-linux.sh on wlan0)\n"
        " 4. On the device: adb shell, then ping 8.8.8.8\n"
        "\n"
        "To stop:\n"
        "      sudo sh share-internet-linux.sh off\n"
        "\n"
        "USB Serial (ACM) appears as /dev/ttyACM0; use: screen /dev/ttyACM0 115200";
}

/* make sure all host scripts are on the SD card root (copy from the pak when missing / outdated) */
static void ensure_host_tools(void) {
    const char *dir = getenv("USBMODE_PAK_DIR");
    char cmd[768];
    if (!dir || !*dir) dir = "/mnt/SDCARD/Tools/m21/USB Mode.pak";
    snprintf(cmd, sizeof(cmd),
             "for f in share-internet-macos.sh share-internet-linux.sh share-internet-windows.bat "
             "share-internet-windows.ps1 usb-diag.sh usb-diag-windows.bat; do "
             "s='%s/host-tools/'$f; d=/mnt/SDCARD/$f; [ -f \"$s\" ] || continue; "
             "cmp -s \"$s\" \"$d\" 2>/dev/null || cp \"$s\" \"$d\"; done 2>/dev/null", dir);
    system(cmd);
}

static void run_help_page(SDL_Surface *screen, int os) {
    static char lines[96][200];
    int text_w = screen->w - SCALE1(PADDING * 2 + BUTTON_PADDING * 2);
    int nl = wrap_text(font.tiny, help_text(os), text_w, lines, 96);
    int lh = TTF_FontLineSkip(font.tiny);
    if (lh < 8) lh = 12;
    int y0 = SCALE1(PADDING + PILL_SIZE + 2);
    int y1 = screen->h - SCALE1(PADDING + PILL_SIZE) - SCALE1(2);
    int vis = (y1 - y0) / lh;
    if (vis < 1) vis = 1;
    int top = 0, maxtop = nl > vis ? nl - vis : 0;
    int dirty = 1, show_setting = 0, quit = 0;

    while (!quit) {
        GFX_startFrame();
        PAD_poll();
        if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_A)) quit = 1;
        else if (PAD_justRepeated(BTN_UP) || PAD_justPressed(BTN_UP)) { if (top > 0) { top--; dirty = 1; } }
        else if (PAD_justRepeated(BTN_DOWN) || PAD_justPressed(BTN_DOWN)) { if (top < maxtop) { top++; dirty = 1; } }
        else if (PAD_justPressed(BTN_LEFT)) { top = MAX(0, top - vis + 1); dirty = 1; }
        else if (PAD_justPressed(BTN_RIGHT)) { top = MIN(maxtop, top + vis - 1); dirty = 1; }

        PWR_update(&dirty, &show_setting, NULL, NULL);
        if (dirty) {
            GFX_clear(screen);
            GFX_blitHardwareGroup(screen, show_setting, 0);
            char tt[96];
            snprintf(tt, sizeof(tt), "Connect: %s", os_name(os));
            if (maxtop > 0) { char t2[96]; snprintf(t2, sizeof(t2), "%s  (%d/%d)", tt, top + 1, maxtop + 1); snprintf(tt, sizeof(tt), "%s", t2); }
            SDL_Surface *title = TTF_RenderUTF8_Blended(font.large, tt, COLOR_GOLD);
            if (title) {
                SDL_BlitSurface(title, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), SCALE1(PADDING)});
                SDL_FreeSurface(title);
            }
            for (int k = 0; k < vis && top + k < nl; k++) {
                const char *ln = lines[top + k];
                if (!*ln) continue;
                SDL_Surface *t = TTF_RenderUTF8_Blended(font.tiny, ln, COLOR_WHITE);
                if (t) {
                    SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){SCALE1(PADDING + BUTTON_PADDING), y0 + k * lh});
                    SDL_FreeSurface(t);
                }
            }
            GFX_blitButtonGroup((char*[]){ "U/D", "SCROLL", "L/R", "PAGE", NULL }, 0, screen, 0, 0);
            GFX_blitButtonGroup((char*[]){ "B", "BACK", NULL }, 1, screen, 1, 0);
            GFX_flip(screen);
            dirty = 0;
        } else GFX_sync();
    }
}

/* switch network mode (0 off / 1 +internet / 2 local only) and reconfigure the gadget */
static void net_set_mode(SDL_Surface *screen, int mode) {
    cur_net = mode;
    if (cur_net && cur_serial) cur_serial = 0;   /* exclusive: one interrupt endpoint on this UDC */
    if (cur_net == 2) {
        mkdir("/mnt/SDCARD/.userdata", 0777);
        mkdir("/mnt/SDCARD/.userdata/m21", 0777);
        FILE *nf = fopen(NOSHARE_FLAG, "w");
        if (nf) fclose(nf);
    } else {
        unlink(NOSHARE_FLAG);
    }
    cur_role = ROLE_DEVICE;
    GFX_clear(screen);
    GFX_blitMessage(font.large, "Applying USB Network...", screen, &(SDL_Rect){0, 0, screen->w, screen->h});
    GFX_flip(screen);
    apply_usb_config();
}

static const char *net_mode_label(void) {
    return cur_net == 1 ? "< ON (+Internet via PC) >" : (cur_net == 2 ? "< ON (Local only) >" : "< OFF >");
}

static void run_net_page(SDL_Surface *screen) {
    enum { R_MODE = 0, R_TYPE, R_HELP, R_BACK, R_COUNT };
    int sel = 0, top = 0, os = OS_WINDOWS, dirty = 1, show_setting = 0, quit = 0;
    char foot[200] = "Mode: ON(+Internet) = route via PC. ON(Local only) = no gateway.";
    ensure_host_tools();

    while (!quit) {
        GFX_startFrame();
        PAD_poll();
        int left = PAD_justPressed(BTN_LEFT), right = PAD_justPressed(BTN_RIGHT), a = PAD_justPressed(BTN_A);
        if (PAD_justPressed(BTN_B)) quit = 1;
        else if (PAD_justRepeated(BTN_UP) || PAD_justPressed(BTN_UP)) { sel = (sel + R_COUNT - 1) % R_COUNT; dirty = 1; }
        else if (PAD_justRepeated(BTN_DOWN) || PAD_justPressed(BTN_DOWN)) { sel = (sel + 1) % R_COUNT; dirty = 1; }
        else if (left || right || a) {
            int dir = left ? -1 : 1;
            dirty = 1;
            switch (sel) {
                case R_MODE:
                    net_set_mode(screen, (cur_net + dir + 3) % 3);
                    snprintf(foot, sizeof(foot), "%s", status_msg);
                    break;
                case R_TYPE:
                    net_type = (net_type + dir + 3) % 3;
                    save_net_type();
                    if (cur_net) { cur_role = ROLE_DEVICE; apply_usb_config(); }
                    snprintf(foot, sizeof(foot), "Type %s. Re-run the share script on the PC after switching.", net_type_name());
                    break;
                case R_HELP:
                    if (a) run_help_page(screen, os);
                    else os = (os + dir + OS_COUNT) % OS_COUNT;
                    break;
                case R_BACK:
                    if (a) quit = 1;
                    break;
            }
        }

        PWR_update(&dirty, &show_setting, NULL, NULL);
        if (dirty) {
            char rows[R_COUNT][UI_ROW_LEN];
            snprintf(rows[R_MODE], UI_ROW_LEN, "Mode: %s", net_mode_label());
            snprintf(rows[R_TYPE], UI_ROW_LEN, "Network Type: < %s >", net_type_label());
            snprintf(rows[R_HELP], UI_ROW_LEN, "How to connect: < %s >  (A: open)", os_name(os));
            snprintf(rows[R_BACK], UI_ROW_LEN, "[ Back ]");
            const char *f = foot;
            if (sel == R_TYPE) f = "ECM: Mac/Linux  NCM: Mac/Linux/Win11  RNDIS: Windows (Mac: no)";
            else if (sel == R_HELP) f = "Scripts to share the PC's Internet are in the SD card root";
            ui_draw_list(screen, show_setting, "USB Network", rows, R_COUNT, sel, &top, f, "CHANGE");
            dirty = 0;
        } else GFX_sync();
    }
}

static void run_boot_page(SDL_Surface *screen) {
    enum { B_ADB = 0, B_STORAGE, B_SERIAL, B_NET, B_TYPE, B_BACK, B_COUNT };
    int sel = 0, top = 0, dirty = 1, show_setting = 0, quit = 0;
    char foot[200] = "Choose what USB turns on at every boot. Saved automatically.";

    while (!quit) {
        GFX_startFrame();
        PAD_poll();
        int left = PAD_justPressed(BTN_LEFT), right = PAD_justPressed(BTN_RIGHT), a = PAD_justPressed(BTN_A);
        if (PAD_justPressed(BTN_B)) quit = 1;
        else if (PAD_justRepeated(BTN_UP) || PAD_justPressed(BTN_UP)) { sel = (sel + B_COUNT - 1) % B_COUNT; dirty = 1; }
        else if (PAD_justRepeated(BTN_DOWN) || PAD_justPressed(BTN_DOWN)) { sel = (sel + 1) % B_COUNT; dirty = 1; }
        else if (left || right || a) {
            int dir = left ? -1 : 1;
            dirty = 1;
            switch (sel) {
                case B_ADB:
                    boot_adb = !boot_adb;
                    snprintf(foot, sizeof(foot), boot_adb ? "ADB will start at boot" : "ADB will NOT start at boot (use with care)");
                    break;
                case B_STORAGE:
                    boot_storage = (boot_storage + dir + 3) % 3;
                    snprintf(foot, sizeof(foot), "SD card shown to the PC at boot: %s",
                             boot_storage == 1 ? "read-write" : (boot_storage == 2 ? "read-only" : "no"));
                    break;
                case B_SERIAL:
                    boot_serial = !boot_serial;
                    if (boot_serial && boot_net) { boot_net = 0; snprintf(foot, sizeof(foot), "Serial ON: USB Network turned off (they exclude each other)"); }
                    else snprintf(foot, sizeof(foot), boot_serial ? "Serial console (ttyGS0) at boot" : "Serial off at boot");
                    break;
                case B_NET:
                    boot_net = !boot_net;
                    if (boot_net && boot_serial) { boot_serial = 0; snprintf(foot, sizeof(foot), "Network ON: USB Serial turned off (they exclude each other)"); }
                    else snprintf(foot, sizeof(foot), boot_net ? "USB Network at boot (sharing: see USB Network page)" : "USB Network off at boot");
                    break;
                case B_TYPE:
                    net_type = (net_type + dir + 3) % 3;
                    save_net_type();
                    snprintf(foot, sizeof(foot), "Network type: %s", net_type_label());
                    break;
                case B_BACK:
                    if (a) quit = 1;
                    break;
            }
            save_boot_config();
        }

        PWR_update(&dirty, &show_setting, NULL, NULL);
        if (dirty) {
            char rows[B_COUNT][UI_ROW_LEN];
            snprintf(rows[B_ADB], UI_ROW_LEN, "[%c] ADB Debugging", boot_adb ? 'x' : ' ');
            snprintf(rows[B_STORAGE], UI_ROW_LEN, "[%c] USB Storage: %s", boot_storage ? 'x' : ' ',
                     boot_storage == 1 ? "< Read-Write >" : (boot_storage == 2 ? "< Read-Only >" : "< Off >"));
            snprintf(rows[B_SERIAL], UI_ROW_LEN, "[%c] USB Serial (CDC-ACM)", boot_serial ? 'x' : ' ');
            snprintf(rows[B_NET], UI_ROW_LEN, "[%c] USB Network", boot_net ? 'x' : ' ');
            snprintf(rows[B_TYPE], UI_ROW_LEN, "      Network Type: < %s >", net_type_label());
            snprintf(rows[B_BACK], UI_ROW_LEN, "[ Back ]");
            ui_draw_list(screen, show_setting, "Boot Defaults", rows, B_COUNT, sel, &top, foot, "TOGGLE");
            dirty = 0;
        } else GFX_sync();
    }
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
    int top = 0;
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
            int is_a = PAD_justPressed(BTN_A);
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
                    if (cur_serial && cur_net) cur_net = 0;   /* UDC has one interrupt endpoint: serial and network are exclusive */
                    apply_usb_config();
                    if (cur_serial) snprintf(status_msg, sizeof(status_msg), "Serial (CDC-ACM) ON; Network turned off (not enough endpoints)");
                    break;
                case ITEM_NETWORK:
                    if (is_a) run_net_page(screen);
                    else net_set_mode(screen, (cur_net + dir + 3) % 3);
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
                case ITEM_BOOT:
                    if (is_a) run_boot_page(screen);
                    break;
                case ITEM_EXIT:
                    if (is_a) quit = 1;
                    break;
            }
        } else if (PAD_justPressed(BTN_B)) {
            quit = 1;
        }

        PWR_update(&dirty, &show_setting, NULL, NULL);

        if (dirty) {
            char rows[ITEM_COUNT][UI_ROW_LEN];
            snprintf(rows[ITEM_STORAGE], UI_ROW_LEN, "USB Storage (SD Card): %s",
                     cur_storage == STORAGE_RW ? "< ON (Read-Write) >" :
                     (cur_storage == STORAGE_RO ? "< ON (Read-Only) >" : "< OFF >"));
            snprintf(rows[ITEM_ADB], UI_ROW_LEN, "ADB Debugging: %s", cur_adb ? "< ON >" : "< OFF >");
            snprintf(rows[ITEM_SERIAL], UI_ROW_LEN, "USB Serial (CDC-ACM): %s", cur_serial ? "< ON >" : "< OFF >");
            snprintf(rows[ITEM_NETWORK], UI_ROW_LEN, "USB Network: %s  (A: more)",
                     cur_net == 1 ? "< ON +Internet >" : (cur_net == 2 ? "< ON Local >" : "< OFF >"));
            snprintf(rows[ITEM_ROLE], UI_ROW_LEN, "USB Host (OTG): %s",
                     cur_role == ROLE_HOST ? "< ON >  (A: devices)" : "< OFF >  (A: open)");
            snprintf(rows[ITEM_KEEP_BG], UI_ROW_LEN, "On Exit to MinUI: %s",
                     keep_bg ? "< Keep Storage >" : "< Auto Eject >");
            snprintf(rows[ITEM_BOOT], UI_ROW_LEN, "Boot Defaults  (A: choose)");
            snprintf(rows[ITEM_EXIT], UI_ROW_LEN, "[ Return to MinUI ]");
            ui_draw_list(screen, show_setting, "USB Mode Manager", rows, ITEM_COUNT, selected, &top,
                         status_msg, "CHANGE");
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
