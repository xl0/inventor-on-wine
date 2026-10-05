#!/bin/bash
# build-wpf.sh: compile wpf.cs with the .NET 4.8 csc of WINEPREFIX (WINEBUILD set) (174)
cd "$(dirname "$0")"
F='C:\windows\Microsoft.NET\Framework64\v4.0.30319'
WINEDEBUG=-all $WINEBUILD/wine "$F\\csc.exe" /nologo /debug- /platform:x64 /out:wpf.exe \
	"/r:$F\\WPF\\PresentationFramework.dll" "/r:$F\\WPF\\PresentationCore.dll" "/r:$F\\WPF\\WindowsBase.dll" \
	"/r:$F\\System.Xaml.dll" "/r:$F\\System.Windows.Forms.dll" "/r:$F\\System.Drawing.dll" wpf.cs </dev/null
