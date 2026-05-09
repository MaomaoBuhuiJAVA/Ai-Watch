#pragma once

#include <stdbool.h>
#include <stddef.h>

void app_audio_init(void);
bool app_audio_recording(void);
void app_audio_record_set(bool on);
void app_audio_record_toggle(void);

size_t app_audio_pcm_bytes(void);
