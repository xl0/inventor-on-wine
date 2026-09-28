# 055 ucrtbase math results differ from Windows in the last bits; rebuilt geometry differs slightly
Status: open (draft, informational, low priority) · Owner: - · Branch: - · Found in: samples campaign, integ 38e4c1c00ce (prefixes/inv3)

## Symptom
The samples scenario runs `Rebuild2` on Autodesk's 2022 sample parts (after migration to
2027). Some rebuilt volumes differ from the VM. The results are deterministic across 3 Wine
runs and 2 VM runs:

| part | opened | rebuilt, Wine | rebuilt, VM |
|---|---|---|---|
| Rim.ipt | 3670.798 cm3 | 3670.781 | 3670.798 (unchanged) |
| Hairdryer.ipt | 22.91994 | 22.92101 | 22.91994 (unchanged) |
| Speedometer.ipt | 7.019911 | 7.023572 | 7.023525 |

Relative size 5e-6 to 5e-5. Everything else in the set rebuilds to the VM's values
(69 documents: counts, BOM, mass).

## Windows ground truth (`tests/crt_math.c`, Win11 VM vs Wine)
The probe calls ucrtbase functions via GetProcAddress, like Inventor's imports.
Inventor uses Wine's builtin ucrtbase; it ships no CRT. There are 200k pseudo-random
arguments per function. Results differ in:

| function | differ | max ULP |
|---|---|---|
| atan2 | 17.8 % | 1 |
| hypot | 10.1 % | 1 |
| sin / cos / tan | 1.6 / 1.6 / 4.1 % | 1 |
| asin / acos / atan | 2.6 / 0.9 / 0.06 % | 1 |
| exp / log / pow | 0.5 / 0.004 / 0.08 % | 1 |
| cbrt / sinh / cosh / tanh | 29 / 28 / 25 / 6 % | 2 |
| sqrt, fmod | 0 | 0 |

## Caveat
The math library is not confirmed as the cause of the geometry differences. The
processor count also differs: Wine reports 64 CPUs (120 capped), the VM 16. That can
change TBB partitioning and summation order. To check the thread count, restrict
Inventor's CPU count on Wine to 16 (or the VM to 64) and compare Rim.ipt's rebuild.

## Task
Decide whether bit-compatibility with MS UCRT for the common transcendental functions
(atan2, hypot, sin/cos/tan, cbrt) is worth pursuing. Both are within 1-2 ULP, so this is
not a correctness bug. Nothing fails functionally.
