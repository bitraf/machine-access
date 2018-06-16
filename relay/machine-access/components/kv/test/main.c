#include "kv.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

int kv_found(void*ctx, const char *key, const char *value)
{
    printf("KV: '%s'='%s'\n", key, value);

    return 0;
}

int main(int argc, char **argv) {
    struct kv_parser p;
    char *input, *kbuf, *vbuf;
    int chunk_size = 10;
    int ksz = 10, vsz = 100;
    char c;

    while ((c = getopt (argc, argv, "c:k:v:")) != -1) {
        switch(c) {
            case 'c':
                chunk_size = atoi(optarg);
                break;
            case 'k':
                ksz = atoi(optarg);
                break;
            case 'v':
                vsz = atoi(optarg);
                break;
        }
    }

    printf("%s: key size=%d, value size=%d, chunk_size=%d\n", __FUNCTION__, ksz, vsz, chunk_size);

    input = malloc(chunk_size);
    assert(input != NULL);
    kbuf = malloc(ksz);
    assert(kbuf != NULL);
    vbuf = malloc(vsz);
    assert(vbuf != NULL);

    kv_parser_init(&p, &kv_found, NULL, kbuf, ksz, vbuf, vsz);

    int ret;
    while (1) {
        int count = read(STDIN_FILENO, input, chunk_size);
        if (count == 0) {
            // printf("%s: EOF\n", __FUNCTION__);
            break;
        } else if (count < 0) {
            goto fail;
        } else {
            printf("%s: read %d bytes\n", __FUNCTION__, count);
        }
        ret = kv_parser_add(&p, input, count);
        if (ret) {
            printf("%s: parser failed: %d\n", __FUNCTION__, ret);
            goto fail;
        }
    }

    if (kv_parser_end(&p)) {
        printf("%s: kv_parser_end failed\n", __FUNCTION__);
        goto fail;
    }

    return EXIT_SUCCESS;

fail:
    return EXIT_FAILURE;
}
