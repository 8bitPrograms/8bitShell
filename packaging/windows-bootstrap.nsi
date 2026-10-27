; A single Windows product: EXE is a bootstrap for the SAME MSI, not a second
; installer/uninstaller with conflicting ownership of the installation directory.
Unicode true
Name "8bitShell"
OutFile "${OUTPUT_EXE}"
Icon "${APP_ICON}"
VIProductVersion "${FILE_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "8bitShell"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "8bitShell setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
RequestExecutionLevel user
SilentInstall silent
SetCompressor /SOLID lzma
Section
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=8bitShell.msi "${INPUT_MSI}"
    ExecWait '"$SYSDIR\msiexec.exe" /i "$PLUGINSDIR\8bitShell.msi"' $0
    SetErrorLevel $0
    StrCmp $0 0 done
    StrCmp $0 3010 done
    StrCmp $0 1602 done
    MessageBox MB_OK|MB_ICONSTOP "8bitShell setup failed (Windows Installer code $0)."
    done:
SectionEnd
