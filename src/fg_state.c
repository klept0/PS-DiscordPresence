#include "fg_state.h"

#include <ctype.h>
#include <errno.h>
#include <ps5/kernel.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>

#ifndef CTL_KERN
#error "sys/sysctl.h did not provide CTL_KERN"
#endif

struct proc_offsets {
    uint32_t titleid;
    uint32_t contentid;
};

struct parsed_log {
    uint32_t controller_focus;
    uint32_t game_appid;
    char focus_to[96];
    char lifecycle[32];
    char background_title[16];
    char last_line[256];
    int saw_controller;
    int saw_game_video;
    int saw_focus;
    int saw_lifecycle;
};

static void zcopy(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;
    snprintf(dst, dstsz, "%s", src);
}

static int starts_titleid(const char *s)
{
    if (!s)
        return 0;
    for (int i = 0; i < 4; i++) {
        if (s[i] < 'A' || s[i] > 'Z')
            return 0;
    }
    for (int i = 4; i < 9; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
    }
    return 1;
}

static int find_titleid_bytes(const uint8_t *buf, size_t len, char out[16])
{
    if (!buf || len < 9 || !out)
        return -1;
    for (size_t i = 0; i + 9 <= len; i++) {
        if (starts_titleid((const char *)buf + i)) {
            memcpy(out, buf + i, 9);
            out[9] = '\0';
            return 0;
        }
    }
    return -1;
}

static int find_contentid_bytes(const uint8_t *buf, size_t len, char *out, size_t outsz)
{
    if (!buf || len < 17 || !out || outsz == 0)
        return -1;
    out[0] = '\0';
    for (size_t i = 0; i + 17 <= len; i++) {
        const uint8_t *s = buf + i;
        int ok = 1;
        for (int j = 0; j < 2; j++)
            ok &= (s[j] >= 'A' && s[j] <= 'Z');
        for (int j = 2; j < 6; j++)
            ok &= (s[j] >= '0' && s[j] <= '9');
        ok &= (s[6] == '-');
        for (int j = 7; j < 11; j++)
            ok &= (s[j] >= 'A' && s[j] <= 'Z');
        for (int j = 11; j < 16; j++)
            ok &= (s[j] >= '0' && s[j] <= '9');
        ok &= (s[16] == '_');
        if (!ok)
            continue;
        size_t k = 0;
        while (i + k < len && s[k] != 0 && k + 1 < outsz) {
            out[k] = (char)s[k];
            k++;
        }
        out[k] = '\0';
        return 0;
    }
    return -1;
}

static void get_proc_offsets(struct proc_offsets *out)
{
    uint32_t fw = kernel_get_fw_version() & 0xffff0000u;
    memset(out, 0, sizeof(*out));
    out->titleid = fw >= 0x8000000u ? 0x470u :
                   fw >= 0x7000000u ? 0x49Au :
                   fw >= 0x6000000u ? 0x498u : 0x470u;
    out->contentid = fw >= 0x12000000u ? 0x504u :
                     fw >= 0x8000000u  ? 0x4F4u :
                     fw >= 0x7000000u  ? 0x4FCu :
                     fw >= 0x6000000u  ? 0x4ECu : 0x4C4u;
}

static int is_eboot_comm(const char *comm)
{
    return comm && strncmp(comm, "eboot.bin", 9) == 0
           && (comm[9] == '\0' || comm[9] == ' ');
}

static int find_eboot(struct fg_state_response *out)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t sz = 0;
    uint8_t *buf = NULL;
    struct kinfo_proc *best = NULL;
    int best_pid = 0;

    if (sysctl(mib, 4, NULL, &sz, NULL, 0) != 0 || sz == 0)
        return -1;
    buf = malloc(sz);
    if (!buf)
        return -1;
    if (sysctl(mib, 4, buf, &sz, NULL, 0) != 0) {
        free(buf);
        return -1;
    }

    for (uint8_t *p = buf; p < buf + sz; ) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        if (ki->ki_structsize <= 0)
            break;
        if (is_eboot_comm(ki->ki_comm) && ki->ki_pid > best_pid) {
            best = ki;
            best_pid = ki->ki_pid;
        }
        p += ki->ki_structsize;
    }

    if (!best) {
        free(buf);
        return 0;
    }

    out->pid = (uint32_t)best->ki_pid;
    out->proc_state = best->ki_stat;
    out->runtime_us = best->ki_runtime;
    out->pctcpu = best->ki_pctcpu;
    out->slptime = best->ki_slptime;
    zcopy(out->comm, sizeof(out->comm), best->ki_comm);

    struct proc_offsets off;
    get_proc_offsets(&off);
    intptr_t kproc = kernel_get_proc(best->ki_pid);
    if (kproc) {
        uint8_t proc_buf[0x800];
        memset(proc_buf, 0, sizeof(proc_buf));
        if (kernel_copyout(kproc, proc_buf, sizeof(proc_buf)) == 0) {
            if (find_contentid_bytes(proc_buf + 0x440, 0x200,
                                     out->contentid, sizeof(out->contentid)) != 0
                && off.contentid + sizeof(out->contentid) <= sizeof(proc_buf)) {
                memcpy(out->contentid, proc_buf + off.contentid,
                       sizeof(out->contentid) - 1);
                out->contentid[sizeof(out->contentid) - 1] = '\0';
            }
            if (find_titleid_bytes(proc_buf + 0x440, 0x200, out->titleid) != 0
                && off.titleid + 9 <= sizeof(proc_buf)) {
                memcpy(out->titleid, proc_buf + off.titleid, 9);
                out->titleid[9] = '\0';
            }
        }
    }

    free(buf);
    return 1;
}

static int read_msgbuf(char **out_buf, size_t *out_len)
{
    size_t sz = 0;
    char *buf;
    int rc;

    *out_buf = NULL;
    *out_len = 0;

    rc = sysctlbyname("kern.msgbuf", NULL, &sz, NULL, 0);
    if (rc != 0 || sz == 0) {
        
        sz = 262144;
    }
    if (sz > 524288)
        sz = 524288;
    buf = malloc(sz + 1);
    if (!buf)
        return -ENOMEM;
    memset(buf, 0, sz + 1);
    rc = sysctlbyname("kern.msgbuf", buf, &sz, NULL, 0);
    if (rc != 0) {
        int err = errno ? -errno : -1;
        free(buf);
        return err;
    }
    buf[sz] = '\0';
    *out_buf = buf;
    *out_len = sz;
    return 0;
}

static const char *find_substr(const char *hay, const char *needle)
{
    return strstr(hay, needle);
}

static int parse_hex_or_dec(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!s || !out)
        return -1;
    errno = 0;
    v = strtoul(s, &end, 0);
    if (errno != 0 || end == s)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_hex_digits(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!s || !out)
        return -1;
    errno = 0;
    v = strtoul(s, &end, 16);
    if (errno != 0 || end == s)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static void update_last_line(struct parsed_log *pl, const char *line, size_t len)
{
    size_t n = len;
    if (n >= sizeof(pl->last_line))
        n = sizeof(pl->last_line) - 1;
    memcpy(pl->last_line, line, n);
    pl->last_line[n] = '\0';
}

static void parse_line(const char *line, size_t len, const struct fg_state_response *state,
                       struct parsed_log *pl)
{
    char tmp[512];
    size_t n = len;
    if (n >= sizeof(tmp))
        n = sizeof(tmp) - 1;
    memcpy(tmp, line, n);
    tmp[n] = '\0';

    const char *p;
    if ((p = find_substr(tmp, "VideoOut: shared (pid=0x")) != NULL) {
        uint32_t pid = 0, appid = 0;
        p += strlen("VideoOut: shared (pid=0x");
        if (parse_hex_digits(p, &pid) == 0) {
            const char *a = strstr(p, "appId=0x");
            if (a && parse_hex_digits(a + strlen("appId=0x"), &appid) == 0) {
                if (pid == state->pid && pid != 0) {
                    pl->game_appid = appid;
                    pl->saw_game_video = 1;
                    update_last_line(pl, line, len);
                }
            }
        }
    }

    if ((p = find_substr(tmp, "SetControllerFocus(")) != NULL) {
        p += strlen("SetControllerFocus(");
        if (p[0] != '-') {
            uint32_t focus = 0;
            if (parse_hex_or_dec(p, &focus) == 0) {
                pl->controller_focus = focus;
                pl->saw_controller = 1;
                update_last_line(pl, line, len);
            }
        }
    }

    if ((p = find_substr(tmp, "OnFocusActiveSceneChanged")) != NULL) {
        const char *arrow = strstr(tmp, "-> [");
        if (arrow) {
            arrow += 4;
            const char *end = strchr(arrow, ']');
            size_t copy = end && end > arrow ? (size_t)(end - arrow) : strlen(arrow);
            if (copy >= sizeof(pl->focus_to))
                copy = sizeof(pl->focus_to) - 1;
            memcpy(pl->focus_to, arrow, copy);
            pl->focus_to[copy] = '\0';
            pl->saw_focus = 1;
            update_last_line(pl, line, len);
        }
    }

    if (find_substr(tmp, "resumeApp()") || find_substr(tmp, "sceApplicationResume")
        || (find_substr(tmp, "AppId=") && find_substr(tmp, "resumed"))) {
        zcopy(pl->lifecycle, sizeof(pl->lifecycle), "resume");
        pl->saw_lifecycle = 1;
        update_last_line(pl, line, len);
    }
    if (find_substr(tmp, "suspendApp()") || find_substr(tmp, "sceApplicationSuspend")
        || (find_substr(tmp, "AppId=") && find_substr(tmp, "suspended"))) {
        zcopy(pl->lifecycle, sizeof(pl->lifecycle), "suspend");
        pl->saw_lifecycle = 1;
        update_last_line(pl, line, len);
    }

    if ((p = find_substr(tmp, "/user/app/")) != NULL) {
        p += strlen("/user/app/");
        if (starts_titleid(p)) {
            memcpy(pl->background_title, p, 9);
            pl->background_title[9] = '\0';
        }
    }
}

static void parse_msgbuf(const char *buf, size_t len, const struct fg_state_response *state,
                         struct parsed_log *pl)
{
    memset(pl, 0, sizeof(*pl));
    const char *cur = buf;
    const char *end = buf + len;
    while (cur < end && *cur) {
        const char *nl = memchr(cur, '\n', (size_t)(end - cur));
        size_t line_len = nl ? (size_t)(nl - cur) : strlen(cur);
        if (line_len > 0)
            parse_line(cur, line_len, state, pl);
        if (!nl)
            break;
        cur = nl + 1;
    }
}

static int scene_contains(const char *scene, const char *needle)
{
    return scene && scene[0] && strstr(scene, needle) != NULL;
}

static void classify(struct fg_state_response *out, const struct parsed_log *pl)
{
    out->game_appid = pl->game_appid;
    out->controller_focus = pl->controller_focus;
    zcopy(out->focus_to, sizeof(out->focus_to), pl->focus_to);
    zcopy(out->lifecycle, sizeof(out->lifecycle), pl->lifecycle);
    zcopy(out->last_line, sizeof(out->last_line), pl->last_line);

    if (out->pid == 0) {
        out->classification = FG_STATE_CLASS_NOT_LOADED;
        out->confidence = 2;
        zcopy(out->reason, sizeof(out->reason), "no eboot.bin process is loaded");
        return;
    }

    if (pl->saw_game_video && pl->saw_controller && pl->game_appid != 0
        && pl->controller_focus == pl->game_appid) {
        out->classification = FG_STATE_CLASS_ACTIVE_RUNNING;
        out->confidence = 2;
        snprintf(out->reason, sizeof(out->reason),
                 "controller focus 0x%08x matches game appId 0x%08x for pid %u",
                 pl->controller_focus, pl->game_appid, out->pid);
        return;
    }

    if (pl->saw_controller && pl->controller_focus == 0x7) {
        out->classification = FG_STATE_CLASS_SUSPENDED_OR_HOME;
        out->confidence = pl->saw_game_video ? 2 : 1;
        snprintf(out->reason, sizeof(out->reason),
                 "controller focus is Shell/Home 0x00000007 while title %s remains loaded",
                 out->titleid[0] ? out->titleid : "<unknown>");
        return;
    }

    if (scene_contains(pl->focus_to, "NPXS40002") || scene_contains(pl->focus_to, "NPXS40003")
        || scene_contains(pl->focus_to, "ReactModalScene") || scene_contains(pl->focus_to, "FocusCapture")) {
        out->classification = FG_STATE_CLASS_SUSPENDED_OR_HOME;
        out->confidence = 1;
        snprintf(out->reason, sizeof(out->reason),
                 "latest ShellUI focus scene is %s while a title is loaded", pl->focus_to);
        return;
    }

    if (scene_contains(pl->focus_to, "AppScreen") || scene_contains(pl->focus_to, "ApplicationScreenScene")) {
        out->classification = FG_STATE_CLASS_ACTIVE_RUNNING;
        out->confidence = 1;
        snprintf(out->reason, sizeof(out->reason),
                 "latest ShellUI focus scene is %s", pl->focus_to);
        return;
    }

    out->classification = FG_STATE_CLASS_LOADED_UNKNOWN;
    out->confidence = 0;
    zcopy(out->reason, sizeof(out->reason),
          out->msgbuf_rc == 0 ? "title loaded but msgbuf has no decisive focus event" : "title loaded but kern.msgbuf read failed");
}

int fg_state_query(struct fg_state_response *out)
{
    char *msg = NULL;
    size_t msg_len = 0;
    struct parsed_log pl;
    int eboot_rc;

    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    out->version = FG_STATE_VERSION;
    out->classification = FG_STATE_CLASS_UNKNOWN;
    out->proc_state = -1;
    out->fw_version = kernel_get_fw_version();

    eboot_rc = find_eboot(out);
    if (eboot_rc < 0) {
        out->classification = FG_STATE_CLASS_UNKNOWN;
        out->confidence = 0;
        zcopy(out->reason, sizeof(out->reason), "sysctl KERN_PROC failed");
        return eboot_rc;
    }

    out->msgbuf_rc = read_msgbuf(&msg, &msg_len);
    out->msgbuf_size = (uint32_t)(msg_len > 0xffffffffu ? 0xffffffffu : msg_len);
    if (out->msgbuf_rc == 0 && msg) {
        out->msgbuf_scanned = out->msgbuf_size;
        parse_msgbuf(msg, msg_len, out, &pl);
        free(msg);
    } else {
        memset(&pl, 0, sizeof(pl));
    }

    classify(out, &pl);
    return 0;
}

const char *fg_state_class_name(uint32_t classification)
{
    switch (classification) {
    case FG_STATE_CLASS_NOT_LOADED:
        return "not_loaded";
    case FG_STATE_CLASS_LOADED_UNKNOWN:
        return "loaded_unknown";
    case FG_STATE_CLASS_ACTIVE_RUNNING:
        return "active_running";
    case FG_STATE_CLASS_SUSPENDED_OR_HOME:
        return "suspended_or_home";
    default:
        return "unknown";
    }
}
