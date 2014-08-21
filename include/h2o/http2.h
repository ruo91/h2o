#ifndef h2o__http2_h
#define h2o__http2_h

#include "khash.h"

typedef struct st_h2o_http2_conn_t h2o_http2_conn_t;

#define H2O_HTTP2_DECODE_INCOMPLETE -2
#define H2O_HTTP2_DECODE_ERROR -1

/* hpack */

#define H2O_HTTP2_ENCODE_INT_MAX_LENGTH 5

typedef struct st_h2o_hpack_header_table_t {
    /* ring buffer */
    struct st_h2o_hpack_header_table_entry_t *entries;
    size_t num_entries, entry_capacity, entry_start_index;
    /* size and capacity are 32+name_len+value_len (as defined by hpack spec.) */
    size_t hpack_size;
    size_t hpack_capacity;
} h2o_hpack_header_table_t;

void h2o_hpack_dispose_header_table(h2o_mempool_t *pool, h2o_hpack_header_table_t *header_table);
int h2o_hpack_parse_headers(h2o_req_t *req, h2o_hpack_header_table_t *header_table, int *allow_psuedo, const uint8_t *src, size_t len);
size_t h2o_hpack_encode_string(uint8_t *dst, const char *s, size_t len);
uv_buf_t h2o_hpack_flatten_headers(h2o_mempool_t *pool, uint32_t stream_id, size_t max_frame_size, h2o_res_t *res);

/* settings */

#define H2O_HTTP2_SETTINGS_HEADER_TABLE_SIZE 1
#define H2O_HTTP2_SETTINGS_ENABLE_PUSH 2
#define H2O_HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS 3
#define H2O_HTTP2_SETTINGS_INITIAL_WINDOW_SIZE 4
#define H2O_HTTP2_SETTINGS_MAX_FRAME_SIZE 5
#define H2O_HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE 6

typedef struct st_h2o_http2_settings_t {
    uint32_t header_table_size;
    uint32_t enable_push;
    uint32_t max_concurrent_streams;
    uint32_t initial_window_size;
    uint32_t max_frame_size;
} h2o_http2_settings_t;

const h2o_http2_settings_t H2O_HTTP2_SETTINGS_DEFAULT;

/* frames */

#define H2O_HTTP2_FRAME_HEADER_SIZE 9

#define H2O_HTTP2_FRAME_TYPE_DATA 0
#define H2O_HTTP2_FRAME_TYPE_HEADERS 1
#define H2O_HTTP2_FRAME_TYPE_PRIORITY 2
#define H2O_HTTP2_FRAME_TYPE_RST_STREAM 3
#define H2O_HTTP2_FRAME_TYPE_SETTINGS 4
#define H2O_HTTP2_FRAME_TYPE_PUSH_PROMISE 5
#define H2O_HTTP2_FRAME_TYPE_PING 6
#define H2O_HTTP2_FRAME_TYPE_GOAWAY 7
#define H2O_HTTP2_FRAME_TYPE_WINDOW_UPDATE 8
#define H2O_HTTP2_FRAME_TYPE_CONTINUATION 9

#define H2O_HTTP2_FRAME_FLAG_END_STREAM 0x1
#define H2O_HTTP2_FRAME_FLAG_ACK 0x1
#define H2O_HTTP2_FRAME_FLAG_END_HEADERS 0x4
#define H2O_HTTP2_FRAME_FLAG_PADDED 0x8
#define H2O_HTTP2_FRAME_FLAG_PRIORITY 0x20

typedef struct st_h2o_http2_frame_t {
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
    const uint8_t *payload;
} h2o_http2_frame_t;

typedef struct st_h2o_http2_headers_payload_t {
    int exclusive;
    uint32_t stream_dependency; /* 0 if not set */
    uint16_t weight; /* 0 if not set */
    const uint8_t *headers;
    size_t headers_len;
} h2o_http2_headers_payload_t;

typedef void (*h2o_http2_close_cb)(h2o_http2_conn_t *conn);

typedef enum enum_h2o_http2_stream_state_t {
    H2O_HTTP2_STREAM_STATE_RECV_PSUEDO_HEADERS,
    H2O_HTTP2_STREAM_STATE_RECV_HEADERS,
    H2O_HTTP2_STREAM_STATE_SEND_HEADERS,
    H2O_HTTP2_STREAM_STATE_SEND_BODY
} h2o_http2_stream_state_t;

typedef struct st_h2o_http2_stream_t {
    uint32_t stream_id;
    h2o_req_t req;
    h2o_ostream_t _ostr_final;
    h2o_http2_stream_state_t _state;
} h2o_http2_stream_t;

KHASH_MAP_INIT_INT64(h2o_http2_stream_t, h2o_http2_stream_t*)

struct st_h2o_http2_conn_t {
    uv_stream_t *stream;
    h2o_loop_context_t *ctx;
    /* callbacks that should be set by the user */
    h2o_req_cb req_cb;
    h2o_http2_close_cb close_cb;
    /* settings */
    h2o_http2_settings_t peer_settings;
    /* streams */
    khash_t(h2o_http2_stream_t) *active_streams;
    uint32_t max_stream_id;
    /* internal */
    ssize_t (*_read_expect)(h2o_http2_conn_t *conn, const uint8_t *src, size_t len);
    h2o_input_buffer_t *_input;
    h2o_input_buffer_t *_http1_req_input; /* contains data referred to by original request via HTTP/1.1 */
    h2o_hpack_header_table_t _input_header_table;
    struct {
        h2o_mempool_t pool;
        uv_write_t wreq;
        H2O_VECTOR(uv_buf_t) bufs;
        H2O_VECTOR(h2o_http2_stream_t*) closing_streams;
        h2o_timeout_entry_t timeout_entry;
    } _write;
};

int h2o_http2_update_peer_settings(h2o_http2_settings_t *settings, const uint8_t *src, size_t len);

/* frames */

void h2o_http2_encode_frame_header(uint8_t *dst, size_t length, uint8_t type, uint8_t flags, int32_t stream_id);
ssize_t h2o_http2_decode_frame(h2o_http2_frame_t *frame, const uint8_t *src, size_t len, const h2o_http2_settings_t *host_settings);
int h2o_http2_decode_headers_payload(h2o_http2_headers_payload_t *payload, const h2o_http2_frame_t *frame);

/* core */
void h2o_http2_close_and_free(h2o_http2_conn_t *conn);
int h2o_http2_handle_upgrade(h2o_req_t *req, h2o_http2_conn_t *conn);

#endif