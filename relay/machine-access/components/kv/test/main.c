#include "kv.h"

#include <stdio.h>
#include <stdlib.h>

int kv_found(void*ctx, const char *key, const char *value)
{
    printf("%s: '%s'='%s'\n", __FUNCTION__, key, value);

    return 0;
}

int main() {
    struct kv_parser p;
    char kbuf[10];
    char vbuf[100];
    kv_parser_init(&p, &kv_found, NULL, kbuf, sizeof(kbuf), vbuf, sizeof(vbuf));

    int ret;
    while (1) {
        char c = getc(stdin);
        if (c == EOF) {
            // printf("%s: EOF\n", __FUNCTION__);
            break;
        }
        ret = kv_parser_add(&p, &c, 1);
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
