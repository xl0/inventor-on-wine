/* ucrtbase math functions bit-for-bit: for each function, evaluate a fixed
 * pseudo-random set of arguments (geometry-kernel ranges), print a hash of
 * the result bits and write all bits to crt_math.bin (cwd) for a per-value
 * comparison between Windows and Wine. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef double (__cdecl *f1)(double);
typedef double (__cdecl *f2)(double, double);

static unsigned long long rng = 88172645463325252ull;
static double rnd(double lo, double hi)
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return lo + (hi - lo) * (double)(rng >> 11) / 9007199254740992.0;
}

int main(void)
{
    static const struct { const char *name; int args; double lo, hi; } fns[] = {
        {"sin", 1, -10, 10}, {"cos", 1, -10, 10}, {"tan", 1, -1.5, 1.5}, {"asin", 1, -1, 1},
        {"acos", 1, -1, 1}, {"atan", 1, -100, 100}, {"atan2", 2, -10, 10}, {"exp", 1, -20, 20},
        {"log", 1, 1e-6, 1e6}, {"log10", 1, 1e-6, 1e6}, {"pow", 2, 0.001, 10}, {"sqrt", 1, 0, 1e6},
        {"cbrt", 1, -1e3, 1e3}, {"hypot", 2, -1e3, 1e3}, {"sinh", 1, -5, 5}, {"cosh", 1, -5, 5},
        {"tanh", 1, -5, 5}, {"fmod", 2, -100, 100}, {"exp2", 1, -20, 20}, {"log2", 1, 1e-6, 1e6},
    };
    HMODULE crt = LoadLibraryA("ucrtbase.dll");
    FILE *out = fopen("crt_math.bin", "wb");
    int i, j;

    for (i = 0; i < ARRAYSIZE(fns); i++)
    {
        void *f = GetProcAddress(crt, fns[i].name);
        unsigned long long h = 1469598103934665603ull, first[3];
        if (!f) { printf("%-6s missing\n", fns[i].name); continue; }
        rng = 88172645463325252ull + i;
        for (j = 0; j < 200000; j++)
        {
            double a = rnd(fns[i].lo, fns[i].hi), b = rnd(fns[i].lo, fns[i].hi), r;
            unsigned long long bits;
            r = fns[i].args == 1 ? ((f1)f)(a) : ((f2)f)(a, fns[i].args == 2 && !strcmp(fns[i].name, "pow") ? b / 3 : b);
            memcpy(&bits, &r, 8);
            if (j < 3) first[j] = bits;
            fwrite(&bits, 8, 1, out);
            h = (h ^ bits) * 1099511628211ull;
        }
        printf("%-6s %016llx  %016llx %016llx %016llx\n", fns[i].name, h, first[0], first[1], first[2]);
    }
    fclose(out);
    return 0;
}
