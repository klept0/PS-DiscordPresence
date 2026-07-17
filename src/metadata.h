#ifndef PRESENCE_METADATA_H
#define PRESENCE_METADATA_H
#include <stddef.h>
struct presence_metadata { char title_id[16]; char title_name[192]; char icon_path[320]; };
int presence_title_id_valid(const char *id);
int presence_metadata_resolve_at(const char *root, const char *title_id, struct presence_metadata *out);
int presence_metadata_resolve(const char *title_id, struct presence_metadata *out);
#endif
