/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Nick Moore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"

#include "py/mpconfig.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/nlr.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/objlist.h"

#include "modnetwork.h"

#include "lib/dl_list.h"

// TODO do something about that
typedef struct _mp_obj_bool_t {
    mp_obj_base_t base;
    bool value;
} mp_obj_bool_t;

typedef struct _wlan_if_obj_t {
    mp_obj_base_t base;
    int if_id;
} wlan_if_obj_t;

const mp_obj_type_t wlan_if_type;
STATIC const wlan_if_obj_t wlan_sta_obj = {{&wlan_if_type}, WIFI_IF_STA};
STATIC const wlan_if_obj_t wlan_ap_obj = {{&wlan_if_type}, WIFI_IF_AP};

// ESPNow-related nodes
struct ESPNode {
    struct MNode node;
    uint8_t macaddr[ESP_NOW_ETH_ALEN];
  	uint16_t len;
  	uint8_t data[ESP_NOW_MAX_DATA_LEN];
};
typedef struct ESPNode *esnd;

NORETURN void _esp_espnow_exceptions(esp_err_t e) {
   switch (e) {
      case ESP_ERR_ESPNOW_NOT_INIT:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Not Initialized");
      case ESP_ERR_ESPNOW_ARG:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Invalid Argument");
      case ESP_ERR_ESPNOW_NO_MEM:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Out Of Mem");
      case ESP_ERR_ESPNOW_FULL:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Peer List Full");
      case ESP_ERR_ESPNOW_NOT_FOUND:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Peer Not Found");
      case ESP_ERR_ESPNOW_INTERNAL:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Internal");
      case ESP_ERR_ESPNOW_EXIST:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Peer Exists");
      default:
        nlr_raise(mp_obj_new_exception_msg_varg(
          &mp_type_RuntimeError, "ESP-Now Unknown Error 0x%04x", e
        ));
   }
}

static inline void esp_espnow_exceptions(esp_err_t e) {
    if (e != ESP_OK) _esp_espnow_exceptions(e);
}

static inline void _get_bytes(mp_obj_t str, size_t len, uint8_t *dst) {
    size_t str_len;
    const char *data = mp_obj_str_get_data(str, &str_len);
    if (str_len != len) mp_raise_ValueError("bad len");
    memcpy(dst, data, len);
}

// Contains tuples with MAC and the message
static DL_LIST incoming_messages;
// Tells if ESPNow was already initialized
static int initialized = 0;

STATIC void IRAM_ATTR recv_cb(const uint8_t *macaddr, const uint8_t *data, int len)
{
    struct ESPNode * m;
    m = malloc(sizeof(struct ESPNode));
    m->len = len;
    memcpy(m->macaddr, macaddr, ESP_NOW_ETH_ALEN);
    memcpy(m->data, data, len);
    addTail(incoming_messages, (NODE)m);
}

STATIC mp_obj_t espnow_init() {
    if (!initialized) {
        esp_now_init();
        initialized = 1;
      	esp_now_register_recv_cb(recv_cb);
        incoming_messages = newList();
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(espnow_init_obj, espnow_init);

STATIC mp_obj_t espnow_deinit() {
    if (initialized) {
        esp_now_deinit();
        initialized = 0;
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(espnow_deinit_obj, espnow_deinit);

// Sets Primary Master Key. Read more:
// https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/wifi/esp_now.rst
STATIC mp_obj_t espnow_set_pmk(mp_obj_t pmk) {
    uint8_t buf[ESP_NOW_KEY_LEN];
    _get_bytes(pmk, ESP_NOW_KEY_LEN, buf);
    esp_espnow_exceptions(esp_now_set_pmk(buf));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(espnow_set_pmk_obj, espnow_set_pmk);

STATIC mp_obj_t espnow_add_peer(size_t n_args, const mp_obj_t *args) {
    esp_now_peer_info_t peer = {0};
    mp_obj_t channel_obj;
    uint8_t channel;
    // Leaving channel as 0 for autodetect
    peer.ifidx = ((wlan_if_obj_t *)MP_OBJ_TO_PTR(args[0]))->if_id;
    _get_bytes(args[1], ESP_NOW_ETH_ALEN, peer.peer_addr);
    // Adds a channel
    if (n_args > 2) {
        channel_obj = args[2];
        channel = (mp_uint_t)mp_obj_get_int(channel_obj);
        peer.channel = channel;
    }
    // Adds encryption key
    if (n_args > 3) {
        _get_bytes(args[3], ESP_NOW_KEY_LEN, peer.lmk);
        peer.encrypt = 1;
    }
    esp_espnow_exceptions(esp_now_add_peer(&peer));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_add_peer_obj, 2, 3, espnow_add_peer);

STATIC mp_obj_t espnow_mod_peer(size_t n_args, const mp_obj_t *args) {
    esp_now_peer_info_t peer = {0};
    mp_obj_t channel_obj;
    uint8_t channel;
    // Leaving channel as 0 for autodetect
    peer.ifidx = ((wlan_if_obj_t *)MP_OBJ_TO_PTR(args[0]))->if_id;
    _get_bytes(args[1], ESP_NOW_ETH_ALEN, peer.peer_addr);
    // Adds a channel
    if (n_args > 2) {
        channel_obj = args[2];
        channel = (mp_uint_t)mp_obj_get_int(channel_obj);
        peer.channel = channel;
    }
    // Adds encryption key
    if (n_args > 3) {
        _get_bytes(args[3], ESP_NOW_KEY_LEN, peer.lmk);
        peer.encrypt = 1;
    }
    esp_espnow_exceptions(esp_now_mod_peer(&peer));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_mod_peer_obj, 2, 3, espnow_mod_peer);

STATIC mp_obj_t espnow_del_peer(mp_obj_t mac)
{
    mp_obj_t exists;
    mp_obj_str_t *mac_obj = MP_OBJ_TO_PTR(mac);
    const uint8_t *macaddr = mac_obj->data;
    exists = (esp_now_is_peer_exist(macaddr)) ? mp_const_true : mp_const_false;
    esp_now_del_peer(macaddr);
    return exists;
}
MP_DEFINE_CONST_FUN_OBJ_1(espnow_del_peer_obj, espnow_del_peer);

STATIC mp_obj_t peer_exists(mp_obj_t mac)
{
    mp_obj_str_t *mac_obj = MP_OBJ_TO_PTR(mac);
    const uint8_t *macaddr = mac_obj->data;
    if (esp_now_is_peer_exist(macaddr)) {
        return mp_const_true;
    } else {
        return mp_const_false;
    }
}
MP_DEFINE_CONST_FUN_OBJ_1(peer_exists_obj, peer_exists);

STATIC mp_obj_t espnow_send(mp_obj_t addr, mp_obj_t msg) {
    mp_uint_t len1;
    const uint8_t *buf1 = (const uint8_t *)mp_obj_str_get_data(addr, &len1);
    mp_uint_t len2;
    const uint8_t *buf2 = (const uint8_t *)mp_obj_str_get_data(msg, &len2);
    if (len1 != ESP_NOW_ETH_ALEN) mp_raise_ValueError("addr invalid");
    if (len2 > ESP_NOW_MAX_DATA_LEN) mp_raise_ValueError("Msg too long");
    esp_espnow_exceptions(esp_now_send(buf1, buf2, len2));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(espnow_send_obj, espnow_send);

STATIC mp_obj_t espnow_send_all(mp_obj_t msg) {
    mp_uint_t len;
    const uint8_t *buf = (const uint8_t *)mp_obj_str_get_data(msg, &len);
    if (len > ESP_NOW_MAX_DATA_LEN) mp_raise_ValueError("Msg too long");
    esp_espnow_exceptions(esp_now_send(NULL, buf, len));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(espnow_send_all_obj, espnow_send_all);

STATIC mp_obj_t get_packet_mac_bytes(NODE list_item) {
    esnd enode = (esnd)list_item;
    byte * p = m_new(byte, ESP_NOW_ETH_ALEN);
    mp_obj_t current_mac_addr;
    // Make a buffer, store data, make Python bytes instance,
    // append to the output
    memcpy(p, enode->macaddr, ESP_NOW_ETH_ALEN);
    current_mac_addr = mp_obj_new_bytes(p, ESP_NOW_ETH_ALEN);
    return current_mac_addr;
}

STATIC mp_obj_t get_packet_data_bytes(NODE list_item) {
    esnd enode = (esnd)list_item;
    uint16_t len = enode->len;
    byte * p = m_new(byte, len);
    // Make a buffer, store data, make Python bytes instance,
    // append to the output
    memcpy(p, enode->data, len * sizeof(byte));
    return mp_obj_new_bytes(p, len);
}

STATIC mp_obj_t espnow_extract_list_by_mac(mp_obj_t macaddr) {
    // List of separate packets from MAC to return
    mp_obj_t current_mac_addr;
    mp_obj_t packet_list = mp_obj_new_list(0, NULL);
    mp_obj_t packet_data;
    NODE list_item;
    NODE pushed;
    if (incoming_messages != NULL) {
        list_item = getHead(incoming_messages);
        for(;;) {
            if (list_item != 0) {
                current_mac_addr = get_packet_mac_bytes(list_item);
                if (mp_obj_equal(macaddr, current_mac_addr)) {
                    pushed = removeNode(incoming_messages, list_item);
                    packet_data = get_packet_data_bytes(pushed);
                    list_item = pushed->succ;
                    free(pushed);
                    mp_obj_list_append(packet_list, packet_data);
                } else {
                    list_item = list_item->succ;
                }
            }
            else {
                // Stop iteration on the end of a list
                break;
            }
        }
    }
    return packet_list;
}
MP_DEFINE_CONST_FUN_OBJ_1(
    espnow_extract_list_by_mac_obj,
    espnow_extract_list_by_mac
);

STATIC mp_obj_t espnow_extract_mac_list() {
    mp_obj_t mac;
    // List to store our MACs
    mp_obj_t mac_list_obj = mp_obj_new_list(0, NULL);
    mp_obj_t mac_count_obj;
    mp_uint_t mac_count;
    NODE list_item;

    if (incoming_messages != NULL && !dlListIsEmpty(incoming_messages)) {
        list_item = getHead(incoming_messages);
        for(;;) {
            if (list_item != 0 && list_item->succ != 0) {
                mac = get_packet_mac_bytes(list_item);
                // Count to check if MAC is already in list
                mac_count_obj = list_count(mac_list_obj, mac);
                mac_count = (mp_uint_t)mp_obj_get_int(mac_count_obj);
                // If there are no such MAC in list,
                // proceed with appending it
                if (mac_count == 0) {
                    mp_obj_list_append(mac_list_obj, mac);
                }
                // Process next item
                list_item = list_item->succ;
            }
            else {
                break;
            }
        }
    }
    return mac_list_obj;
}
MP_DEFINE_CONST_FUN_OBJ_0(espnow_extract_mac_list_obj, espnow_extract_mac_list);

STATIC mp_obj_t espnow_data_available() {
    return mp_obj_new_bool(
        !dlListIsEmpty(incoming_messages)
    );
}
MP_DEFINE_CONST_FUN_OBJ_0(espnow_data_available_obj, espnow_data_available);

STATIC const mp_rom_map_elem_t espnow_globals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&espnow_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&espnow_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pmk), MP_ROM_PTR(&espnow_set_pmk_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_peer), MP_ROM_PTR(&espnow_add_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_del_peer), MP_ROM_PTR(&espnow_del_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_mod_peer), MP_ROM_PTR(&espnow_mod_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_exists), MP_ROM_PTR(&peer_exists_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&espnow_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_send_all), MP_ROM_PTR(&espnow_send_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_data_available), MP_ROM_PTR(&espnow_data_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_extract_list_by_mac), MP_ROM_PTR(&espnow_extract_list_by_mac_obj) },
    { MP_ROM_QSTR(MP_QSTR_extract_mac_list), MP_ROM_PTR(&espnow_extract_mac_list_obj) }
};
STATIC MP_DEFINE_CONST_DICT(espnow_globals_dict, espnow_globals_dict_table);

const mp_obj_module_t mp_module_esp_espnow = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&espnow_globals_dict,
};
