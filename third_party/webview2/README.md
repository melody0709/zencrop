# WebView2 SDK Files

This folder contains the native WebView2 loader files vendored for the OCR Dashboard Markdown preview.

## Version

- Microsoft.Web.WebView2 SDK: 1.0.4022.49

## Source

- NuGet package: `Microsoft.Web.WebView2`
- Official docs: https://learn.microsoft.com/en-us/microsoft-edge/webview2/

## Files Used

- `include/WebView2.h`
- `include/WebView2EnvironmentOptions.h`
- `x64/WebView2Loader.dll`
- `x64/WebView2Loader.dll.lib`

Runtime still depends on the Microsoft Edge WebView2 Evergreen Runtime being installed on the machine. If the runtime is missing, the dashboard preview falls back to Source mode.
