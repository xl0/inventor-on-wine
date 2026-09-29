# 064 Content Center empty: GetWindowsAccountDomainSid returns ERROR_INVALID_SID for non-account SIDs
Status: open (draft) · Owner: - · Branch: - · Found in: specialised-environments pass (inv3/:100, integ c036c687c47)

## Symptom
Content Center shows no libraries although the desktop libraries are installed
(`C:\ProgramData\Autodesk\Inventor 2027\Content Center\Libraries\*.idcl`, configured in
Default.ipj, App Options → Content Center = Inventor Desktop Content):
- Place from Content Center / Content Center Editor: empty category tree, Library list empty.
- API: `ContentCenter.TreeViewTopNode.ChildNodes` is empty.
- Frame Generator → Insert Frame: "Content Center server query failed. Please check your Content
  Center configuration ..."; the Frame Member section has no Standard/Family/Size.
Blocks everything built on Content Center: Frame Generator, Design Accelerator bolted
connection (fasteners), Tube & Pipe (fittings), Place from CC. (The VM has no CC libraries
installed, so there is no Inventor-level reference.)

## Evidence
Inventor started with a .NET EventPipe trace of exceptions (CoreCLR add-ins, .NET 10):
`DOTNET_EnableEventPipe=1 DOTNET_EventPipeOutputPath=C:\t\inv.nettrace
DOTNET_EventPipeConfig=Microsoft-Windows-DotNETRuntime:0x8000:4`, then `strings -el` on the
file. Opening Place from Content Center throws, once per configured library (13x):
```
System.ComponentModel.Win32Exception: Invalid SID.
  Connectivity.Core.Util.IOUtil::HasWritePermission(string, WindowsIdentity)
  Connectivity.Core.Database.TransactionContext::.ctor(...)
  Connectivity.Core.DataAccess.KnowledgeLibraries::GetKnowledgeLibraryByName(string)
```
(the .idcl SQLite files are never opened). HasWritePermission walks the library folder's ACL
(Everyone S-1-1-0, SYSTEM S-1-5-18, the user) with managed SecurityIdentifier APIs.
`SecurityIdentifier.AccountDomainSid` throws on Wine for non-account SIDs (.NET 4.8 probe:
Wine `S-1-1-0` / `S-1-5-18` → SystemException "Invalid SID."; Windows → null). .NET maps the
Win32 error of `GetWindowsAccountDomainSid`: success, ERROR_NON_ACCOUNT_SID (→ null) and
ERROR_INSUFFICIENT_BUFFER are expected, anything else is thrown.

## Windows ground truth (`tests/account_domain_sid.c`)
| SID | Win11 VM | Wine |
|---|---|---|
| S-1-1-0, S-1-5-18, S-1-5-32-544, S-1-5-11, S-1-2-0, S-1-5-4, S-1-16-12288 | FALSE, 1257 ERROR_NON_ACCOUNT_SID | FALSE, 1337 ERROR_INVALID_SID |
| S-1-5-21-1-2 (3 subauths) | FALSE, 1257 | FALSE, 1337 |
| S-1-5-80-1-2-3-4-5, S-1-5-22-1-2-3-4 | FALSE, 1257 | TRUE (S-1-5-80-1-2-3, S-1-5-22-1-2-3) |
| S-1-5-21-1-2-3[-...] | TRUE, S-1-5-21-1-2-3, size 24 | same |
EqualDomainSid already matches Windows for all rows (1258 ERROR_NON_DOMAIN_SID for non-domain).

## Task
kernelbase `GetWindowsAccountDomainSid` (semi-stub): only S-1-5-21-x-y-z[-...] are account
SIDs; everything else (valid) fails with ERROR_NON_ACCOUNT_SID. Add the table to
advapi32/tests/security.c. Then retest Content Center (ccprobe-like tree query, Place from CC)
and Frame Generator (tools/invscen/frame.cs + Insert Frame UI).
