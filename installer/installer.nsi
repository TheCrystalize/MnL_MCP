!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "WinMessages.nsh"

Name "MCP Server"
OutFile "mcp_server_installer.exe"
Icon "staging\\MnLLogo.ico"
InstallDir "$PROGRAMFILES\\MCP Server"
RequestExecutionLevel admin

!define STAGING_DIR "staging"
!define LAUNCHER "launcher.exe"
Var SERVICE_CHECK
Var INSTALL_SERVICE

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
Page custom ServicePageCreate ServicePageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

Function ServicePageCreate
  nsDialogs::Create 1018
  Pop $0
  ${NSD_CreateLabel} 0 0 100% 12u "Optional: install MCP Server as a Windows service (uses bundled nssm)"
  Pop $0
  ${NSD_CreateCheckbox} 0 12u 100% 12u "Install and start service after installation"
  Pop $SERVICE_CHECK
  ${NSD_SetState} $SERVICE_CHECK ${BST_CHECKED}
  nsDialogs::Show
FunctionEnd

Function ServicePageLeave
  ${NSD_GetState} $SERVICE_CHECK $R0
  StrCmp $R0 ${BST_CHECKED} +2
    StrCpy $INSTALL_SERVICE "0"
    Goto done
  StrCpy $INSTALL_SERVICE "1"
done:
FunctionEnd

Section "Install"
  SetOutPath "$INSTDIR"
  ; include all files prepared in installer\staging
  File /r "${STAGING_DIR}\*.*"

  ; create shortcuts (Start Menu + Desktop) with icon
  CreateDirectory "$SMPROGRAMS\\MCP Server"
  CreateShortCut "$SMPROGRAMS\\MCP Server\\MCP Server.lnk" "$INSTDIR\\${LAUNCHER}" "" "$INSTDIR\\MnLLogo.ico" 0
  CreateShortCut "$DESKTOP\\MCP Server.lnk" "$INSTDIR\\${LAUNCHER}" "" "$INSTDIR\\MnLLogo.ico" 0

  ; Uninstaller entry
  WriteUninstaller "$INSTDIR\\Uninstall.exe"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MCP Server" "DisplayName" "MCP Server"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MCP Server" "UninstallString" "$INSTDIR\\Uninstall.exe"

  ; Prepare logs folder for service
  CreateDirectory "$INSTDIR\\logs"

  ; If user selected service installation, install and start service via bundled nssm
  StrCmp $INSTALL_SERVICE "1" 0 +6
    ; Install service (service name: MCPServer)
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" install "MCPServer" "$INSTDIR\\mcp_stdio_server.exe"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" set "MCPServer" AppDirectory "$INSTDIR"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" set "MCPServer" AppStdout "$INSTDIR\\logs\\stdout.log"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" set "MCPServer" AppStderr "$INSTDIR\\logs\\stderr.log"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" start "MCPServer"'
SectionEnd

Section "Uninstall"
  ; Stop and remove service if present (use nssm from installed dir)
  IfFileExists "$INSTDIR\\nssm\\nssm.exe" 0 +4
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" stop "MCPServer"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" remove "MCPServer" confirm'

  Delete "$SMPROGRAMS\\MCP Server\\MCP Server.lnk"
  Delete "$DESKTOP\\MCP Server.lnk"
  RMDir "$SMPROGRAMS\\MCP Server"
  Delete "$INSTDIR\\Uninstall.exe"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MCP Server"
SectionEnd