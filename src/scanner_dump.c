#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SCAN_BUF_SIZE 1024
#define DEFAULT_IDLE_TIMEOUT_MS 250
#define DEFAULT_VENDOR_ID "34eb"
#define DEFAULT_MODEL_ID "1502"

struct app_config {
    const char *device;
    bool grab;
    int idle_timeout_ms;
};

static const char *key_name(unsigned int code) {
    switch (code) {
    case KEY_1: return "KEY_1";
    case KEY_2: return "KEY_2";
    case KEY_3: return "KEY_3";
    case KEY_4: return "KEY_4";
    case KEY_5: return "KEY_5";
    case KEY_6: return "KEY_6";
    case KEY_7: return "KEY_7";
    case KEY_8: return "KEY_8";
    case KEY_9: return "KEY_9";
    case KEY_0: return "KEY_0";
    case KEY_A: return "KEY_A";
    case KEY_B: return "KEY_B";
    case KEY_C: return "KEY_C";
    case KEY_D: return "KEY_D";
    case KEY_E: return "KEY_E";
    case KEY_F: return "KEY_F";
    case KEY_G: return "KEY_G";
    case KEY_H: return "KEY_H";
    case KEY_I: return "KEY_I";
    case KEY_J: return "KEY_J";
    case KEY_K: return "KEY_K";
    case KEY_L: return "KEY_L";
    case KEY_M: return "KEY_M";
    case KEY_N: return "KEY_N";
    case KEY_O: return "KEY_O";
    case KEY_P: return "KEY_P";
    case KEY_Q: return "KEY_Q";
    case KEY_R: return "KEY_R";
    case KEY_S: return "KEY_S";
    case KEY_T: return "KEY_T";
    case KEY_U: return "KEY_U";
    case KEY_V: return "KEY_V";
    case KEY_W: return "KEY_W";
    case KEY_X: return "KEY_X";
    case KEY_Y: return "KEY_Y";
    case KEY_Z: return "KEY_Z";
    case KEY_MINUS: return "KEY_MINUS";
    case KEY_EQUAL: return "KEY_EQUAL";
    case KEY_LEFTBRACE: return "KEY_LEFTBRACE";
    case KEY_RIGHTBRACE: return "KEY_RIGHTBRACE";
    case KEY_BACKSLASH: return "KEY_BACKSLASH";
    case KEY_SEMICOLON: return "KEY_SEMICOLON";
    case KEY_APOSTROPHE: return "KEY_APOSTROPHE";
    case KEY_GRAVE: return "KEY_GRAVE";
    case KEY_COMMA: return "KEY_COMMA";
    case KEY_DOT: return "KEY_DOT";
    case KEY_SLASH: return "KEY_SLASH";
    case KEY_SPACE: return "KEY_SPACE";
    case KEY_ENTER: return "KEY_ENTER";
    case KEY_TAB: return "KEY_TAB";
    case KEY_LEFTSHIFT: return "KEY_LEFTSHIFT";
    case KEY_RIGHTSHIFT: return "KEY_RIGHTSHIFT";
    default: return "KEY_UNKNOWN";
    }
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

static const char *event_value_name(int value) {
    switch (value) {
    case 0: return "UP";
    case 1: return "DOWN";
    case 2: return "REPEAT";
    default: return "VALUE";
    }
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Uso: %s [--grab] [--idle-ms N] [/dev/input/eventX]\n"
            "\n"
            "Lee eventos EV_KEY del escaner y reconstruye texto con layout US.\n"
            "Si no se indica dispositivo, intenta autodetectar %s:%s en /sys/class/input.\n"
            "--grab evita que el dispositivo original escriba tambien en el escritorio.\n"
            "Para la primera prueba, empieza sin --grab.\n",
            argv0, DEFAULT_VENDOR_ID, DEFAULT_MODEL_ID);
}

static int parse_args(int argc, char **argv, struct app_config *config) {
    config->device = NULL;
    config->grab = false;
    config->idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--grab") == 0) {
            config->grab = true;
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

static void print_scan_buffer(const char *buf, size_t len, const char *reason) {
    if (len == 0) {
        return;
    }

    printf("SCAN[%s]: \"", reason);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
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

static int run_dump(const struct app_config *config) {
    int fd = open(config->device, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "No puedo abrir %s: %s\n", config->device, strerror(errno));
        return 1;
    }

    char input_name[256] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(input_name)), input_name) < 0) {
        snprintf(input_name, sizeof(input_name), "unknown");
    }

    printf("DEVICE: %s (%s)\n", config->device, input_name);
    printf("MODE: layout-in=US idle-timeout-ms=%d grab=%s\n",
           config->idle_timeout_ms, config->grab ? "yes" : "no");

    if (config->grab && ioctl(fd, EVIOCGRAB, 1) < 0) {
        fprintf(stderr, "EVIOCGRAB fallo en %s: %s\n", config->device, strerror(errno));
        close(fd);
        return 1;
    }

    bool left_shift = false;
    bool right_shift = false;
    char scan_buf[SCAN_BUF_SIZE];
    size_t scan_len = 0;
    bool have_pending_scan = false;

    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };

    for (;;) {
        int timeout = have_pending_scan ? config->idle_timeout_ms : -1;
        int pr = poll(&pfd, 1, timeout);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "poll fallo: %s\n", strerror(errno));
            break;
        }

        if (pr == 0) {
            print_scan_buffer(scan_buf, scan_len, "idle");
            scan_len = 0;
            have_pending_scan = false;
            continue;
        }

        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            fprintf(stderr, "El dispositivo dejo de estar disponible (revents=0x%x)\n", pfd.revents);
            break;
        }

        for (;;) {
            struct input_event ev;
            ssize_t n = read(fd, &ev, sizeof(ev));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "read fallo: %s\n", strerror(errno));
                goto out;
            }
            if (n == 0) {
                fprintf(stderr, "EOF inesperado\n");
                goto out;
            }
            if ((size_t)n != sizeof(ev)) {
                fprintf(stderr, "evento corto: %zd bytes\n", n);
                goto out;
            }
            if (ev.type != EV_KEY) {
                continue;
            }

            unsigned int code = ev.code;
            int value = ev.value;

            if (is_shift_key(code)) {
                if (code == KEY_LEFTSHIFT) {
                    left_shift = value != 0;
                } else {
                    right_shift = value != 0;
                }
            }

            bool shift = left_shift || right_shift;
            int ch = (value == 1) ? us_key_to_char(code, shift) : -1;

            printf("%ld.%06ld %-6s code=%u %-18s shift=%d",
                   (long)ev.time.tv_sec,
                   (long)ev.time.tv_usec,
                   event_value_name(value),
                   code,
                   key_name(code),
                   shift ? 1 : 0);

            if (ch >= 0) {
                printf(" char='%c'", ch);
            }
            printf("\n");
            fflush(stdout);

            if (value != 1) {
                continue;
            }

            if (code == KEY_ENTER || code == KEY_TAB) {
                print_scan_buffer(scan_buf, scan_len, code == KEY_ENTER ? "enter" : "tab");
                scan_len = 0;
                have_pending_scan = false;
                continue;
            }

            if (ch >= 0) {
                if (scan_len + 1 >= sizeof(scan_buf)) {
                    print_scan_buffer(scan_buf, scan_len, "overflow");
                    scan_len = 0;
                }
                scan_buf[scan_len++] = (char)ch;
                have_pending_scan = true;
            }
        }
    }

out:
    if (config->grab) {
        ioctl(fd, EVIOCGRAB, 0);
    }
    close(fd);
    return 1;
}

int main(int argc, char **argv) {
    struct app_config config;
    if (parse_args(argc, argv, &config) != 0) {
        usage(argv[0]);
        return 2;
    }

    char detected_device[64];
    if (!config.device) {
        if (detect_event_device(detected_device, sizeof(detected_device)) != 0) {
            fprintf(stderr, "No encontre el escaner %s:%s. Indica /dev/input/eventX manualmente.\n",
                    DEFAULT_VENDOR_ID, DEFAULT_MODEL_ID);
            return 1;
        }
        config.device = detected_device;
    }

    return run_dump(&config);
}
