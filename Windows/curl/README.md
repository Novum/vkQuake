# libcurl for Windows

Official curl **8.23.0**, Windows build **8.23.0_10**, from
<https://curl.se/windows/>. The DLLs include their dependencies and use the
Windows certificate store through `CURLSSLOPT_NATIVE_CA`.

The headers, DLLs, export definitions and MinGW import libraries are copied
unchanged from these packages:

| Architecture | Package | SHA-256 |
| --- | --- | --- |
| x64 | [curl-8.23.0_10-win64-mingw.zip](https://curl.se/windows/dl-8.23.0_10/curl-8.23.0_10-win64-mingw.zip) | `7157104332c15c80edba0359446584cb317c63f4c3cf5e569b76ecad5aa0e197` |
| ARM64 | [curl-8.23.0_10-win64a-mingw.zip](https://curl.se/windows/dl-8.23.0_10/curl-8.23.0_10-win64a-mingw.zip) | `cc281bf89a38656dc553b77e19814ed87e3ee909c41e10332784e367cbb3f891` |

The MSVC import library is generated in a Visual Studio developer shell:

```powershell
lib /def:lib/x64/libcurl-x64.def /machine:x64 /name:libcurl-x64.dll /out:lib/x64/libcurl.lib
```

`curl-LICENSE.txt` contains the upstream license and the licenses for the
dependencies linked into the DLLs. Include it when distributing them.

When updating, check the latest release and security advisories at
<https://curl.se/docs/security.html>, verify both package checksums, replace
the headers and libraries together, and regenerate the MSVC import library.
Linux uses distribution packages with backported security fixes; macOS uses Homebrew curl.
