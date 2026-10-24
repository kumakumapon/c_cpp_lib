#include "embed_mqtt/mqtt_topic.h"
#include <string.h>

bool mqtt_topic_match(const char *filter, const char *topic) {
    if (!filter || !topic || *filter == '\0' || *topic == '\0') return false;

    /* MQTT 3.1.1 4.7.2: '$' で始まるトピックは、先頭が '+' / '#' のフィルタにはマッチしない */
    if (*topic == '$' && (*filter == '+' || *filter == '#')) return false;

    for (;;) {
        /* '#' は最後のレベルでのみ有効。その位置以降(親レベルを含む)すべてにマッチする */
        if (*filter == '#') return filter[1] == '\0';

        if (*filter == '+') {
            /* '+' はレベル全体を占めなければならない。トピック側の1レベル(空も可)を読み飛ばす */
            if (filter[1] != '\0' && filter[1] != '/') return false;
            while (*topic && *topic != '/') topic++;
            filter++;
        } else {
            /* 通常のレベルは完全一致 */
            while (*filter && *filter != '/') {
                if (*filter != *topic) return false;
                filter++;
                topic++;
            }
            if (*topic && *topic != '/') return false;
        }

        /* ここで filter / topic はレベル境界('/' または終端)にある */
        if (*filter == '\0') return *topic == '\0';
        filter++; /* '/' */
        if (*topic == '\0') {
            /* トピックが先に終わった: "a/#" のように残りが '#' だけなら親レベルにマッチする */
            return filter[0] == '#' && filter[1] == '\0';
        }
        topic++; /* '/' */
    }
}
