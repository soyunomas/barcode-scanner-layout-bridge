#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DEFAULT_VENDOR_ID "34eb"
#define DEFAULT_MODEL_ID "1502"
#define DEFAULT_IDLE_TIMEOUT_MS 1000
#define DEFAULT_KEY_DELAY_US 1000
#define DEFAULT_CHAR_DELAY_US 3000
#define DEFAULT_OUTPUT_LAYOUT "es"
#define DEFAULT_RECONNECT_DELAY_MS 1000
#define SCAN_BUF_SIZE 1024
#define SCAN_QUEUE_SIZE 64
#define RECONNECT_EXIT_CODE 75

struct app_config {
    const char *device;
    const char *output_layout;
    bool dry_run;
    bool emit_invalid;
    bool reconnect;
    int idle_timeout_ms;
    int key_delay_us;
    int char_delay_us;
    int reconnect_delay_ms;
};

struct key_combo {
    unsigned int code;
    bool shift;
    bool altgr;
};

struct layout_profile {
    const char *name;
    const char *description;
    int (*char_to_combo)(char c, struct key_combo *combo);
};

struct scan_item {
    char text[SCAN_BUF_SIZE];
    size_t len;
};

struct scan_queue {
    struct scan_item items[SCAN_QUEUE_SIZE];
    size_t head;
    size_t tail;
    size_t count;
    bool stopped;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

struct bridge_context {
    int out_fd;
    bool dry_run;
    bool emit_invalid;
    const struct layout_profile *layout;
    struct scan_queue queue;
};

static int g_key_delay_us = DEFAULT_KEY_DELAY_US;
static int g_char_delay_us = DEFAULT_CHAR_DELAY_US;

static int read_trimmed_file(const char *path, char *buf, size_t buflen) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    ssize_t n = read(fd, buf, buflen - 1);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;

    if (n < 0) {
        return -1;
    }
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }
    return 0;
}

static int detect_event_device(char *out, size_t out_len) {
    for (int event_num = 0; event_num < 256; event_num++) {
        char base[256];
        char vendor_path[512];
        char product_path[512];
        char name_path[512];
        char vendor[32];
        char product[32];
        char name[256];

        snprintf(base, sizeof(base), "/sys/class/input/event%d/device", event_num);
        snprintf(vendor_path, sizeof(vendor_path), "%s/id/vendor", base);
        snprintf(product_path, sizeof(product_path), "%s/id/product", base);
        snprintf(name_path, sizeof(name_path), "%s/name", base);

        if (read_trimmed_file(vendor_path, vendor, sizeof(vendor)) != 0 ||
            read_trimmed_file(product_path, product, sizeof(product)) != 0) {
            continue;
        }

        if (strcasecmp(vendor, DEFAULT_VENDOR_ID) != 0 ||
            strcasecmp(product, DEFAULT_MODEL_ID) != 0) {
            continue;
        }

        if (read_trimmed_file(name_path, name, sizeof(name)) != 0) {
            snprintf(name, sizeof(name), "unknown");
        }

        snprintf(out, out_len, "/dev/input/event%d", event_num);
        fprintf(stderr, "Autodetectado %s (%s, vendor=%s product=%s)\n",
                out, name, vendor, product);
        return 0;
    }

    errno = ENOENT;
    return -1;
}

static bool is_shift_key(unsigned int code) {
    return code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT;
}

static int us_key_to_char(unsigned int code, bool shift) {
    switch (code) {
    case KEY_A: return shift ? 'A' : 'a';
    case KEY_B: return shift ? 'B' : 'b';
    case KEY_C: return shift ? 'C' : 'c';
    case KEY_D: return shift ? 'D' : 'd';
    case KEY_E: return shift ? 'E' : 'e';
    case KEY_F: return shift ? 'F' : 'f';
    case KEY_G: return shift ? 'G' : 'g';
    case KEY_H: return shift ? 'H' : 'h';
    case KEY_I: return shift ? 'I' : 'i';
    case KEY_J: return shift ? 'J' : 'j';
    case KEY_K: return shift ? 'K' : 'k';
    case KEY_L: return shift ? 'L' : 'l';
    case KEY_M: return shift ? 'M' : 'm';
    case KEY_N: return shift ? 'N' : 'n';
    case KEY_O: return shift ? 'O' : 'o';
    case KEY_P: return shift ? 'P' : 'p';
    case KEY_Q: return shift ? 'Q' : 'q';
    case KEY_R: return shift ? 'R' : 'r';
    case KEY_S: return shift ? 'S' : 's';
    case KEY_T: return shift ? 'T' : 't';
    case KEY_U: return shift ? 'U' : 'u';
    case KEY_V: return shift ? 'V' : 'v';
    case KEY_W: return shift ? 'W' : 'w';
    case KEY_X: return shift ? 'X' : 'x';
    case KEY_Y: return shift ? 'Y' : 'y';
    case KEY_Z: return shift ? 'Z' : 'z';
    case KEY_1: return shift ? '!' : '1';
    case KEY_2: return shift ? '@' : '2';
    case KEY_3: return shift ? '#' : '3';
    case KEY_4: return shift ? '$' : '4';
    case KEY_5: return shift ? '%' : '5';
    case KEY_6: return shift ? '^' : '6';
    case KEY_7: return shift ? '&' : '7';
    case KEY_8: return shift ? '*' : '8';
    case KEY_9: return shift ? '(' : '9';
    case KEY_0: return shift ? ')' : '0';
    case KEY_MINUS: return shift ? '_' : '-';
    case KEY_EQUAL: return shift ? '+' : '=';
    case KEY_LEFTBRACE: return shift ? '{' : '[';
    case KEY_RIGHTBRACE: return shift ? '}' : ']';
    case KEY_BACKSLASH: return shift ? '|' : '\\';
    case KEY_SEMICOLON: return shift ? ':' : ';';
    case KEY_APOSTROPHE: return shift ? '"' : '\'';
    case KEY_GRAVE: return shift ? '~' : '`';
    case KEY_COMMA: return shift ? '<' : ',';
    case KEY_DOT: return shift ? '>' : '.';
    case KEY_SLASH: return shift ? '?' : '/';
    case KEY_SPACE: return ' ';
    default: return -1;
    }
}

static int qwerty_letter_to_combo(char c, struct key_combo *combo) {
    memset(combo, 0, sizeof(*combo));

    static const unsigned int keys[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
        KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
        KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    };

    if (c >= 'a' && c <= 'z') {
        combo->code = keys[c - 'a'];
        return 0;
    }

    if (c >= 'A' && c <= 'Z') {
        combo->code = keys[c - 'A'];
        combo->shift = true;
        return 0;
    }

    return -1;
}

static int us_char_to_combo(char c, struct key_combo *combo) {
    if (qwerty_letter_to_combo(c, combo) == 0) {
        return 0;
    }

    memset(combo, 0, sizeof(*combo));

    switch (c) {
    case '1': combo->code = KEY_1; return 0;
    case '2': combo->code = KEY_2; return 0;
    case '3': combo->code = KEY_3; return 0;
    case '4': combo->code = KEY_4; return 0;
    case '5': combo->code = KEY_5; return 0;
    case '6': combo->code = KEY_6; return 0;
    case '7': combo->code = KEY_7; return 0;
    case '8': combo->code = KEY_8; return 0;
    case '9': combo->code = KEY_9; return 0;
    case '0': combo->code = KEY_0; return 0;
    case '.': combo->code = KEY_DOT; return 0;
    case ',': combo->code = KEY_COMMA; return 0;
    case '-': combo->code = KEY_MINUS; return 0;
    case ' ': combo->code = KEY_SPACE; return 0;
    case ':': combo->code = KEY_SEMICOLON; combo->shift = true; return 0;
    case '/': combo->code = KEY_SLASH; return 0;
    case '?': combo->code = KEY_SLASH; combo->shift = true; return 0;
    case '=': combo->code = KEY_EQUAL; return 0;
    case '&': combo->code = KEY_7; combo->shift = true; return 0;
    case '%': combo->code = KEY_5; combo->shift = true; return 0;
    case '+': combo->code = KEY_EQUAL; combo->shift = true; return 0;
    case '_': combo->code = KEY_MINUS; combo->shift = true; return 0;
    case ';': combo->code = KEY_SEMICOLON; return 0;
    case '"': combo->code = KEY_APOSTROPHE; combo->shift = true; return 0;
    case '!': combo->code = KEY_1; combo->shift = true; return 0;
    case '(': combo->code = KEY_9; combo->shift = true; return 0;
    case ')': combo->code = KEY_0; combo->shift = true; return 0;
    case '@': combo->code = KEY_2; combo->shift = true; return 0;
    case '#': combo->code = KEY_3; combo->shift = true; return 0;
    case '~': combo->code = KEY_GRAVE; combo->shift = true; return 0;
    default: return -1;
    }
}

static int es_char_to_combo(char c, struct key_combo *combo) {
    if (qwerty_letter_to_combo(c, combo) == 0) {
        return 0;
    }

    memset(combo, 0, sizeof(*combo));

    switch (c) {
    case '1': combo->code = KEY_1; return 0;
    case '2': combo->code = KEY_2; return 0;
    case '3': combo->code = KEY_3; return 0;
    case '4': combo->code = KEY_4; return 0;
    case '5': combo->code = KEY_5; return 0;
    case '6': combo->code = KEY_6; return 0;
    case '7': combo->code = KEY_7; return 0;
    case '8': combo->code = KEY_8; return 0;
    case '9': combo->code = KEY_9; return 0;
    case '0': combo->code = KEY_0; return 0;
    case '.': combo->code = KEY_DOT; return 0;
    case ',': combo->code = KEY_COMMA; return 0;
    case '-': combo->code = KEY_SLASH; return 0;
    case ' ': combo->code = KEY_SPACE; return 0;
    case ':': combo->code = KEY_DOT; combo->shift = true; return 0;
    case '/': combo->code = KEY_7; combo->shift = true; return 0;
    case '?': combo->code = KEY_MINUS; combo->shift = true; return 0;
    case '=': combo->code = KEY_0; combo->shift = true; return 0;
    case '&': combo->code = KEY_6; combo->shift = true; return 0;
    case '%': combo->code = KEY_5; combo->shift = true; return 0;
    case '+': combo->code = KEY_RIGHTBRACE; return 0;
    case '_': combo->code = KEY_SLASH; combo->shift = true; return 0;
    case ';': combo->code = KEY_COMMA; combo->shift = true; return 0;
    case '"': combo->code = KEY_2; combo->shift = true; return 0;
    case '!': combo->code = KEY_1; combo->shift = true; return 0;
    case '(': combo->code = KEY_8; combo->shift = true; return 0;
    case ')': combo->code = KEY_9; combo->shift = true; return 0;
    case '@': combo->code = KEY_2; combo->altgr = true; return 0;
    case '#': combo->code = KEY_3; combo->altgr = true; return 0;
    case '~': combo->code = KEY_4; combo->altgr = true; return 0;
    default: return -1;
    }
}

static int fr_char_to_combo(char c, struct key_combo *combo) {
    memset(combo, 0, sizeof(*combo));

    static const unsigned int lower_keys[26] = {
        KEY_Q,         /* a */
        KEY_B,         /* b */
        KEY_C,         /* c */
        KEY_D,         /* d */
        KEY_E,         /* e */
        KEY_F,         /* f */
        KEY_G,         /* g */
        KEY_H,         /* h */
        KEY_I,         /* i */
        KEY_J,         /* j */
        KEY_K,         /* k */
        KEY_L,         /* l */
        KEY_SEMICOLON, /* m */
        KEY_N,         /* n */
        KEY_O,         /* o */
        KEY_P,         /* p */
        KEY_A,         /* q */
        KEY_R,         /* r */
        KEY_S,         /* s */
        KEY_T,         /* t */
        KEY_U,         /* u */
        KEY_V,         /* v */
        KEY_Z,         /* w */
        KEY_X,         /* x */
        KEY_Y,         /* y */
        KEY_W,         /* z */
    };

    if (c >= 'a' && c <= 'z') {
        combo->code = lower_keys[c - 'a'];
        return 0;
    }
    if (c >= 'A' && c <= 'Z') {
        combo->code = lower_keys[c - 'A'];
        combo->shift = true;
        return 0;
    }

    switch (c) {
    case '1': combo->code = KEY_1; combo->shift = true; return 0;
    case '2': combo->code = KEY_2; combo->shift = true; return 0;
    case '3': combo->code = KEY_3; combo->shift = true; return 0;
    case '4': combo->code = KEY_4; combo->shift = true; return 0;
    case '5': combo->code = KEY_5; combo->shift = true; return 0;
    case '6': combo->code = KEY_6; combo->shift = true; return 0;
    case '7': combo->code = KEY_7; combo->shift = true; return 0;
    case '8': combo->code = KEY_8; combo->shift = true; return 0;
    case '9': combo->code = KEY_9; combo->shift = true; return 0;
    case '0': combo->code = KEY_0; combo->shift = true; return 0;
    case '.': combo->code = KEY_COMMA; combo->shift = true; return 0;
    case ',': combo->code = KEY_M; return 0;
    case '-': combo->code = KEY_6; return 0;
    case ' ': combo->code = KEY_SPACE; return 0;
    case ':': combo->code = KEY_DOT; return 0;
    case '/': combo->code = KEY_DOT; combo->shift = true; return 0;
    case '?': combo->code = KEY_M; combo->shift = true; return 0;
    case '=': combo->code = KEY_EQUAL; return 0;
    case '&': combo->code = KEY_1; return 0;
    case '%': combo->code = KEY_APOSTROPHE; combo->shift = true; return 0;
    case '+': combo->code = KEY_EQUAL; combo->shift = true; return 0;
    case '_': combo->code = KEY_8; return 0;
    case ';': combo->code = KEY_COMMA; return 0;
    case '"': combo->code = KEY_3; return 0;
    case '!': combo->code = KEY_SLASH; return 0;
    case '(': combo->code = KEY_5; return 0;
    case ')': combo->code = KEY_MINUS; return 0;
    case '@': combo->code = KEY_0; combo->altgr = true; return 0;
    case '#': combo->code = KEY_3; combo->altgr = true; return 0;
    case '~': combo->code = KEY_2; combo->altgr = true; return 0;
    default: return -1;
    }
}

static int de_char_to_combo(char c, struct key_combo *combo) {
    if (qwerty_letter_to_combo(c, combo) == 0) {
        if (c == 'y' || c == 'Y') {
            combo->code = KEY_Z;
            combo->shift = c == 'Y';
        } else if (c == 'z' || c == 'Z') {
            combo->code = KEY_Y;
            combo->shift = c == 'Z';
        }
        return 0;
    }

    memset(combo, 0, sizeof(*combo));

    switch (c) {
    case '1': combo->code = KEY_1; return 0;
    case '2': combo->code = KEY_2; return 0;
    case '3': combo->code = KEY_3; return 0;
    case '4': combo->code = KEY_4; return 0;
    case '5': combo->code = KEY_5; return 0;
    case '6': combo->code = KEY_6; return 0;
    case '7': combo->code = KEY_7; return 0;
    case '8': combo->code = KEY_8; return 0;
    case '9': combo->code = KEY_9; return 0;
    case '0': combo->code = KEY_0; return 0;
    case '.': combo->code = KEY_DOT; return 0;
    case ',': combo->code = KEY_COMMA; return 0;
    case '-': combo->code = KEY_SLASH; return 0;
    case ' ': combo->code = KEY_SPACE; return 0;
    case ':': combo->code = KEY_DOT; combo->shift = true; return 0;
    case '/': combo->code = KEY_7; combo->shift = true; return 0;
    case '?': combo->code = KEY_MINUS; combo->shift = true; return 0;
    case '=': combo->code = KEY_0; combo->shift = true; return 0;
    case '&': combo->code = KEY_6; combo->shift = true; return 0;
    case '%': combo->code = KEY_5; combo->shift = true; return 0;
    case '+': combo->code = KEY_RIGHTBRACE; return 0;
    case '_': combo->code = KEY_SLASH; combo->shift = true; return 0;
    case ';': combo->code = KEY_COMMA; combo->shift = true; return 0;
    case '"': combo->code = KEY_2; combo->shift = true; return 0;
    case '!': combo->code = KEY_1; combo->shift = true; return 0;
    case '(': combo->code = KEY_8; combo->shift = true; return 0;
    case ')': combo->code = KEY_9; combo->shift = true; return 0;
    case '@': combo->code = KEY_Q; combo->altgr = true; return 0;
    case '#': combo->code = KEY_BACKSLASH; return 0;
    case '~': combo->code = KEY_RIGHTBRACE; combo->altgr = true; return 0;
    default: return -1;
    }
}

static const struct layout_profile layout_profiles[] = {
    {"es", "Spanish XKB basic layout (tested)", es_char_to_combo},
    {"us", "US XKB basic layout", us_char_to_combo},
    {"fr", "French XKB basic AZERTY layout", fr_char_to_combo},
    {"de", "German XKB basic QWERTZ layout", de_char_to_combo},
};

static const struct layout_profile *find_layout_profile(const char *name) {
    for (size_t i = 0; i < sizeof(layout_profiles) / sizeof(layout_profiles[0]); i++) {
        if (strcmp(layout_profiles[i].name, name) == 0) {
            return &layout_profiles[i];
        }
    }
    return NULL;
}

static int emit_event(int fd, unsigned int type, unsigned int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;

    ssize_t n = write(fd, &ev, sizeof(ev));
    if (n != (ssize_t)sizeof(ev)) {
        return -1;
    }
    return 0;
}

static int emit_syn(int fd) {
    return emit_event(fd, EV_SYN, SYN_REPORT, 0);
}

static int emit_key(int fd, unsigned int code, int value) {
    if (emit_event(fd, EV_KEY, code, value) != 0) {
        return -1;
    }
    if (emit_syn(fd) != 0) {
        return -1;
    }
    if (g_key_delay_us > 0) {
        usleep((useconds_t)g_key_delay_us);
    }
    return 0;
}

static int emit_combo(int fd, const struct key_combo *combo) {
    if (combo->shift && emit_key(fd, KEY_LEFTSHIFT, 1) != 0) return -1;
    if (combo->altgr && emit_key(fd, KEY_RIGHTALT, 1) != 0) return -1;
    if (emit_key(fd, combo->code, 1) != 0) return -1;
    if (emit_key(fd, combo->code, 0) != 0) return -1;
    if (combo->altgr && emit_key(fd, KEY_RIGHTALT, 0) != 0) return -1;
    if (combo->shift && emit_key(fd, KEY_LEFTSHIFT, 0) != 0) return -1;
    if (g_char_delay_us > 0) {
        usleep((useconds_t)g_char_delay_us);
    }
    return 0;
}

static void print_layouts(FILE *stream) {
    for (size_t i = 0; i < sizeof(layout_profiles) / sizeof(layout_profiles[0]); i++) {
        fprintf(stream, "  %-2s  %s\n", layout_profiles[i].name, layout_profiles[i].description);
    }
}

static int emit_text_layout(int fd, const struct layout_profile *layout, const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        struct key_combo combo;
        if (layout->char_to_combo(text[i], &combo) != 0) {
            fprintf(stderr, "Caracter no soportado para salida %s: 0x%02x '%c'\n",
                    layout->name,
                    (unsigned char)text[i],
                    (text[i] >= 32 && text[i] <= 126) ? text[i] : '?');
            return -1;
        }
        if (emit_combo(fd, &combo) != 0) {
            fprintf(stderr, "No pude emitir caracter '%c': %s\n", text[i], strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int setup_uinput(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "No puedo abrir /dev/uinput: %s\n", strerror(errno));
        return -1;
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
        fprintf(stderr, "UI_SET_EVBIT fallo: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    for (unsigned int code = 1; code < KEY_MAX; code++) {
        ioctl(fd, UI_SET_KEYBIT, code);
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x34eb;
    usetup.id.product = 0x1502;
    usetup.id.version = 1;
    snprintf(usetup.name, sizeof(usetup.name), "escanner corrected keyboard");

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
        fprintf(stderr, "UI_DEV_SETUP fallo: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "UI_DEV_CREATE fallo: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    usleep(200000);
    return fd;
}

static void destroy_uinput(int fd) {
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
}

static void print_scan(const char *text, size_t len, bool dry_run) {
    printf("%sSCAN: \"", dry_run ? "DRY-" : "");
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\\' || c == '"') {
            putchar('\\');
            putchar(c);
        } else if (c >= 32 && c <= 126) {
            putchar(c);
        } else {
            printf("\\x%02x", c);
        }
    }
    printf("\"\n");
    fflush(stdout);
}

static bool is_url_host_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '.' || c == '-';
}

static bool looks_like_url(const char *text, size_t len) {
    size_t host_start = 0;
    if (len >= 7 && strncmp(text, "http://", 7) == 0) {
        host_start = 7;
    } else if (len >= 8 && strncmp(text, "https://", 8) == 0) {
        host_start = 8;
    } else {
        return false;
    }

    if (host_start >= len) {
        return false;
    }

    size_t host_len = 0;
    bool has_dot = false;
    for (size_t i = host_start; i < len; i++) {
        char c = text[i];
        if (c == '/' || c == '?' || c == '#' || c == ':') {
            break;
        }
        if (!is_url_host_char(c)) {
            return false;
        }
        if (c == '.') {
            has_dot = true;
        }
        host_len++;
    }

    return host_len > 0 && has_dot;
}

static int process_scan(int out_fd, const struct layout_profile *layout, const char *text, size_t len, bool dry_run, bool emit_invalid) {
    print_scan(text, len, dry_run);
    if (!looks_like_url(text, len)) {
        if (!emit_invalid) {
            fprintf(stderr, "DROP: escaneo incompleto o URL invalida; no se emite.\n");
            return 0;
        }
        fprintf(stderr, "Aviso: el escaneo no parece URL; se emite por --emit-invalid.\n");
    }

    int rc = 0;
    if (!dry_run) {
        rc = emit_text_layout(out_fd, layout, text, len);
        if (rc == 0) {
            struct key_combo enter = {.code = KEY_ENTER};
            rc = emit_combo(out_fd, &enter);
        }
    }

    return rc;
}

static void scan_queue_init(struct scan_queue *queue) {
    memset(queue, 0, sizeof(*queue));
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

static void scan_queue_destroy(struct scan_queue *queue) {
    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
}

static int scan_queue_push(struct scan_queue *queue, const char *text, size_t len) {
    if (len >= SCAN_BUF_SIZE) {
        len = SCAN_BUF_SIZE - 1;
    }

    pthread_mutex_lock(&queue->mutex);
    if (queue->stopped) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    if (queue->count == SCAN_QUEUE_SIZE) {
        pthread_mutex_unlock(&queue->mutex);
        fprintf(stderr, "DROP: cola de emision llena; escaneo descartado.\n");
        return 0;
    }

    struct scan_item *item = &queue->items[queue->tail];
    memcpy(item->text, text, len);
    item->text[len] = '\0';
    item->len = len;
    queue->tail = (queue->tail + 1) % SCAN_QUEUE_SIZE;
    queue->count++;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

static bool scan_queue_pop(struct scan_queue *queue, struct scan_item *out) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->stopped) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    if (queue->count == 0 && queue->stopped) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }

    *out = queue->items[queue->head];
    queue->head = (queue->head + 1) % SCAN_QUEUE_SIZE;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

static void scan_queue_stop(struct scan_queue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

static void *emitter_thread_main(void *arg) {
    struct bridge_context *ctx = arg;
    struct scan_item item;

    while (scan_queue_pop(&ctx->queue, &item)) {
        if (process_scan(ctx->out_fd, ctx->layout, item.text, item.len, ctx->dry_run, ctx->emit_invalid) != 0) {
            fprintf(stderr, "ERROR: fallo emitiendo escaneo.\n");
        }
    }

    return NULL;
}

static int flush_scan(struct bridge_context *ctx, char *buf, size_t *len) {
    if (*len == 0) {
        return 0;
    }

    int rc;
    if (ctx->dry_run) {
        rc = process_scan(ctx->out_fd, ctx->layout, buf, *len, ctx->dry_run, ctx->emit_invalid);
    } else {
        rc = scan_queue_push(&ctx->queue, buf, *len);
    }
    *len = 0;
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Uso: %s [opciones] [/dev/input/eventX]\n"
            "\n"
            "Captura el escaner %s:%s, traduce su entrada US y emite teclas para el layout indicado.\n"
            "\n"
            "Opciones:\n"
            "  --output-layout NAME     Layout de salida: es, us, fr, de (por defecto: %s)\n"
            "  --dry-run                Captura y muestra, pero no crea /dev/uinput ni reemite\n"
            "  --emit-invalid           Emite tambien escaneos que no parezcan URL completa\n"
            "  --idle-ms N              Timeout de fin de escaneo sin ENTER/TAB (por defecto: %d)\n"
            "  --key-delay-us N         Retardo entre eventos de tecla (por defecto: %d)\n"
            "  --char-delay-us N        Retardo tras cada caracter (por defecto: %d)\n"
            "  --no-reconnect           No reintenta tras desconectar/reconectar\n"
            "  --reconnect-delay-ms N   Pausa entre reintentos (por defecto: %d)\n"
            "  --help                   Muestra esta ayuda\n"
            "\n"
            "Layouts disponibles:\n",
            argv0,
            DEFAULT_VENDOR_ID,
            DEFAULT_MODEL_ID,
            DEFAULT_OUTPUT_LAYOUT,
            DEFAULT_IDLE_TIMEOUT_MS,
            DEFAULT_KEY_DELAY_US,
            DEFAULT_CHAR_DELAY_US,
            DEFAULT_RECONNECT_DELAY_MS);
    print_layouts(stderr);
    fprintf(stderr,
            "\n"
            "--dry-run captura y muestra, pero no crea /dev/uinput ni reemite.\n"
            "Retardos por defecto: key=%dus char=%dus.\n",
            DEFAULT_KEY_DELAY_US, DEFAULT_CHAR_DELAY_US);
}

static int parse_args(int argc, char **argv, struct app_config *config) {
    config->device = NULL;
    config->output_layout = DEFAULT_OUTPUT_LAYOUT;
    config->dry_run = false;
    config->emit_invalid = false;
    config->reconnect = true;
    config->idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;
    config->key_delay_us = DEFAULT_KEY_DELAY_US;
    config->char_delay_us = DEFAULT_CHAR_DELAY_US;
    config->reconnect_delay_ms = DEFAULT_RECONNECT_DELAY_MS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            config->dry_run = true;
        } else if (strcmp(argv[i], "--emit-invalid") == 0) {
            config->emit_invalid = true;
        } else if (strcmp(argv[i], "--no-reconnect") == 0) {
            config->reconnect = false;
        } else if (strcmp(argv[i], "--output-layout") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--output-layout requiere un valor\n");
                return -1;
            }
            config->output_layout = argv[++i];
        } else if (strcmp(argv[i], "--idle-ms") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--idle-ms requiere un valor\n");
                return -1;
            }
            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed < 10 || parsed > 10000) {
                fprintf(stderr, "--idle-ms invalido: %s\n", argv[i]);
                return -1;
            }
            config->idle_timeout_ms = (int)parsed;
        } else if (strcmp(argv[i], "--reconnect-delay-ms") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--reconnect-delay-ms requiere un valor\n");
                return -1;
            }
            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed < 100 || parsed > 60000) {
                fprintf(stderr, "--reconnect-delay-ms invalido: %s\n", argv[i]);
                return -1;
            }
            config->reconnect_delay_ms = (int)parsed;
        } else if (strcmp(argv[i], "--key-delay-us") == 0 || strcmp(argv[i], "--char-delay-us") == 0) {
            const char *option = argv[i];
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requiere un valor\n", option);
                return -1;
            }
            char *end = NULL;
            long parsed = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed < 0 || parsed > 500000) {
                fprintf(stderr, "%s invalido: %s\n", option, argv[i]);
                return -1;
            }
            if (strcmp(option, "--key-delay-us") == 0) {
                config->key_delay_us = (int)parsed;
            } else {
                config->char_delay_us = (int)parsed;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Opcion desconocida: %s\n", argv[i]);
            return -1;
        } else if (!config->device) {
            config->device = argv[i];
        } else {
            fprintf(stderr, "Argumento extra: %s\n", argv[i]);
            return -1;
        }
    }

    return 0;
}

static int run_bridge(const struct app_config *config) {
    g_key_delay_us = config->key_delay_us;
    g_char_delay_us = config->char_delay_us;

    const struct layout_profile *layout = find_layout_profile(config->output_layout);
    if (!layout) {
        fprintf(stderr, "Layout de salida desconocido: %s\n", config->output_layout);
        fprintf(stderr, "Layouts disponibles:\n");
        print_layouts(stderr);
        return 2;
    }

    int in_fd = open(config->device, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (in_fd < 0) {
        fprintf(stderr, "No puedo abrir %s: %s\n", config->device, strerror(errno));
        if (errno == ENOENT || errno == ENODEV || errno == EIO) {
            return RECONNECT_EXIT_CODE;
        }
        return 1;
    }

    char input_name[256] = {0};
    if (ioctl(in_fd, EVIOCGNAME(sizeof(input_name)), input_name) < 0) {
        snprintf(input_name, sizeof(input_name), "unknown");
    }

    int out_fd = -1;
    if (!config->dry_run) {
        out_fd = setup_uinput();
        if (out_fd < 0) {
            close(in_fd);
            return 1;
        }
    }

    struct bridge_context ctx = {
        .out_fd = out_fd,
        .dry_run = config->dry_run,
        .emit_invalid = config->emit_invalid,
        .layout = layout,
    };
    scan_queue_init(&ctx.queue);

    pthread_t emitter_thread;
    bool emitter_started = false;
    if (!config->dry_run) {
        int prc = pthread_create(&emitter_thread, NULL, emitter_thread_main, &ctx);
        if (prc != 0) {
            fprintf(stderr, "No pude crear hilo emisor: %s\n", strerror(prc));
            scan_queue_destroy(&ctx.queue);
            destroy_uinput(out_fd);
            close(in_fd);
            return 1;
        }
        emitter_started = true;
    }

    if (ioctl(in_fd, EVIOCGRAB, 1) < 0) {
        fprintf(stderr, "EVIOCGRAB fallo en %s: %s\n", config->device, strerror(errno));
        int rc = (errno == ENODEV || errno == ENOENT || errno == EIO) ? RECONNECT_EXIT_CODE : 1;
        if (emitter_started) {
            scan_queue_stop(&ctx.queue);
            pthread_join(emitter_thread, NULL);
        }
        scan_queue_destroy(&ctx.queue);
        destroy_uinput(out_fd);
        close(in_fd);
        return rc;
    }

    printf("BRIDGE: input=%s (%s) output-layout=%s dry-run=%s emit-invalid=%s idle-timeout-ms=%d key-delay-us=%d char-delay-us=%d\n",
           config->device,
           input_name,
           layout->name,
           config->dry_run ? "yes" : "no",
           config->emit_invalid ? "yes" : "no",
           config->idle_timeout_ms,
           config->key_delay_us,
           config->char_delay_us);
    fflush(stdout);

    bool left_shift = false;
    bool right_shift = false;
    char scan_buf[SCAN_BUF_SIZE];
    size_t scan_len = 0;
    bool have_pending_scan = false;

    struct pollfd pfd = {
        .fd = in_fd,
        .events = POLLIN,
    };

    int exit_code = 0;
    for (;;) {
        int timeout = have_pending_scan ? config->idle_timeout_ms : -1;
        int pr = poll(&pfd, 1, timeout);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "poll fallo: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }
        if (pr == 0) {
            if (flush_scan(&ctx, scan_buf, &scan_len) != 0) {
                exit_code = 1;
                break;
            }
            have_pending_scan = false;
            continue;
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            fprintf(stderr, "El dispositivo dejo de estar disponible (revents=0x%x)\n", pfd.revents);
            exit_code = RECONNECT_EXIT_CODE;
            break;
        }

        for (;;) {
            struct input_event ev;
            ssize_t n = read(in_fd, &ev, sizeof(ev));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "read fallo: %s\n", strerror(errno));
                exit_code = (errno == ENODEV || errno == ENOENT || errno == EIO) ? RECONNECT_EXIT_CODE : 1;
                goto out;
            }
            if (n == 0) {
                fprintf(stderr, "EOF inesperado\n");
                exit_code = RECONNECT_EXIT_CODE;
                goto out;
            }
            if ((size_t)n != sizeof(ev)) {
                fprintf(stderr, "evento corto: %zd bytes\n", n);
                exit_code = 1;
                goto out;
            }
            if (ev.type != EV_KEY) {
                continue;
            }

            if (is_shift_key(ev.code)) {
                if (ev.code == KEY_LEFTSHIFT) {
                    left_shift = ev.value != 0;
                } else {
                    right_shift = ev.value != 0;
                }
            }

            if (ev.value != 1) {
                continue;
            }

            if (ev.code == KEY_ENTER || ev.code == KEY_TAB) {
                if (flush_scan(&ctx, scan_buf, &scan_len) != 0) {
                    exit_code = 1;
                    goto out;
                }
                have_pending_scan = false;
                continue;
            }

            int ch = us_key_to_char(ev.code, left_shift || right_shift);
            if (ch < 0) {
                continue;
            }

            if (scan_len + 1 >= sizeof(scan_buf)) {
                if (flush_scan(&ctx, scan_buf, &scan_len) != 0) {
                    exit_code = 1;
                    goto out;
                }
            }
            scan_buf[scan_len++] = (char)ch;
            have_pending_scan = true;
        }
    }

out:
    ioctl(in_fd, EVIOCGRAB, 0);
    if (emitter_started) {
        scan_queue_stop(&ctx.queue);
        pthread_join(emitter_thread, NULL);
    }
    scan_queue_destroy(&ctx.queue);
    destroy_uinput(out_fd);
    close(in_fd);
    return exit_code;
}

int main(int argc, char **argv) {
    struct app_config config;
    if (parse_args(argc, argv, &config) != 0) {
        usage(argv[0]);
        return 2;
    }

    if (!find_layout_profile(config.output_layout)) {
        fprintf(stderr, "Layout de salida desconocido: %s\n", config.output_layout);
        fprintf(stderr, "Layouts disponibles:\n");
        print_layouts(stderr);
        return 2;
    }

    const char *configured_device = config.device;

    for (;;) {
        char detected_device[64];
        if (!configured_device) {
            while (detect_event_device(detected_device, sizeof(detected_device)) != 0) {
                if (!config.reconnect) {
                    fprintf(stderr, "No encontre el escaner %s:%s. Indica /dev/input/eventX manualmente.\n",
                            DEFAULT_VENDOR_ID, DEFAULT_MODEL_ID);
                    return 1;
                }
                fprintf(stderr, "No encontre el escaner %s:%s; reintentando en %d ms.\n",
                        DEFAULT_VENDOR_ID, DEFAULT_MODEL_ID, config.reconnect_delay_ms);
                usleep((useconds_t)config.reconnect_delay_ms * 1000U);
            }
            config.device = detected_device;
        } else {
            config.device = configured_device;
        }

        int rc = run_bridge(&config);
        if (!config.reconnect || rc != RECONNECT_EXIT_CODE) {
            return rc;
        }

        fprintf(stderr, "Reintentando conexion del escaner en %d ms.\n", config.reconnect_delay_ms);
        usleep((useconds_t)config.reconnect_delay_ms * 1000U);
    }
}
