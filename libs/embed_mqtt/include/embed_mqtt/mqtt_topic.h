#pragma once
#ifndef MQTT_TOPIC_H
#define MQTT_TOPIC_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTTトピックフィルタとトピック名がマッチするかを判定します。
 * 
 * '+' (単一レベルワイルドカード) および '#' (複数レベルワイルドカード) をサポートします。
 * 
 * @param filter 購読トピックフィルタ (例: "sensor/+/temp", "device/#")
 * @param topic 実際のトピック名 (例: "sensor/room1/temp", "device/living/light")
 * @return true マッチした, false 不一致
 */
bool mqtt_topic_match(const char *filter, const char *topic);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TOPIC_H */
