#include "fwversion.h"

#include <ctype.h>
#include <stdlib.h>

// Reads up to three numbers and tells whether a pre-release suffix follows.
static void parse(const char *s, long out[3], int *pre)
{
    out[0] = out[1] = out[2] = 0;
    *pre = 0;
    if (!s) return;
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3 && isdigit((unsigned char)*s); i++) {
        char *end;
        out[i] = strtol(s, &end, 10);
        s = end;
        if (*s == '.') s++;
        else break;
    }
    *pre = *s == '-' || isalpha((unsigned char)*s);
}

int fwversion_compare(const char *a, const char *b)
{
    long va[3], vb[3];
    int pa, pb;
    parse(a, va, &pa);
    parse(b, vb, &pb);
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    }
    if (pa != pb) return pa ? -1 : 1; // 0.5-rc1 < 0.5
    return 0;
}
