#include "kv.h"

#include <stdio.h>
#include <string.h>

#define printf(...)

int kv_parser_init(struct kv_parser *parser, kv_callback *callback, void *ctx, char *kbuf, int ksz, char *vbuf, int vsz)
{
    parser->callback = callback;
    parser->ctx = ctx;
    parser->kbuf = kbuf;
    parser->ksz = ksz;
    parser->vbuf = vbuf;
    parser->vsz = vsz;

    parser->has_key = 0;
    parser->end = kbuf;

    return 0;
}

int kv_parser_add(struct kv_parser *parser, char *buf, const int sz)
{

    char *dst;
    int dstsz;
    char target;

    if (!parser->has_key) {
        target = '=';
        dst = parser->kbuf;
        dstsz = parser->ksz;
    } else {
        target = '\n';
        dst = parser->vbuf;
        dstsz = parser->vsz;
    }

    int dstspc = &dst[dstsz] - parser->end;
    // printf("%s: dstspc=%d, end=%d\n", __FUNCTION__, dstspc, end);

    int i;
    for (i = 0; i < sz; i++) {
        char c = buf[i];
        printf("%s: c=%c, dstspc=%d\n", __FUNCTION__, c, dstspc);

        if (c != target) {
            *parser->end++ = c;

            dstspc--;
            if (dstspc == 0) {
                if (parser->end == parser->kbuf) {
                    printf("%s: Key too long\n", __FUNCTION__);
                    return 1;
                } else {
                    printf("%s: Value too long\n", __FUNCTION__);
                    return 1;
                }
            }

            continue;
        }
        *parser->end = 0;

        if (!parser->has_key) {
            printf("%s: found key: %s\n", __FUNCTION__, parser->kbuf);

            parser->has_key = 1;
            parser->end = parser->vbuf;
            dstspc = parser->vsz;
            target = '\n';
        } else {
            printf("%s: found value: %s\n", __FUNCTION__, parser->vbuf);
            if (parser->callback) {
                parser->callback(parser->ctx, parser->kbuf, parser->vbuf);
            }

            parser->has_key = 0;
            parser->end = parser->kbuf;
            dstspc = parser->ksz;
            target = '=';
        }
    }

    return 0;
}

int kv_parser_end(struct kv_parser *parser)
{
    if (parser->has_key) {
        // If we're into the value, that's ok
        *parser->end = 0;
        if (parser->callback) {
            parser->callback(parser->ctx, parser->kbuf, parser->vbuf);
        }
        return 0;
    } else {
        // Check that we haven't started reading anything else
        return parser->end != parser->kbuf;
    }
}

int kv_write_str(char *buffer, int sz, const char *key, const char *value)
{
    int count = snprintf(buffer, sz, "%s=%s\n", key, value);
    return count < sz ? count : 0;
}

int kv_write_int(char *buffer, int sz, const char *key, int value)
{
    int count = snprintf(buffer, sz, "%s=%d\n", key, value);
    return count < sz ? count : 0;
}
