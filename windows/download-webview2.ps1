# Downloads the WebView2 SDK files (header + loader DLL) from NuGet.
# Usage:  powershell -ExecutionPolicy Bypass -File download-webview2.ps1
$ErrorActionPreference = "Stop"

$ver = "1.0.2903.40"
$url = "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$ver"

Write-Host "Downloading WebView2 SDK $ver ..."
Invoke-WebRequest -Uri $url -OutFile "webview2.nupkg"

Write-Host "Extracting ..."
Expand-Archive "webview2.nupkg" -DestinationPath "webview2pkg"

Copy-Item "webview2pkg/build/native/include/WebView2.h" "WebView2.h" -Force
Copy-Item "webview2pkg/runtimes/win-x64/native/WebView2Loader.dll" "WebView2Loader.dll" -Force

Remove-Item "webview2.nupkg" -Recurse
Remove-Item "webview2pkg" -Recurse

Write-Host "OK: WebView2.h e WebView2Loader.dll prontos."
