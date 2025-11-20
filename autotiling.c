// autotiling.c
// Minimal pure-C autotiling for i3 / sway using native IPC socket (no json lib).
// Build: make
// Run: ./autotiling [options]
// Copyright: adapted from the Python autotiling logic.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <pwd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <ctype.h>
#include <getopt.h>

#define I3_IPC_MAGIC "i3-ipc"
#define I3_IPC_HEADER_LEN 14

// i3 message types (subset)
enum {
    I3_IPC_MESSAGE_TYPE_COMMAND = 0,
    I3_IPC_MESSAGE_TYPE_GET_WORKSPACES = 1,
    I3_IPC_MESSAGE_TYPE_SUBSCRIBE = 2,
    I3_IPC_MESSAGE_TYPE_GET_OUTPUTS = 3,
    I3_IPC_MESSAGE_TYPE_GET_TREE = 4,
    I3_IPC_MESSAGE_TYPE_GET_MARKS = 5,
    I3_IPC_MESSAGE_TYPE_GET_BAR_CONFIG = 6,
    I3_IPC_MESSAGE_TYPE_GET_VERSION = 7,
    I3_IPC_MESSAGE_TYPE_GET_BINDING_MODES = 8,
    I3_IPC_MESSAGE_TYPE_GET_CONFIG = 9,
    I3_IPC_MESSAGE_TYPE_SEND_TICK = 10,
    I3_IPC_MESSAGE_TYPE_SYNC = 11
};

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static int verbose = 0;

static char *get_socket_path() {
    // Prefer SWAYSOCK then I3SOCK then XDG runtime default
    const char *p;
    if ((p = getenv("SWAYSOCK")) && p[0]) return strdup(p);
    if ((p = getenv("I3SOCK")) && p[0]) return strdup(p);

    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (!xdg) {
        // fallback to /run/user/<uid>
        uid_t uid = getuid();
        char *buf = malloc(64);
        if (!buf) return NULL;
        snprintf(buf, 64, "/run/user/%d", (int)uid);
        xdg = buf;
    }

    // try common names
    char *trial;
    trial = malloc(strlen(xdg) + 32);
    if (!trial) return NULL;
    snprintf(trial, strlen(xdg) + 32, "%s/i3/ipc-socket.0", xdg);
    if (access(trial, F_OK) == 0) return trial;
    free(trial);

    trial = malloc(strlen(xdg) + 32);
    snprintf(trial, strlen(xdg) + 32, "%s/sway-ipc.0", xdg);
    if (access(trial, F_OK) == 0) return trial;
    free(trial);

    // final fallback: SWAYSOCK/I3SOCK not set and we couldn't find default
    return NULL;
}

static int connect_socket(const char *sockpath) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(sockpath) >= sizeof(addr.sun_path))
        return -1;
    strncpy(addr.sun_path, sockpath, sizeof(addr.sun_path)-1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_ipc(int sock, uint32_t type, const char *payload) {
    uint32_t plen = payload ? (uint32_t) strlen(payload) : 0;
    uint32_t leplen = plen;
    uint32_t letype = type;
    // header: "i3-ipc" + 4 bytes length (LE) + 4 bytes type (LE)
    char header[I3_IPC_HEADER_LEN+1];
    memset(header, 0, sizeof(header));
    memcpy(header, I3_IPC_MAGIC, strlen(I3_IPC_MAGIC));
    // write length and type in little-endian
    header[6] = (leplen) & 0xff;
    header[7] = (leplen >> 8) & 0xff;
    header[8] = (leplen >> 16) & 0xff;
    header[9] = (leplen >> 24) & 0xff;
    header[10] = (letype) & 0xff;
    header[11] = (letype >> 8) & 0xff;
    header[12] = (letype >> 16) & 0xff;
    header[13] = (letype >> 24) & 0xff;

    ssize_t r = write(sock, header, I3_IPC_HEADER_LEN);
    if (r != I3_IPC_HEADER_LEN) return -1;
    if (plen) {
        r = write(sock, payload, plen);
        if (r != (ssize_t)plen) return -1;
    }
    return 0;
}

static char *recv_ipc(int sock, uint32_t *ptype_out, uint32_t *plen_out) {
    char header[I3_IPC_HEADER_LEN];
    ssize_t r = read(sock, header, I3_IPC_HEADER_LEN);
    if (r <= 0) return NULL;
    if (r != I3_IPC_HEADER_LEN) {
        // try to read remaining
        ssize_t need = I3_IPC_HEADER_LEN - r;
        ssize_t rr = read(sock, header + r, need);
        if (rr <= 0) return NULL;
    }
    if (memcmp(header, I3_IPC_MAGIC, strlen(I3_IPC_MAGIC)) != 0) {
        return NULL;
    }
    uint32_t plen = (uint8_t)header[6] | ((uint8_t)header[7] << 8) | ((uint8_t)header[8] << 16) | ((uint8_t)header[9] << 24);
    uint32_t ptype = (uint8_t)header[10] | ((uint8_t)header[11] << 8) | ((uint8_t)header[12] << 16) | ((uint8_t)header[13] << 24);
    char *buf = malloc(plen + 1);
    if (!buf) return NULL;
    size_t got = 0;
    while (got < plen) {
        ssize_t n = read(sock, buf + got, plen - got);
        if (n <= 0) { free(buf); return NULL; }
        got += n;
    }
    buf[plen] = '\0';
    if (ptype_out) *ptype_out = ptype;
    if (plen_out) *plen_out = plen;
    return buf;
}

// Minimal helpers to find fields in the get_tree JSON
// We look for the first occurrence of "\"focused\": true" and then
// parse near it for rect.width rect.height percent fullscreen_mode and layout of parent
static int extract_focused_info(const char *tree_json,
                                int *width, int *height,
                                double *percent, int *fullscreen_mode,
                                int *is_floating, char *parent_layout, size_t parent_layout_len,
                                char *output_name, size_t output_len,
                                char *workspace_num, size_t ws_len) {
    const char *p = strstr(tree_json, "\"focused\": true");
    if (!p) p = strstr(tree_json, "\"focused\":true");
    if (!p) return 0;

    // extract rect: look forward from p
    const char *rect = strstr(p, "\"rect\":");
    if (!rect) return 0;
    const char *bw = strstr(rect, "\"width\":");
    const char *bh = strstr(rect, "\"height\":");
    if (!bw || !bh) return 0;
    int w=0,h=0;
    if (sscanf(bw, "\"width\":%d", &w) < 1) return 0;
    if (sscanf(bh, "\"height\":%d", &h) < 1) return 0;
    if (width) *width = w;
    if (height) *height = h;

    // percent (optional)
    double pct = 0.0;
    const char *pp = strstr(p, "\"percent\":");
    if (pp) {
        sscanf(pp, "\"percent\":%lf", &pct);
    }
    if (percent) *percent = pct;

    // fullscreen_mode
    int fs = 0;
    const char *fp = strstr(p, "\"fullscreen_mode\":");
    if (fp) {
        sscanf(fp, "\"fullscreen_mode\":%d", &fs);
    }
    if (fullscreen_mode) *fullscreen_mode = fs;

    // floating: check "type":"floating_con" near p or "floating" attribute
    int floating = 0;
    const char *tp = strstr(p, "\"type\":");
    if (tp) {
        char typebuf[64] = {0};
        if (sscanf(tp, "\"type\":\"%63[^\"]", typebuf) == 1) {
            if (strcmp(typebuf, "floating_con") == 0) floating = 1;
        }
    }
    // also inspect "floating" attribute earlier
    const char *floating_attr = strstr(p, "\"floating\":");
    if (floating_attr) {
        if (strstr(floating_attr, "\"auto_on\"") || strstr(floating_attr, "\"user_on\"")) floating = 1;
    }
    if (is_floating) *is_floating = floating;

    // parent layout: heuristic -- search backwards from p for the closest "\"layout\":\"...\""
    const char *q = p;
    const char *found_layout = NULL;
    for (int i = 0; i < 1024; ++i) {
        // move backwards by chunks
        size_t step = 128;
        if ((size_t)(q - tree_json) < step) q = tree_json;
        else q -= step;
        const char *cand = q == tree_json ? tree_json : q;
        // find last occurrence of "\"layout\":\"" before p
        const char *r = p;
        const char *last = NULL;
        while (1) {
            const char *tmp = strstr(cand, "\"layout\":");
            if (!tmp || tmp >= p) break;
            last = tmp;
            cand = tmp + 1;
        }
        if (last) { found_layout = last; break; }
        if (q == tree_json) break;
    }
    if (found_layout) {
        char lay[64] = {0};
        if (sscanf(found_layout, "\"layout\":\"%63[^\"]", lay) == 1) {
            strncpy(parent_layout, lay, parent_layout_len-1);
        }
    } else {
        parent_layout[0] = '\0';
    }

    // output name: search backwards for a node with "type":"output" and find "name"
    const char *outpos = p;
    const char *found_output = NULL;
    while (outpos != tree_json) {
        const char *cand = tree_json;
        const char *last = NULL;
        while (1) {
            const char *tmp = strstr(cand, "\"type\":\"output\"");
            if (!tmp || tmp >= p) break;
            last = tmp;
            cand = tmp + 1;
        }
        if (last) { found_output = last; break; }
        break;
    }
    if (found_output) {
        const char *namepos = found_output;
        const char *n = strstr(namepos, "\"name\":");
        if (n) {
            char nb[128] = {0};
            if (sscanf(n, "\"name\":\"%127[^\"]", nb) == 1) {
                strncpy(output_name, nb, output_len-1);
            }
        }
    } else {
        output_name[0] = '\0';
    }

    // workspace num: search backwards for "\"type\":\"workspace\"" near p and parse "num"
    const char *wpos = tree_json;
    const char *lastws = NULL;
    while (1) {
        const char *tmp = strstr(wpos, "\"type\":\"workspace\"");
        if (!tmp || tmp >= p) break;
        lastws = tmp;
        wpos = tmp + 1;
    }
    if (lastws) {
        const char *numpos = strstr(lastws, "\"num\":");
        if (numpos) {
            int num=0;
            if (sscanf(numpos, "\"num\":%d", &num) == 1) {
                snprintf(workspace_num, ws_len, "%d", num);
            }
        }
    } else {
        workspace_num[0] = '\0';
    }

    return 1;
}

// wrapper to send i3 command string
static int i3_command(int sock, const char *cmd) {
    if (verbose) fprintf(stderr, ">>> command: %s\n", cmd);
    return send_ipc(sock, I3_IPC_MESSAGE_TYPE_COMMAND, cmd);
}

struct config {
    int debug;
    double splitratio;
    double splitwidth;
    double splitheight;
    int limit;
    char **outputs;
    int n_outputs;
    char **workspaces;
    int n_workspaces;
    char **events;
    int n_events;
};

static int in_list(char **list, int n, const char *s) {
    if (!list || n==0) return 0;
    for (int i=0;i<n;i++) if (list[i] && strcmp(list[i], s)==0) return 1;
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -d,--debug                debug output\n"
        "  -o,--outputs O1 O2        restrict to outputs\n"
        "  -w,--workspaces W1 W2     restrict to workspaces\n"
        "  -l,--limit N              depth limit (0 none)\n"
        "  -sw,--splitwidth F        split width factor (default 1.0)\n"
        "  -sh,--splitheight F       split height factor (default 1.0)\n"
        "  -sr,--splitratio F        split ratio (default 1.0)\n"
        "  -e,--events E1 E2         events to subscribe (default WINDOW MODE)\n",
        prog);
}

int main(int argc, char **argv) {
    struct config cfg;
    memset(&cfg,0,sizeof(cfg));
    cfg.debug = 0;
    cfg.splitratio = 1.0;
    cfg.splitwidth = 1.0;
    cfg.splitheight = 1.0;
    cfg.limit = 0;
    cfg.events = NULL;
    cfg.n_events = 0;

    // Simple getopt long parsing (positional lists separated by commas or repeated -o)
    static struct option longopts[] = {
        {"debug", no_argument, 0, 'd'},
        {"outputs", required_argument, 0, 'o'},
        {"workspaces", required_argument, 0, 'w'},
        {"limit", required_argument, 0, 'l'},
        {"splitwidth", required_argument, 0, 0},
        {"splitheight", required_argument, 0, 0},
        {"splitratio", required_argument, 0, 0},
        {"events", required_argument, 0, 'e'},
        {0,0,0,0}
    };

    // We'll accept repeated -o and -w; for simplicity -o arg may be comma-separated list
    int opt;
    while ((opt = getopt_long(argc, argv, "do:w:l:e:", longopts, NULL)) != -1) {
        if (opt == 'd') cfg.debug = 1;
        else if (opt == 'o') {
            // split on commas and add
            char *tok = strtok(optarg, ",");
            while (tok) {
                cfg.outputs = realloc(cfg.outputs, sizeof(char*)*(cfg.n_outputs+1));
                cfg.outputs[cfg.n_outputs++] = strdup(tok);
                tok = strtok(NULL, ",");
            }
        } else if (opt == 'w') {
            char *tok = strtok(optarg, ",");
            while (tok) {
                cfg.workspaces = realloc(cfg.workspaces, sizeof(char*)*(cfg.n_workspaces+1));
                cfg.workspaces[cfg.n_workspaces++] = strdup(tok);
                tok = strtok(NULL, ",");
            }
        } else if (opt == 'l') {
            cfg.limit = atoi(optarg);
        } else if (opt == 'e') {
            // comma separated events
            char *tok = strtok(optarg, ",");
            while (tok) {
                cfg.events = realloc(cfg.events, sizeof(char*)*(cfg.n_events+1));
                cfg.events[cfg.n_events++] = strdup(tok);
                tok = strtok(NULL, ",");
            }
        } else if (opt == 0) {
            const char *name = longopts[optind-1].name;
            if (strcmp(name, "splitwidth")==0) cfg.splitwidth = atof(optarg);
            else if (strcmp(name, "splitheight")==0) cfg.splitheight = atof(optarg);
            else if (strcmp(name, "splitratio")==0) cfg.splitratio = atof(optarg);
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (cfg.n_events == 0) {
        // default events WINDOW and MODE
        cfg.events = malloc(sizeof(char*)*2);
        cfg.events[cfg.n_events++] = strdup("window");
        cfg.events[cfg.n_events++] = strdup("mode");
    }

    verbose = cfg.debug;

    if (verbose) {
        fprintf(stderr, "autotiling (C) starting. debug=%d\n", cfg.debug);
        if (cfg.n_outputs) {
            fprintf(stderr, "outputs limited to:");
            for (int i=0;i<cfg.n_outputs;i++) fprintf(stderr," %s", cfg.outputs[i]);
            fprintf(stderr, "\n");
        }
        if (cfg.n_workspaces) {
            fprintf(stderr, "workspaces limited to:");
            for (int i=0;i<cfg.n_workspaces;i++) fprintf(stderr," %s", cfg.workspaces[i]);
            fprintf(stderr, "\n");
        }
    }

    char *sockpath = get_socket_path();
    if (!sockpath) die("Could not determine i3/sway socket. Set SWAYSOCK or I3SOCK env or adjust code.");
    if (verbose) fprintf(stderr, "Using socket: %s\n", sockpath);

    int sock = connect_socket(sockpath);
    if (sock < 0) die("Could not connect to socket %s: %s", sockpath, strerror(errno));

    // subscribe to events
    // build json array of events, event names should be lowercase for our minimal client
    size_t jslen = 3;
    for (int i=0;i<cfg.n_events;i++) jslen += strlen(cfg.events[i]) + 3;
    char *jpayload = malloc(jslen+1);
    if (!jpayload) die("malloc");
    strcpy(jpayload, "[");
    for (int i=0;i<cfg.n_events;i++) {
        if (i) strcat(jpayload, ",");
        strcat(jpayload, "\"");
        strcat(jpayload, cfg.events[i]);
        strcat(jpayload, "\"");
    }
    strcat(jpayload, "]");

    if (send_ipc(sock, I3_IPC_MESSAGE_TYPE_SUBSCRIBE, jpayload) != 0) {
        die("Subscribe failed: %s", strerror(errno));
    }
    free(jpayload);

    // read subscribe reply
    uint32_t ptype=0, plen=0;
    char *reply = recv_ipc(sock, &ptype, &plen);
    if (!reply) die("No reply to subscribe");
    if (verbose) {
        fprintf(stderr, "Subscribed. server reply type=%u len=%u\n", ptype, plen);
    }
    free(reply);

    // Main loop: read events and handle
    while (1) {
        uint32_t etype=0, elen=0;
        char *event = recv_ipc(sock, &etype, &elen);
        if (!event) {
            if (verbose) fprintf(stderr, "Event receive failed, reconnecting...\n");
            close(sock);
            sock = connect_socket(sockpath);
            if (sock < 0) die("Re-connect failed");
            continue;
        }

        // event payload is JSON with "change" and "container" etc depending on event
        // For simplicity: on any event we fetch tree, find focused container and act.
        if (verbose) fprintf(stderr, "Event payload (first 120 chars): %.120s\n", event);

        // get the whole tree
        if (send_ipc(sock, I3_IPC_MESSAGE_TYPE_GET_TREE, "") != 0) {
            free(event);
            die("GET_TREE send failed");
        }
        uint32_t ttype=0, tlen=0;
        char *tree = recv_ipc(sock, &ttype, &tlen);
        if (!tree) {
            free(event);
            die("GET_TREE recv failed");
        }

        int w=0,h=0, fullscreen=0, floating=0;
        double percent=0.0;
        char parent_layout[64] = {0};
        char output_name[128] = {0};
        char workspace_num[32] = {0};
        int ok = extract_focused_info(tree, &w, &h, &percent, &fullscreen, &floating, parent_layout, sizeof(parent_layout), output_name, sizeof(output_name), workspace_num, sizeof(workspace_num));
        if (!ok) {
            if (cfg.debug) fprintf(stderr, "Debug: no focused container found in tree\n");
            free(tree);
            free(event);
            continue;
        }

        if (cfg.n_outputs && output_name[0]) {
            if (!in_list(cfg.outputs, cfg.n_outputs, output_name)) {
                if (cfg.debug) fprintf(stderr, "Debug: Autotiling turned off on output %s\n", output_name);
                free(tree);
                free(event);
                continue;
            }
        }

        if (cfg.n_workspaces && workspace_num[0]) {
            if (!in_list(cfg.workspaces, cfg.n_workspaces, workspace_num)) {
                if (cfg.debug) fprintf(stderr, "Debug: Autotiling turned off on workspace %s\n", workspace_num);
                free(tree);
                free(event);
                continue;
            }
        }

        if (floating || fullscreen) {
            if (cfg.debug) fprintf(stderr, "Debug: container is floating or fullscreen, skipping\n");
            free(tree);
            free(event);
            continue;
        }

        // decide split direction: "splitv" if height > width / splitratio else "splith"
        double ratio = cfg.splitratio;
        int want_splitv = (h > (int)( (double)w / ratio ));

        // parent_layout contains closest found layout (heuristic); if it's equal to new layout skip
        const char *current_layout = parent_layout;
        const char *new_layout = want_splitv ? "splitv" : "splith";
        if (strlen(current_layout) == 0 || ( (strcmp(current_layout, "splitv")!=0 && strcmp(current_layout, "splith")!=0) || ( (strcmp(current_layout,"splitv")==0) != want_splitv) )) {
            // apply change
            if (i3_command(sock, new_layout) != 0) {
                if (cfg.debug) fprintf(stderr, "Debug: failed to send new_layout %s\n", new_layout);
            } else if (cfg.debug) {
                fprintf(stderr, "Debug: switched to %s\n", new_layout);
            }
        } else {
            if (cfg.debug) fprintf(stderr, "Debug: layout already %s\n", current_layout);
        }

        // if event is new or move, we may want to resize based on percent
        // Heuristic: check if event JSON contains "change":"new" or "change":"move"
        int do_resize = 0;
        if (strstr(event, "\"change\":\"new\"") || strstr(event, "\"change\":\"move\"")) do_resize = 1;

        if (do_resize && percent > 0.0001) {
            char cmd[128] = {0};
            if (want_splitv && cfg.splitheight != 1.0) {
                int ppt = (int)(percent * cfg.splitheight * 100.0);
                snprintf(cmd, sizeof(cmd), "resize set height %d ppt", ppt);
                i3_command(sock, cmd);
            } else if (!want_splitv && cfg.splitwidth != 1.0) {
                int ppt = (int)(percent * cfg.splitwidth * 100.0);
                snprintf(cmd, sizeof(cmd), "resize set width %d ppt", ppt);
                i3_command(sock, cmd);
            }
        }

        free(tree);
        free(event);
    }

    close(sock);
    free(sockpath);
    return 0;
}

