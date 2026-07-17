#include "presence.h"
#include <stdio.h>
#include <string.h>

const char *presence_class_name(uint32_t c) {
    switch (c) {
    case FG_STATE_CLASS_NOT_LOADED: return "not_loaded";
    case FG_STATE_CLASS_LOADED_UNKNOWN: return "loaded_unknown";
    case FG_STATE_CLASS_ACTIVE_RUNNING: return "active_running";
    case FG_STATE_CLASS_SUSPENDED_OR_HOME: return "suspended_or_home";
    default: return "unknown";
    }
}
const char *presence_action_name(enum presence_action a) {
    return a == PRESENCE_SET_ACTIVE ? "set_active" : a == PRESENCE_CLEAR ? "clear" : "no_change";
}
static void copy_snapshot(struct presence_snapshot *to, const struct fg_state_response *from) {
    memset(to, 0, sizeof(*to)); to->classification=from->classification; to->confidence=from->confidence;
    snprintf(to->titleid,sizeof(to->titleid),"%s",from->titleid);
    snprintf(to->contentid,sizeof(to->contentid),"%s",from->contentid);
    snprintf(to->reason,sizeof(to->reason),"%s",from->reason);
}
enum presence_action presence_reduce(const struct presence_snapshot *previous, const struct fg_state_response *current, struct presence_snapshot *next) {
    int was_active = previous && previous->classification == FG_STATE_CLASS_ACTIVE_RUNNING;
    int is_active = current && current->classification == FG_STATE_CLASS_ACTIVE_RUNNING;
    if (!current || !next) return PRESENCE_NO_CHANGE;
    copy_snapshot(next,current);
    if (is_active && (!was_active || strcmp(previous->titleid,current->titleid))) return PRESENCE_SET_ACTIVE;
    if (!is_active && was_active) return PRESENCE_CLEAR;
    return PRESENCE_NO_CHANGE;
}
