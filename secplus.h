/*
 * Copyright 2022 Clayton Smith (argilo@gmail.com)
 *
 * This file is part of secplus.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef SECPLUS_H
#define SECPLUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern int8_t encode_v1(uint32_t rolling, uint32_t fixed, uint8_t *symbols1,
                        uint8_t *symbols2);

extern int8_t decode_v1(const uint8_t *symbols1, const uint8_t *symbols2,
                        uint32_t *rolling, uint32_t *fixed);

extern int8_t encode_v2(uint32_t rolling, uint64_t fixed, uint32_t data,
                        uint8_t frame_type, uint8_t *packet1, uint8_t *packet2);

extern int8_t decode_v2(uint8_t frame_type, const uint8_t *packet1,
                        const uint8_t *packet2, uint32_t *rolling,
                        uint64_t *fixed, uint32_t *data);

extern int8_t encode_wireline(uint32_t rolling, uint64_t fixed, uint32_t data,
                              uint8_t *packet);

extern int8_t decode_wireline(const uint8_t *packet, uint32_t *rolling,
                              uint64_t *fixed, uint32_t *data);

extern int8_t encode_wireline_command(uint32_t rolling, uint64_t device_id,
                                      uint16_t command, uint32_t payload,
                                      uint8_t *packet);

extern int8_t decode_wireline_command(const uint8_t *packet, uint32_t *rolling,
                                      uint64_t *device_id, uint16_t *command,
                                      uint32_t *payload);
typedef struct {
  uint8_t door;
  uint8_t learn;
  uint8_t unk1;
  uint8_t unk2;
  uint8_t light;
  uint8_t lock;
  uint8_t blocked;
  uint8_t unk3;
} secplus_status_t;

extern int8_t decode_wireless_command(const uint8_t *packet1,
                                      const uint8_t *packet2, uint32_t *rolling,
                                      uint64_t *device_id, uint8_t *button,
                                      uint8_t *switch_number, uint16_t *command,
                                      uint32_t *payload);

extern uint8_t secplus_wireless_pressed(uint32_t payload);
extern void secplus_wireless_parse_status(uint32_t payload,
                                          secplus_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
