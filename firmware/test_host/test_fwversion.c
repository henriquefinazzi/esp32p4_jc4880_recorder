// fwversion_compare: numeric parts, optional "v", pre-release suffixes.
#include <stdio.h>

#include "fwversion.h"

static int failures;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c);   \
            failures++;                                             \
        }                                                           \
    } while (0)

int main(void)
{
    CHECK(fwversion_compare("0.4", "0.5") < 0);
    CHECK(fwversion_compare("v0.5", "0.5") == 0);
    CHECK(fwversion_compare("0.5", "0.5.0") == 0);
    CHECK(fwversion_compare("0.10", "0.9") > 0);
    CHECK(fwversion_compare("1.0", "0.99.9") > 0);
    CHECK(fwversion_compare("0.5.1", "v0.5") > 0);
    CHECK(fwversion_compare("0.5-rc1", "0.5") < 0);
    CHECK(fwversion_compare("0.5", "0.5-rc1") > 0);
    CHECK(fwversion_compare("0.6-rc1", "0.5") > 0);
    CHECK(fwversion_compare("", "0.1") < 0);
    CHECK(fwversion_compare(NULL, NULL) == 0);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("fwversion: ok\n");
    return 0;
}
