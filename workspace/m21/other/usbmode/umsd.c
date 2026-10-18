#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>

extern int64_t lseek64(int fd, int64_t offset, int whence);
extern ssize_t pread64(int fd, void *buf, size_t count, int64_t offset);
extern ssize_t pwrite64(int fd, const void *buf, size_t count, int64_t offset);

#ifndef O_LARGEFILE
#define O_LARGEFILE 0400000
#endif

/* FunctionFS definitions */
#define FUNCTIONFS_DESCRIPTORS_MAGIC 1
#define FUNCTIONFS_STRINGS_MAGIC     2

#define FUNCTIONFS_BIND      0
#define FUNCTIONFS_UNBIND    1
#define FUNCTIONFS_ENABLE    2
#define FUNCTIONFS_DISABLE   3
#define FUNCTIONFS_SETUP     4
#define FUNCTIONFS_SUSPEND   5
#define FUNCTIONFS_RESUME    6

struct usb_ctrlrequest {
    uint8_t  bRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

struct usb_functionfs_event {
    union {
        struct usb_ctrlrequest setup;
    } u;
    uint8_t type;
    uint8_t _pad[3];
} __attribute__((packed));

struct usb_interface_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor_no_audio {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));

struct ffs_descriptors {
    uint32_t magic;
    uint32_t length;
    uint32_t fs_count;
    uint32_t hs_count;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio out;
        struct usb_endpoint_descriptor_no_audio in;
    } __attribute__((packed)) fs_descs, hs_descs;
} __attribute__((packed));

#define STR_INTERFACE "USB Mass Storage"
struct ffs_strings {
    uint32_t magic;
    uint32_t length;
    uint32_t str_count;
    uint32_t lang_count;
    uint16_t code;
    char str1[sizeof(STR_INTERFACE)];
} __attribute__((packed));

static const struct ffs_descriptors descriptors = {
    .magic = FUNCTIONFS_DESCRIPTORS_MAGIC,
    .length = sizeof(descriptors),
    .fs_count = 3,
    .hs_count = 3,
    .fs_descs = {
        .intf = {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = 0x04,
            .bInterfaceNumber = 0,
            .bAlternateSetting = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = 0x08,
            .bInterfaceSubClass = 0x06,
            .bInterfaceProtocol = 0x50,
            .iInterface = 1,
        },
        .out = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = 0x05,
            .bEndpointAddress = 0x01,
            .bmAttributes = 0x02,
            .wMaxPacketSize = 64,
            .bInterval = 0,
        },
        .in = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = 0x05,
            .bEndpointAddress = 0x82,
            .bmAttributes = 0x02,
            .wMaxPacketSize = 64,
            .bInterval = 0,
        },
    },
    .hs_descs = {
        .intf = {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = 0x04,
            .bInterfaceNumber = 0,
            .bAlternateSetting = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = 0x08,
            .bInterfaceSubClass = 0x06,
            .bInterfaceProtocol = 0x50,
            .iInterface = 1,
        },
        .out = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = 0x05,
            .bEndpointAddress = 0x01,
            .bmAttributes = 0x02,
            .wMaxPacketSize = 512,
            .bInterval = 0,
        },
        .in = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = 0x05,
            .bEndpointAddress = 0x82,
            .bmAttributes = 0x02,
            .wMaxPacketSize = 512,
            .bInterval = 0,
        },
    },
};

static const struct ffs_strings strings = {
    .magic = FUNCTIONFS_STRINGS_MAGIC,
    .length = sizeof(strings),
    .str_count = 1,
    .lang_count = 1,
    .code = 0x0409,
    .str1 = STR_INTERFACE,
};

#define CBW_SIGNATURE 0x43425355U
#define CSW_SIGNATURE 0x53425355U

struct bot_cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_len;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cb_len;
    uint8_t  cdb[16];
} __attribute__((packed));

struct bot_csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t  status;
} __attribute__((packed));

struct linux_input_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};

static int ep0_fd = -1;
static int ep_out_fd = -1;
static int ep_in_fd = -1;
static int disk_fd = -1;
static pid_t ep0_pid = -1;
static pid_t input_pid = -1;
static uint64_t disk_size = 0;
static uint64_t num_blocks = 0;
static bool read_only = false;
static bool ejected = false;
static bool exit_on_eject = false;
static volatile sig_atomic_t stop_requested = 0;

static uint8_t sense_key = 0;
static uint8_t sense_asc = 0;
static uint8_t sense_ascq = 0;

static void sig_handler(int sig) {
    (void)sig;
    stop_requested = 1;
    if (disk_fd >= 0) fsync(disk_fd);
    if (ep0_pid > 0) kill(ep0_pid, SIGTERM);
    if (input_pid > 0) kill(input_pid, SIGTERM);
    _exit(0);
}

static void set_sense(uint8_t key, uint8_t asc, uint8_t ascq) {
    sense_key = key;
    sense_asc = asc;
    sense_ascq = ascq;
}

static inline void put_be16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xff;
    p[1] = v & 0xff;
}
static inline void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}
static inline void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)(v & 0xffffffffULL));
}
static inline uint16_t get_be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
static inline uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint64_t get_be64(const uint8_t *p) {
    return ((uint64_t)get_be32(p) << 32) | get_be32(p + 4);
}

static ssize_t write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        left -= n;
    }
    return (ssize_t)len;
}

static ssize_t read_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        p += n;
        left -= n;
    }
    return (ssize_t)(len - left);
}

static uint8_t io_buf[65536] __attribute__((aligned(4096)));

static int handle_scsi_data_in(const struct bot_cbw *cbw, const void *data, size_t data_len, uint32_t *residue_out) {
    uint32_t host_len = cbw->data_len;
    size_t to_send = data_len < host_len ? data_len : host_len;
    if (to_send > 0) {
        if (write_all(ep_in_fd, data, to_send) < 0) return -1;
    } else if (host_len > 0) {
        memset(io_buf, 0, host_len < 512 ? host_len : 512);
        size_t pad = host_len < 36 ? host_len : 1;
        write_all(ep_in_fd, io_buf, pad);
    }
    *residue_out = host_len - (uint32_t)to_send;
    return 0;
}

static int handle_cbw(const struct bot_cbw *cbw) {
    struct bot_csw csw __attribute__((aligned(64)));
    uint32_t residue = cbw->data_len;
    csw.signature = CSW_SIGNATURE;
    csw.tag = cbw->tag;
    csw.status = 0;

    const uint8_t *cdb = cbw->cdb;
    uint8_t op = cdb[0];
    uint8_t resp[512] __attribute__((aligned(64)));
    memset(resp, 0, sizeof(resp));

    switch (op) {
    case 0x00: /* TEST UNIT READY */
        if (ejected || disk_fd < 0) {
            set_sense(0x02, 0x3A, 0x00);
            csw.status = 1;
        }
        residue = 0;
        break;

    case 0x12: /* INQUIRY */
        if (cdb[1] & 0x01) {
            uint8_t page = cdb[2];
            if (page == 0x00) {
                resp[0] = 0x00;
                resp[1] = 0x00;
                resp[3] = 3;
                resp[4] = 0x00;
                resp[5] = 0x80;
                resp[6] = 0x83;
                handle_scsi_data_in(cbw, resp, 7, &residue);
            } else if (page == 0x80) {
                resp[0] = 0x00;
                resp[1] = 0x80;
                resp[3] = 8;
                memcpy(&resp[4], "20080411", 8);
                handle_scsi_data_in(cbw, resp, 12, &residue);
            } else if (page == 0x83) {
                resp[0] = 0x00;
                resp[1] = 0x83;
                resp[3] = 12;
                resp[4] = 0x02;
                resp[5] = 0x01;
                resp[7] = 8;
                memcpy(&resp[8], "20080411", 8);
                handle_scsi_data_in(cbw, resp, 16, &residue);
            } else {
                set_sense(0x05, 0x24, 0x00);
                handle_scsi_data_in(cbw, resp, 0, &residue);
                csw.status = 1;
            }
        } else {
            resp[0] = 0x00;
            resp[1] = 0x80;
            resp[2] = 0x04;
            resp[3] = 0x02;
            resp[4] = 31;
            memcpy(&resp[8],  "Allwin  ", 8);
            memcpy(&resp[16], "H133 SD Storage ", 16);
            memcpy(&resp[32], "0100", 4);
            handle_scsi_data_in(cbw, resp, 36, &residue);
        }
        break;

    case 0x03: /* REQUEST SENSE */
        resp[0] = 0x70;
        resp[2] = sense_key;
        resp[7] = 10;
        resp[12] = sense_asc;
        resp[13] = sense_ascq;
        sense_key = 0;
        sense_asc = 0;
        sense_ascq = 0;
        handle_scsi_data_in(cbw, resp, 18, &residue);
        break;

    case 0x23: /* READ FORMAT CAPACITIES */
        resp[3] = 8;
        put_be32(&resp[4], num_blocks > 0xffffffffULL ? 0xffffffffU : (uint32_t)num_blocks);
        resp[8] = ejected ? 0x03 : 0x02;
        resp[9] = 0x00;
        resp[10] = 0x02;
        resp[11] = 0x00;
        handle_scsi_data_in(cbw, resp, 12, &residue);
        break;

    case 0x25: /* READ CAPACITY (10) */
        if (ejected || disk_fd < 0) {
            set_sense(0x02, 0x3A, 0x00);
            handle_scsi_data_in(cbw, resp, 8, &residue);
            csw.status = 1;
        } else {
            uint32_t max_lba = (num_blocks - 1) > 0xffffffffULL ? 0xffffffffU : (uint32_t)(num_blocks - 1);
            put_be32(&resp[0], max_lba);
            put_be32(&resp[4], 512);
            handle_scsi_data_in(cbw, resp, 8, &residue);
        }
        break;

    case 0x9E: /* READ CAPACITY (16) */
        if ((cdb[1] & 0x1f) == 0x10 && !ejected && disk_fd >= 0) {
            put_be64(&resp[0], num_blocks - 1);
            put_be32(&resp[8], 512);
            handle_scsi_data_in(cbw, resp, 32, &residue);
        } else {
            set_sense(0x05, 0x20, 0x00);
            handle_scsi_data_in(cbw, resp, 0, &residue);
            csw.status = 1;
        }
        break;

    case 0x1A: /* MODE SENSE (6) */ {
        uint8_t page = cdb[2] & 0x3f;
        int len = 4;
        resp[1] = 0x00;
        resp[2] = read_only ? 0x80 : 0x00;
        resp[3] = 0x00;
        if (page == 0x08 || page == 0x3f) {
            resp[4] = 0x08;
            resp[5] = 0x0a;
            resp[6] = 0x04;
            len += 12;
        }
        resp[0] = len - 1;
        handle_scsi_data_in(cbw, resp, len, &residue);
        break;
    }

    case 0x5A: /* MODE SENSE (10) */ {
        uint8_t page = cdb[2] & 0x3f;
        int len = 8;
        resp[2] = 0x00;
        resp[3] = read_only ? 0x80 : 0x00;
        if (page == 0x08 || page == 0x3f) {
            resp[8] = 0x08;
            resp[9] = 0x0a;
            resp[10] = 0x04;
            len += 12;
        }
        put_be16(&resp[0], len - 2);
        handle_scsi_data_in(cbw, resp, len, &residue);
        break;
    }

    case 0x1E: /* PREVENT ALLOW MEDIUM REMOVAL */
    case 0x2F: /* VERIFY (10) */
        residue = 0;
        break;

    case 0x35: /* SYNCHRONIZE CACHE (10) */
        if (disk_fd >= 0) fsync(disk_fd);
        residue = 0;
        break;

    case 0x1B: /* START STOP UNIT */ {
        bool loej = (cdb[4] & 0x02) != 0;
        bool start = (cdb[4] & 0x01) != 0;
        if (disk_fd >= 0) fsync(disk_fd);
        if (loej && !start) {
            ejected = true;
            if (disk_fd >= 0) {
                close(disk_fd);
                disk_fd = -1;
            }
        }
        residue = 0;
        break;
    }

    case 0x28: /* READ (10) */
    case 0xA8: /* READ (12) */
    case 0x88: /* READ (16) */ {
        uint64_t lba = (op == 0x88) ? get_be64(&cdb[2]) : get_be32(&cdb[2]);
        uint32_t blocks = (op == 0x28) ? get_be16(&cdb[7]) :
                          (op == 0xA8) ? get_be32(&cdb[6]) : get_be32(&cdb[10]);
        if (ejected || disk_fd < 0) {
            set_sense(0x02, 0x3A, 0x00);
            csw.status = 1;
            break;
        }
        if (lba + blocks > num_blocks) {
            set_sense(0x05, 0x21, 0x00);
            csw.status = 1;
            break;
        }
        int64_t offset = (int64_t)(lba * 512ULL);
        uint32_t bytes_left = blocks * 512U;
        if (bytes_left > cbw->data_len) bytes_left = cbw->data_len;
        while (bytes_left > 0) {
            size_t chunk = bytes_left > sizeof(io_buf) ? sizeof(io_buf) : bytes_left;
            ssize_t r = pread64(disk_fd, io_buf, chunk, offset);
            if (r <= 0) {
                set_sense(0x03, 0x11, 0x00);
                csw.status = 1;
                break;
            }
            if (write_all(ep_in_fd, io_buf, (size_t)r) < 0) return -1;
            offset += r;
            bytes_left -= (uint32_t)r;
            residue -= (uint32_t)r;
        }
        break;
    }

    case 0x2A: /* WRITE (10) */
    case 0xAA: /* WRITE (12) */
    case 0x8A: /* WRITE (16) */ {
        uint64_t lba = (op == 0x8A) ? get_be64(&cdb[2]) : get_be32(&cdb[2]);
        uint32_t blocks = (op == 0x2A) ? get_be16(&cdb[7]) :
                          (op == 0xAA) ? get_be32(&cdb[6]) : get_be32(&cdb[10]);
        int64_t offset = (int64_t)(lba * 512ULL);
        uint32_t bytes_left = blocks * 512U;
        if (bytes_left > cbw->data_len) bytes_left = cbw->data_len;

        if (ejected || disk_fd < 0 || read_only || lba + blocks > num_blocks) {
            while (bytes_left > 0) {
                size_t chunk = bytes_left > sizeof(io_buf) ? sizeof(io_buf) : bytes_left;
                ssize_t r = read_all(ep_out_fd, io_buf, chunk);
                if (r <= 0) return -1;
                bytes_left -= (uint32_t)r;
            }
            if (read_only) set_sense(0x07, 0x27, 0x00);
            else if (ejected || disk_fd < 0) set_sense(0x02, 0x3A, 0x00);
            else set_sense(0x05, 0x21, 0x00);
            csw.status = 1;
            break;
        }
        while (bytes_left > 0) {
            size_t chunk = bytes_left > sizeof(io_buf) ? sizeof(io_buf) : bytes_left;
            ssize_t r = read_all(ep_out_fd, io_buf, chunk);
            if (r <= 0) return -1;
            ssize_t w = pwrite64(disk_fd, io_buf, (size_t)r, offset);
            if (w != r) {
                set_sense(0x03, 0x0C, 0x00);
                csw.status = 1;
            }
            offset += r;
            bytes_left -= (uint32_t)r;
            residue -= (uint32_t)r;
        }
        break;
    }

    default:
        set_sense(0x05, 0x20, 0x00);
        if (cbw->data_len > 0) {
            if (cbw->flags & 0x80) {
                handle_scsi_data_in(cbw, resp, 0, &residue);
            } else {
                uint32_t left = cbw->data_len;
                while (left > 0) {
                    size_t chunk = left > sizeof(io_buf) ? sizeof(io_buf) : left;
                    ssize_t r = read(ep_out_fd, io_buf, chunk);
                    if (r <= 0) break;
                    left -= (uint32_t)r;
                }
            }
        }
        csw.status = 1;
        break;
    }

    csw.residue = residue;
    if (write_all(ep_in_fd, &csw, sizeof(csw)) < 0) return -1;
    return 0;
}

static void ep0_child_loop(void) {
    struct usb_functionfs_event ev;
    while (1) {
        ssize_t n = read(ep0_fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            break;
        }
        if (n != (ssize_t)sizeof(ev)) continue;
        if (ev.type == FUNCTIONFS_SETUP) {
            if (ev.u.setup.bRequestType == 0xA1 && ev.u.setup.bRequest == 0xFE) {
                uint8_t max_lun = 0;
                write(ep0_fd, &max_lun, 1);
            } else if (ev.u.setup.bRequestType & 0x80) {
                uint8_t dummy = 0;
                write(ep0_fd, &dummy, ev.u.setup.wLength ? 1 : 0);
            } else {
                uint8_t dummy;
                read(ep0_fd, &dummy, 0);
            }
        }
    }
    _exit(0);
}

static void input_child_loop(pid_t parent_pid) {
    int fds[3] = {-1, -1, -1};
    fds[0] = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    fds[1] = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    fds[2] = open("/dev/input/event2", O_RDONLY | O_NONBLOCK);

    int btn_b = 289;
    int btn_menu = 27;
    char *env_b = getenv("USER_BTN_B");
    if (env_b && atoi(env_b) > 0) btn_b = atoi(env_b);
    char *env_menu = getenv("USER_BTN_MENU");
    if (env_menu && atoi(env_menu) > 0) btn_menu = atoi(env_menu);

    /* Wait 500ms before accepting exit button so initial A press release doesn't trigger anything */
    usleep(500000);
    struct linux_input_event ev;
    for (int i = 0; i < 3; i++) {
        if (fds[i] >= 0) {
            while (read(fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {}
        }
    }

    while (1) {
        if (kill(parent_pid, 0) < 0) break;
        for (int i = 0; i < 3; i++) {
            if (fds[i] < 0) continue;
            while (read(fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                /* EV_KEY == 1, value == 1 (pressed) */
                if (ev.type == 1 && ev.value == 1) {
                    if (ev.code == btn_b || ev.code == btn_menu || ev.code == 289 || ev.code == 27 || ev.code == 1) {
                        fprintf(stderr, "[umsd-input] Exit button pressed (code=%u), stopping USB Storage.\n", ev.code);
                        fflush(stderr);
                        kill(parent_pid, SIGTERM);
                        _exit(0);
                    }
                }
            }
        }
        usleep(20000);
    }
    _exit(0);
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ffs_dir> <block_dev_or_img> [--ro] [--exit-on-eject] [--watch-input]\n", argv[0]);
        return 1;
    }
    const char *ffs_dir = argv[1];
    const char *disk_path = argv[2];
    bool watch_input = false;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--ro") == 0) read_only = true;
        else if (strcmp(argv[i], "--exit-on-eject") == 0) exit_on_eject = true;
        else if (strcmp(argv[i], "--watch-input") == 0) watch_input = true;
    }

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    disk_fd = open(disk_path, (read_only ? O_RDONLY : O_RDWR) | O_LARGEFILE);
    if (disk_fd < 0 && !read_only) {
        disk_fd = open(disk_path, O_RDONLY | O_LARGEFILE);
        if (disk_fd >= 0) read_only = true;
    }
    if (disk_fd < 0) {
        perror("open disk");
        return 1;
    }

    int64_t end = lseek64(disk_fd, 0, SEEK_END);
    if (end > 0) {
        disk_size = (uint64_t)end;
        lseek64(disk_fd, 0, SEEK_SET);
    }
    num_blocks = disk_size / 512ULL;
    fprintf(stderr, "[umsd] Backing storage: %s (%llu bytes, %llu blocks, ro=%d)\n",
            disk_path, (unsigned long long)disk_size, (unsigned long long)num_blocks, read_only);
    fflush(stderr);

    char path[256];
    snprintf(path, sizeof(path), "%s/ep0", ffs_dir);
    ep0_fd = open(path, O_RDWR);
    if (ep0_fd < 0) {
        perror("open ep0");
        return 1;
    }
    if (write(ep0_fd, &descriptors, sizeof(descriptors)) < 0) {
        perror("write descriptors");
        return 1;
    }
    if (write(ep0_fd, &strings, sizeof(strings)) < 0) {
        perror("write strings");
        return 1;
    }

    snprintf(path, sizeof(path), "%s/ep1", ffs_dir);
    ep_out_fd = open(path, O_RDWR);
    if (ep_out_fd < 0) {
        perror("open ep1 (out)");
        return 1;
    }
    snprintf(path, sizeof(path), "%s/ep2", ffs_dir);
    ep_in_fd = open(path, O_RDWR);
    if (ep_in_fd < 0) {
        perror("open ep2 (in)");
        return 1;
    }

    fprintf(stderr, "[umsd] FunctionFS initialized and ready on %s\n", ffs_dir);
    fflush(stderr);

    pid_t self_pid = getpid();
    ep0_pid = fork();
    if (ep0_pid == 0) {
        ep0_child_loop();
        return 0;
    }

    if (watch_input) {
        input_pid = fork();
        if (input_pid == 0) {
            input_child_loop(self_pid);
            return 0;
        }
    }

    uint8_t cbw_buf[512] __attribute__((aligned(512)));
    while (!stop_requested) {
        ssize_t n = read(ep_out_fd, cbw_buf, sizeof(cbw_buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ESHUTDOWN || errno == ENODEV) {
                usleep(5000);
                continue;
            }
            usleep(10000);
            continue;
        }
        if (n < (ssize_t)sizeof(struct bot_cbw)) {
            continue;
        }
        const struct bot_cbw *cbw = (const struct bot_cbw *)cbw_buf;
        if (cbw->signature != CBW_SIGNATURE) {
            continue;
        }
        handle_cbw(cbw);

        if (ejected && exit_on_eject) {
            fprintf(stderr, "[umsd] Media ejected by host, exiting cleanly.\n");
            fflush(stderr);
            usleep(300000);
            break;
        }
    }

    if (disk_fd >= 0) fsync(disk_fd);
    if (ep0_pid > 0) kill(ep0_pid, SIGTERM);
    if (input_pid > 0) kill(input_pid, SIGTERM);
    close(ep_in_fd);
    close(ep_out_fd);
    close(ep0_fd);
    return 0;
}
