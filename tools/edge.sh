#!/bin/bash
# Add the browser pieces Windows 11 ships in-box to $WINEPREFIX, from pinned installers
# in deps/ (same version as the VM): the WebView2 Evergreen Runtime (AdskLicensingAgent's
# sign-in UI) and Microsoft Edge as the default browser (the Autodesk sign-in form opens
# there). Used by tools/transplant.sh (inv-vm) and for the real install (prefixes/inv).
#   WINEPREFIX=... tools/edge.sh      (Wine: $ROOT/build)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${WINEPREFIX:?}"
wine=$ROOT/build/wine wineserver=$ROOT/build/server/wineserver

# WebView2 Evergreen Runtime 154.0.4258.37 x64 (= VM), https://go.microsoft.com/fwlink/?linkid=2124701
WV2=MicrosoftEdgeWebView2RuntimeInstallerX64.exe
WV2_SHA=771042db15cb5c463bac51a8408e70183d7130e8ac946709384c2223da582c1b
# Microsoft Edge Stable 154.0.4258.37 x64 enterprise MSI.
# URL + sha256 from edgeupdates.microsoft.com/api/products?view=enterprise
EDGE=MicrosoftEdgeEnterpriseX64-154.0.4258.37.msi
EDGE_SHA=4d8d922c8b2470084a380142cdfd51b2b28a83af7982d8f023cb8facbf258246

# It leaves MicrosoftEdgeUpdate running (and auto-start edgeupdate services).
echo "$WV2_SHA  $ROOT/deps/$WV2" | sha256sum -c --quiet
"$wine" "$ROOT/deps/$WV2" /silent /install
$wineserver -k
# Edge registers itself as the http/https handler (HKCR), like on Windows.
# Needs wofutil WofSetFileDataLocation (issue 024).
echo "$EDGE_SHA  $ROOT/deps/$EDGE" | sha256sum -c --quiet
"$wine" msiexec /i "Z:${ROOT//\//\\}\\deps\\$EDGE" /qn
# Wine's http/https UserChoice ProgId is "http"/"https": point those at Edge too.
e='"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"'
for k in http https; do "$wine" reg add "HKLM\\Software\\Classes\\$k\\shell\\open\\command" /ve /d "$e \"%1\"" /f; done
"$wine" reg add 'HKLM\Software\Classes\MSEdgeHTM\shell\open\command' /ve /d "$e --single-argument %1" /f
$wineserver -k
