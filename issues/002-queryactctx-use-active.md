# 002 QueryActCtxW USE_ACTIVE_ACTCTX without an active frame
Status: fixed · Owner: worker · Branch: fix/002-queryactctx-use-active · Found in: tests/sxs_probe child

## Symptom
QueryActCtxW(QUERY_ACTCTX_FLAG_USE_ACTIVE_ACTCTX, NULL, ...,
ActivationContextDetailedInformation, ...) in a process with an exe manifest
and no explicitly activated context → Wine ERROR_INVALID_PARAMETER (87).
Windows returns the process default context's info.

## Windows ground truth (Win11 VM, probe exe with embedded manifest)
With no active frame (also after ActivateActCtx(NULL)), USE_ACTIVE, IS_HMODULE
and IS_ADDRESS (exe or kernel32, whose loader entry has no context) all behave
exactly like hActCtx=NULL: Basic → ok, hActCtx NULL; Detailed, AssemblyDetailed,
Runlevel, Compatibility → the process default context.

## Cause
ntdll/actctx.c find_query_actctx(): only the plain NULL-handle branch fell
back to process_actctx; USE_ACTIVE with no frame and module queries with
pldr->ActivationContext NULL left actctx NULL → INVALID_PARAMETER (87).

## Outcome
Commit 002a5b10864 on fix/002-queryactctx-use-active: apply the non-Basic
fallback after all three branches. Test in test_app_manifest (child with exe
manifest): Detailed via USE_ACTIVE and via IS_HMODULE(kernel32).
kernel32:actctx passes on Wine (x86_64+i386) and the VM (x86_64+i386); the
new checks fail on unpatched Wine.
tests/sxs_probe on this build: the query works; children now print
"- (0 assemblies)" where the dependency isn't resolved (issue 001). Note Wine
still starts those processes with an empty default context, where Windows
fails CreateProcess with 14001 (missing dependency) — separate bug.
Side note: ActivationContextManifestResourceName (7) → Wine err 1, Windows 87.
