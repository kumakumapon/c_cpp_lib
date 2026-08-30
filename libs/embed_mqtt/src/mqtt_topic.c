#include "embed_mqtt/mqtt_topic.h"
#include <string.h>

bool mqtt_topic_match(const char *filter, const char *topic) {
    if (!filter || !topic) return false;

    while (*filter && *topic) {
        if (*filter == '#') {
            return true; /* '#' は末尾のすべての階層にマッチ */
        }

        if (*filter == '+') {
            /* '+' は次の '/' までスキップ */
            while (*topic && *topic != '/') {
                topic++;
            }
            filter++;
            continue;
        }

        if (*filter != *topic) {
            return false;
        }

        filter++;
        topic++;
    }

    if (*filter == '#') {
        return true;
    }

    if (*filter == '+' && *topic == '\0') {
        filter++;
    }

    return (*filter == '\0' && *topic == '\0');
}
