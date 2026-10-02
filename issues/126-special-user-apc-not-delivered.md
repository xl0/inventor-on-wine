# 126 QueueUserAPC2 special user APC succeeds but never interrupts a running thread
Status: open (draft) · Owner: - · Branch: - · Found in: 124 probe (integ d7799da4d5c)

## Symptom
`QueueUserAPC2(func, thread, data, QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC | QUEUE_USER_APC_CALLBACK_DATA_CONTEXT)`
returns TRUE on Wine, but the APC only runs at the target's next alertable wait
(`call_user_apc_dispatcher`: "flags are not supported"; the server queues it as a normal user APC).
Windows 11 interrupts a thread running user code at once and passes the interrupted context
(flags 0x10005b + CONTEXT_EX with xstate).

Repro: `tests/r124/xstate_ctx.exe apc` — "target spinning": Windows `apc delivered: 1`, Wine `0`
(not even after the thread leaves its loop); "in alertable wait": delivered on both (Wine: context
flags 0x10000b, CONTEXT_EX all zero).

## Why it may matter
CoreCLR (.NET 10, `Thread::InitializeSpecialUserModeApc`) probes exactly this call on its own thread
and, since it succeeds, switches GC/debugger thread suspension from SuspendThread + SetThreadContext
redirection to special-APC activation injection. Threads in managed loops without GC polls would then
only stop at return-address hijacks.
A quick check showed no stall: `tests/r124/gc_suspend.cs` (GC.Collect while a thread spins in a
call-free loop) max 12 ms on Wine, 3 ms on Windows. So no observed app impact yet; either implement
delivery (interrupt like a suspend, dispatch KiUserApcDispatcher with the interrupted context) or
fail the special flag so callers fall back.
