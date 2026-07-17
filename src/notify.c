#include "psdp_notify.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ps5/klog.h>

typedef struct { char pad[45]; char message[3075]; } psdp_notify_request_t;
extern int sceKernelSendNotificationRequest(int, void *, size_t, int);
void psdp_notify(const char *fmt, ...) { psdp_notify_request_t req; va_list ap; memset(&req,0,sizeof(req)); va_start(ap,fmt); vsnprintf(req.message,sizeof(req.message),fmt,ap); va_end(ap); klog_printf("PS-DiscordPresence: %s",req.message); sceKernelSendNotificationRequest(0,&req,sizeof(req),0); }
