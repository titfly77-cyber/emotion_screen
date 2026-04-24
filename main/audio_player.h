#pragma once

#include <stdbool.h>

/**
 * @brief 初始化 I2S 音频外设
 */
void audio_player_init(void);

/**
 * @brief 触发播放指定音频文件（自动创建后台任务）
 * @param filename 音频文件路径 (例如 "/audio.wav")
 */
void audio_player_play(const char* filename);

/**
 * @brief 获取当前是否正在播放
 */
bool audio_player_is_playing(void);