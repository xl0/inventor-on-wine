# 021 Whole-file mappings break large PE handling in 32-bit processes (low)
Status: open (draft, low) · Owner: - · Branch: - · Found in: 019 audit

- imagehlp `IMAGEHLP_RecalculateChecksum` (used by ImageAddCertificate /
  ImageRemoveCertificate) and `ImageGetDigestStream` map the whole file; in
  32-bit processes this fails with ERROR_NOT_ENOUGH_MEMORY around 2 GB, while
  Windows' i386 build handles such files.
- wintrust `pe_image_hash` (CryptCATAdmin hashing) maps the whole file and uses
  only the low DWORD of GetFileSize.
Fix direction: hash/checksum in chunks via ReadFile or sliding views. No known
app impact (Autodesk verification uses the 64-bit path).
