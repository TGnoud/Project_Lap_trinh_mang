#include "protocol/frame.h"

#include <arpa/inet.h>
#include <json-c/json.h>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool valid_size(size_t size) {
    return size != 0 && size <= FRAME_MAX_SIZE;
}

bool frame_encode_header(size_t body_size, unsigned char header[FRAME_HEADER_SIZE]) {
    if (!valid_size(body_size)) {
        return false;
    }
    // Đổi thứ tự byte như bài 3-Chap3/bt1_tuan3.c.
    const uint32_t network_size = htonl((uint32_t)body_size);
    memcpy(header, &network_size, FRAME_HEADER_SIZE);
    return true;
}

static size_t utf8_width(const unsigned char *bytes, size_t remaining) {
    const unsigned char lead = bytes[0];
    unsigned char low = 0x80;
    unsigned char high = 0xbf;
    size_t width;
    if (lead >= 0xc2 && lead <= 0xdf) {
        width = 2;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        width = 3;
        if (lead == 0xe0) low = 0xa0;  // Không chấp nhận mã hóa dài hơn cần thiết.
        if (lead == 0xed) high = 0x9f; // Loại trừ surrogate UTF-16.
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        width = 4;
        if (lead == 0xf0) low = 0x90;
        if (lead == 0xf4) high = 0x8f; // Tối đa U+10FFFF.
    } else {
        return 0;
    }
    if (remaining < width || bytes[1] < low || bytes[1] > high) {
        return 0;
    }
    for (size_t i = 2; i < width; ++i) {
        if (bytes[i] < 0x80 || bytes[i] > 0xbf) {
            return 0;
        }
    }
    return width;
}

// json-c strict vẫn có thể chấp nhận -01 hoặc 1. Kiểm định dạng số trước khi parse.
static size_t number_width(const char *start) {
    const char *end = start;
    if (*end == '-') {
        ++end;
    }
    if (!isdigit((unsigned char)*end) || (*end == '0' && isdigit((unsigned char)end[1]))) {
        return 0;
    }
    do {
        ++end;
    } while (isdigit((unsigned char)*end));
    if (*end == '.') {
        if (!isdigit((unsigned char)*++end)) {
            return 0;
        }
        while (isdigit((unsigned char)*end)) {
            ++end;
        }
    }
    if (*end == 'e' || *end == 'E') {
        ++end;
        if (*end == '+' || *end == '-') {
            ++end;
        }
        if (!isdigit((unsigned char)*end)) {
            return 0;
        }
        while (isdigit((unsigned char)*end)) {
            ++end;
        }
    }
    return (size_t)(end - start);
}

static bool valid_json_object(const char *body, size_t size) {
    char *ascii = NULL;
    bool quoted = false;
    for (size_t i = 0; i < size; ++i) {
        const unsigned char byte = (unsigned char)body[i];
        if (byte == '\0' || (!quoted && (byte == 'N' || byte == 'I'))) {
            // json-c còn chấp nhận NaN, Infinity và coi NUL là cuối input.
            free(ascii);
            return false;
        }
        if (byte >= 0x80) {
            const size_t width = utf8_width((const unsigned char *)body + i, size - i);
            if (!quoted || width == 0) {
                free(ascii);
                return false;
            }
            // json-c 0.18 strict có thể từ chối byte >= 0x80 (upstream #875).
            // Dùng bản sao ASCII để kiểm cú pháp; giữ nguyên các byte echo.
            if (ascii == NULL) {
                ascii = malloc(size + 1);
                if (ascii == NULL) {
                    return false;
                }
                memcpy(ascii, body, size + 1);
            }
            memset(ascii + i, '*', width);
            i += width - 1;
        } else if (quoted && byte == '\\' && i + 1 < size) {
            if (body[i + 1] == '\0' || (unsigned char)body[i + 1] >= 0x80) {
                free(ascii);
                return false;
            }
            ++i;
        } else if (byte == '"') {
            quoted = !quoted;
        } else if (!quoted && (byte == '-' || isdigit(byte))) {
            const size_t width = number_width(body + i);
            if (width == 0) {
                free(ascii);
                return false;
            }
            i += width - 1;
        }
    }

    struct json_tokener *tokener = json_tokener_new();
    if (tokener == NULL) {
        free(ascii);
        return false;
    }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    struct json_object *object = json_tokener_parse_ex(tokener, ascii == NULL ? body : ascii,
                                                      (int)size + 1);
    const bool valid = json_tokener_get_error(tokener) == json_tokener_success &&
                       json_tokener_get_parse_end(tokener) == size &&
                       json_object_is_type(object, json_type_object);
    json_object_put(object);
    json_tokener_free(tokener);
    free(ascii);
    return valid;
}

FrameStatus frame_feed(FrameReader *reader, const unsigned char *bytes, size_t size, size_t *used) {
    *used = 0;
    if (size == 0) {
        return FRAME_MORE;
    }

    // Giữ phần đã nhận qua nhiều lần đọc như bài 4-Chap4/chat_room_server.c.
    if (reader->header_used < FRAME_HEADER_SIZE) {
        const size_t missing = FRAME_HEADER_SIZE - reader->header_used;
        *used = size < missing ? size : missing;
        memcpy(reader->header + reader->header_used, bytes, *used);
        reader->header_used += *used;
        if (reader->header_used < FRAME_HEADER_SIZE) {
            return FRAME_MORE;
        }

        uint32_t network_size;
        memcpy(&network_size, reader->header, FRAME_HEADER_SIZE);
        reader->body_size = ntohl(network_size);
        if (!valid_size(reader->body_size)) {
            return FRAME_ERROR;
        }
        reader->body = malloc(reader->body_size + 1);
        if (reader->body == NULL) {
            return FRAME_ERROR;
        }
    }

    const size_t available = size - *used;
    const size_t missing = reader->body_size - reader->body_used;
    const size_t take = available < missing ? available : missing;
    memcpy(reader->body + reader->body_used, bytes + *used, take);
    reader->body_used += take;
    *used += take;
    if (reader->body_used < reader->body_size) {
        return FRAME_MORE;
    }

    reader->body[reader->body_size] = '\0';
    return valid_json_object(reader->body, reader->body_size) ? FRAME_READY : FRAME_ERROR;
}

bool frame_at_boundary(const FrameReader *reader) {
    return reader->header_used == 0;
}

void frame_reset(FrameReader *reader) {
    free(reader->body);
    *reader = (FrameReader){0};
}
