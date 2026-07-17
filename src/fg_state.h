#ifndef FG_STATE_H
#define FG_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define FG_STATE_MAGIC 0x46534731u 
#define FG_STATE_VERSION 1u
#define FG_STATE_DEFAULT_PORT 9877u
#define FG_STATE_CMD_QUERY 1u
#define FG_STATE_STATUS_OK 0u
#define FG_STATE_STATUS_ERROR 0xf5000001u

#define FG_STATE_CLASS_UNKNOWN 0u
#define FG_STATE_CLASS_NOT_LOADED 1u
#define FG_STATE_CLASS_LOADED_UNKNOWN 2u
#define FG_STATE_CLASS_ACTIVE_RUNNING 3u
#define FG_STATE_CLASS_SUSPENDED_OR_HOME 4u

struct fg_state_packet {
    uint32_t magic;
    uint32_t cmd;
    uint32_t datalen;
} __attribute__((packed));

struct fg_state_response {
    uint32_t version;
    uint32_t classification;
    uint32_t confidence; 
    uint32_t pid;
    int32_t proc_state;
    uint64_t runtime_us;
    uint32_t pctcpu;
    uint32_t slptime;
    uint32_t game_appid;
    uint32_t controller_focus;
    int32_t msgbuf_rc;
    uint32_t msgbuf_size;
    uint32_t msgbuf_scanned;
    uint32_t fw_version;
    char titleid[16];
    char contentid[64];
    char comm[32];
    char focus_to[96];
    char lifecycle[32];
    char reason[160];
    char last_line[256];
} __attribute__((packed));

#define FG_STATE_RESPONSE_SIZE ((uint32_t)sizeof(struct fg_state_response))

int fg_state_query(struct fg_state_response *out);
const char *fg_state_class_name(uint32_t classification);

#endif 
