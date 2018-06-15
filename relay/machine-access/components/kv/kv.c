#include "kv.h"

#include <stdio.h>
#include <string.h>

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

int kv_parser_add(struct kv_parser *parser, char *buf, int sz)
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

    int left = &dst[dstsz] - parser->end;
    int end = left < sz ? left : sz;
    printf("left=%d, end=%d\n", left, end);

    int i, found = 0;
    for (i = 0; i < end; i++) {
        char c = buf[i];
        printf("%s: c=%c\n", __FUNCTION__, c);
        if (c == target) {
            *parser->end = 0;
            found = 1;
        } else {
            *parser->end++ = c;
        }
    }

    if (found) {
        if (!parser->has_key) {
            printf("%s: found key: %s\n", __FUNCTION__, parser->kbuf);
            parser->has_key = 1;
            parser->end = parser->vbuf;
        } else {
            printf("%s: found value: %s\n", __FUNCTION__, parser->vbuf);
            if (parser->callback) {
                parser->callback(parser->ctx, parser->kbuf, parser->vbuf);
            }
            parser->has_key = 0;
            parser->end = parser->kbuf;
        }
    } else {
        if (parser->end == parser->kbuf) {
            printf("%s: Key too long\n", __FUNCTION__);
            return 1;
        }
    }

    return 0;
}

int kv_parser_end(struct kv_parser *parser)
{
    // Check that we haven't started reading anything else
    return parser->end != parser->kbuf;
}

int kv_write_str(char *buffer, int sz, const char *key, const char *value)
{
    return snprintf(buffer, sz, "%s=%s", key, value) >= sz;
}

int kv_write_int(char *buffer, int sz, const char *key, int value)
{
    return snprintf(buffer, sz, "%s=%d", key, value) >= sz;
}

/*
int kv_parse(void *ctx, kv_read *read, kv_set_pos *set_pos, kv_callback *callback, char *const buf, int sz)
{
    int ret = 0;
    int pos = 0;

    int fd = open("config", O_RDONLY, 0);
    // printf("fd=%d\n", fd);
    if (fd < 0) {
        ret = -1;
        goto fail;
    }

    while (1) {
        ret = read(ctx, buf, sz);
        // printf("read: pos=%d, ret=%d\n", (int) pos, ret);

        if (ret == 0) {
            break;
        } else if (ret > 0) {
            char *end = (char *)memchr(buf, 0, ret);
            if (end == NULL) {
                printf("could not find newline\n");
                return -1;
            } else {
                *end = '\0';
                size_t line_sz = end - buf;
                // printf("line_sz=%d\n", line_sz);
                pos += line_sz + 1;

                // printf("Handling line: #%d, %s\n", sz, line);
                char *ptr = strchr(buf, '=');
                if (ptr == NULL) {
                    printf("Could not find '='\n");
                    return -1;
                }

                *ptr = '\0';
                const char *value = ++ptr;

                ret = callback(ctx, buf, value);
                if (ret) {
                    printf("callback failed: %d\n", ret);
                    return ret;
                }
            }

            ret = set_pos(ctx, pos);
            if (ret == -1) {
                printf("lseek failed: pos=%d, res=%d\n", pos, ret);
                return ret;
            }
        } else {
            printf("read() failed");
            return -1;
        }
    }

    return ret;
}
*/

/*
static int config_write_kv(int fd, const char *key, const char *value)
{
    int ret;
    char c;
    // printf("Writing %s=%s\n", key, value);

    size_t sz = strlen(key);
    ret = write(fd, (char *)key, sz);
    if (ret != sz) {
        return ret;
    }

    c = '=';
    ret = write(fd, &c, 1);
    if (ret != 1) {
        return ret;
    }

    sz = strlen(value);
    ret = write(fd, (char *)value, sz);
    if (ret != sz) {
        return ret;
    }

    c = 0;
    return write(fd, &c, 1) != 1;
}

static int config_write_kv(int fd, const char *key, int value)
{
    char buf[100];
    itoa(value, buf, 10);

    return config_write_kv(fd, key, buf);
}

static int config_write(struct config &config)
{
    int ret = 0;

    int fd = open("config", O_WRONLY | O_CREAT, 0);

    if (fd == -1) {
        ret = -1;
        goto fail;
    }

    ret = config_write_kv(fd, "mqtt-host", config.mqtt_host);
    if (ret) {
        goto fail;
    }

    ret = config_write_kv(fd, "mqtt-port", config.mqtt_port);
    if (ret) {
        goto fail;
    }

fail:
    if (fd != -1) {
        close(fd);
    }

    // printf("%s: ret=%d\n", __FUNCTION__, ret);
    return ret;
}
*/
