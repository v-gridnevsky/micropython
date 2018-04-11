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
static mp_obj_t incoming_messages = mp_const_none;
// Tells if ESPNow was already initialized
static int initialized = 0;

STATIC void IRAM_ATTR recv_cb(const uint8_t *macaddr, const uint8_t *data, int len)
{
    mp_obj_tuple_t *msg = mp_obj_new_tuple(2, NULL);
    msg->items[0] = mp_obj_new_bytes(macaddr, ESP_NOW_ETH_ALEN);
    msg->items[1] = mp_obj_new_bytes(data, len);
    mp_obj_list_append(incoming_messages, msg);
}

STATIC mp_obj_t espnow_init() {
    if (!initialized) {
        esp_now_init();
        initialized = 1;
      	esp_now_register_recv_cb(recv_cb);
        incoming_messages = mp_obj_new_list(0, NULL);
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

STATIC mp_obj_t espnow_set_pmk(mp_obj_t pmk) {
    uint8_t buf[ESP_NOW_KEY_LEN];
    _get_bytes(pmk, ESP_NOW_KEY_LEN, buf);
    esp_espnow_exceptions(esp_now_set_pmk(buf));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(espnow_set_pmk_obj, espnow_set_pmk);

STATIC mp_obj_t espnow_add_peer(size_t n_args, const mp_obj_t *args) {
    esp_now_peer_info_t peer = {0};
    // leaving channel as 0 for autodetect
    peer.ifidx = ((wlan_if_obj_t *)MP_OBJ_TO_PTR(args[0]))->if_id;
    _get_bytes(args[1], ESP_NOW_ETH_ALEN, peer.peer_addr);
    if (n_args > 2) {
        _get_bytes(args[2], ESP_NOW_KEY_LEN, peer.lmk);
        peer.encrypt = 1;
    }
    esp_espnow_exceptions(esp_now_add_peer(&peer));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_add_peer_obj, 2, 3, espnow_add_peer);

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

STATIC void pop_list_indexes(mp_obj_list_t* messages, mp_uint_t* deleted_indexes, mp_uint_t deleted_indexes_pos) {
    // Delete copied messages in a reverse order
    mp_obj_t args[] = {messages, mp_const_none};
    // Iterate deleted index list in a reverse order;
    // pop those items which have to be deleted
    while (true) {
        // Break if there are no items
        if (messages->len == 0) break;
        // Pop an item
        args[1] = MP_OBJ_NEW_SMALL_INT(deleted_indexes[deleted_indexes_pos]);
        list_pop(2, args);
        // break on 0'th element
        if (deleted_indexes_pos==0) break;
        deleted_indexes_pos--;
    }
}

STATIC mp_obj_t espnow_extract_list_by_mac(mp_obj_t macaddr) {
    // List of separate packets from MAC to return
    mp_obj_t packet_list = mp_obj_new_list(0, NULL);
    // Message object to interact with and its length
    mp_obj_list_t *messages = MP_OBJ_TO_PTR(incoming_messages);
    const mp_int_t len = messages->len;
    // Count an amount of the deleted indexes
    mp_uint_t deleted_count = 0;
    mp_uint_t deleted_indexes_pos = 0;
    // Empty array should be ignored
    if (len > 0) {
        // Count deleted items
        for (mp_int_t i = 0; i < len; i++) {
            mp_obj_tuple_t *msg = MP_OBJ_TO_PTR(messages->items[i]);
            if (mp_obj_equal(macaddr, msg->items[0])) {
                deleted_count++;
            }
        }
        // Define an array of deleted indexes
        mp_uint_t* deleted_indexes = malloc(deleted_count * sizeof(mp_uint_t));
        // Build a packet list
        for (mp_uint_t msg_id = 0; msg_id < len; msg_id++) {
             mp_obj_tuple_t *msg = MP_OBJ_TO_PTR(messages->items[msg_id]);
             if (mp_obj_equal(macaddr, msg->items[0])) {
                  mp_obj_list_append(packet_list, msg->items[1]);
                  // Add index to deleted
                  deleted_indexes[deleted_indexes_pos] = msg_id;
                  deleted_indexes_pos++;
             }
        }
        if (deleted_indexes_pos>0) deleted_indexes_pos--;
        // Pop redundant items
        if (deleted_count > 0) {
            pop_list_indexes(messages, deleted_indexes, deleted_indexes_pos);
        }
        free(deleted_indexes);
    }
    return packet_list;
}
MP_DEFINE_CONST_FUN_OBJ_1(espnow_extract_list_by_mac_obj, espnow_extract_list_by_mac);

STATIC mp_obj_t free_list(mp_obj_t list) {
  mp_obj_list_t *list_ptr = MP_OBJ_TO_PTR(list);
  m_del_obj(mp_obj_list_t, list_ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(free_list_obj, free_list);

STATIC mp_obj_t espnow_extract_mac_list() {
    mp_obj_list_t *messages = MP_OBJ_TO_PTR(incoming_messages);
    const mp_int_t len = messages->len;
    // List to store our MACs
    mp_obj_t mac_list_obj = mp_obj_new_list(0, NULL);
    //
    for (mp_uint_t msg_id = 0; msg_id < len; msg_id++) {
        mp_obj_tuple_t *msg = MP_OBJ_TO_PTR(messages->items[msg_id]);
        mp_obj_t mac_count_obj = list_count(mac_list_obj, msg->items[0]);
        mp_uint_t mac_count = (mp_uint_t)mp_obj_get_int(mac_count_obj);
        if (mac_count == 0) {
            mp_obj_list_append(mac_list_obj, msg->items[0]);
        }
    }
    //
    return mac_list_obj;
}
MP_DEFINE_CONST_FUN_OBJ_0(espnow_extract_mac_list_obj, espnow_extract_mac_list);

STATIC mp_obj_t espnow_data_available() {
    mp_obj_list_t *messages = MP_OBJ_TO_PTR(incoming_messages);
    mp_int_t len = messages->len;
    return mp_obj_new_bool(len > 0);
}
MP_DEFINE_CONST_FUN_OBJ_0(espnow_data_available_obj, espnow_data_available);

STATIC const mp_rom_map_elem_t espnow_globals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&espnow_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&espnow_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pmk), MP_ROM_PTR(&espnow_set_pmk_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_peer), MP_ROM_PTR(&espnow_add_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_exists), MP_ROM_PTR(&peer_exists_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&espnow_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_send_all), MP_ROM_PTR(&espnow_send_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_data_available), MP_ROM_PTR(&espnow_data_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_extract_list_by_mac), MP_ROM_PTR(&espnow_extract_list_by_mac_obj) },
    { MP_ROM_QSTR(MP_QSTR_extract_mac_list), MP_ROM_PTR(&espnow_extract_mac_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_free_list), MP_ROM_PTR(&free_list_obj) }
};
STATIC MP_DEFINE_CONST_DICT(espnow_globals_dict, espnow_globals_dict_table);

const mp_obj_module_t mp_module_esp_espnow = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&espnow_globals_dict,
};
