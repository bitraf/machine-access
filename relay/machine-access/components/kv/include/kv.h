#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int (kv_callback)(void *ctx, const char *key, const char *value);

struct kv_parser {
    void *ctx;
    char *kbuf;
    int ksz;
    char *vbuf;
    int vsz;

    int has_key;
    char *end;

    kv_callback *callback;
};

int kv_parser_init(struct kv_parser *parser, kv_callback *callback, void *ctx, char *kbuf, int ksz, char *vbuf, int vsz);

int kv_parser_add(struct kv_parser *parser, char *buf, int sz);
int kv_parser_end(struct kv_parser *parser);

int kv_write_str(char *buffer, int sz, const char* key, const char* value);
int kv_write_int(char *buffer, int sz, const char* key, int value);

#ifdef __cplusplus
}
#endif
