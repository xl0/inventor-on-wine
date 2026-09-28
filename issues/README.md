One file per Wine bug: `NNN-slug.md`. Header line: Status (open / wip / fixed /
merged / wontfix), Owner, Branch, where it was found. Body: symptom, Windows
ground truth, cause, task; the owner appends findings and the outcome. The file
is the worker's brief and memory — keep it current enough to resume from.
Workers file new bugs they uncover as drafts (next free number) and report
them; the coordinator assigns a new worker, merges its fix into `integ`, and
asks the original worker to rebase if it was blocked. See notes/worker.md.
