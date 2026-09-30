# 093 Absolute wait timeouts expire up to ~1 ms early (coarse client clock)
Status: open (draft) · Owner: - · Branch: - · Found in: 089 worker (wt/089, integ 481a8f5f6ab)

## Symptom
NtWaitForSingleObject with an absolute timeout 1 ms in the future returns after ~0.05 ms on Wine.
The client converts absolute NT times using a coarse clock (CLOCK_REALTIME_COARSE, ~1-4 ms
granularity) while the server compares against a precise clock. Same root cause as the MsgWait
early return fixed in 088 (281e8530a1e → relative remaining time).

## Task
Windows ground truth for absolute waits (VM), then make absolute deadlines exact (precise clock
for the conversion, or convert to relative on the precise clock). Check waitable timers with
absolute due times too. Interacts with 089 (tick rounding) if merged.
