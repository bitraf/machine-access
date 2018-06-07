#include "main.h"

#include <stdio.h>

int app_init()
{
    printf("%s: \n", __FUNCTION__);

    return 0;
}

void app_on_lock()
{
    printf("%s: \n", __FUNCTION__);
}

void app_on_unlock()
{
    printf("%s: \n", __FUNCTION__);
}
