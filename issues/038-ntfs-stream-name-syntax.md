# 038 "file:" / "file:stream" names create literal files instead of streams / failing
Status: open (draft) · Owner: - · Branch: - · Found in: issue 035 probe

## Symptom / ground truth (CreateFileA CREATE_ALWAYS in a temp dir)
| name            | Win11                     | Wine (4e819f054dd)       |
|-----------------|---------------------------|--------------------------|
| foo:            | ERROR_INVALID_NAME (123)  | ok, file "foo:"          |
| foo::           | 123                       | ok, file "foo::"         |
| foo::$DATA      | ok, file "foo"            | ok, file "foo::$DATA"    |
| foo:bar         | ok, file "foo" (+stream)  | ok, file "foo:bar"       |
| foo:bar:        | 123                       | ok, file "foo:bar:"      |
| foo:bar:$DATA   | ok, file "foo" (+stream)  | ok, file "foo:bar:$DATA" |

Wine has no alternate data streams at all (long-standing upstream gap). The
cheap part is rejecting invalid stream syntax and mapping `::$DATA` to the
main stream; real streams are a big feature. No known app impact yet —
only worth doing if an app hits it.
