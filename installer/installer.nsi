!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "WinMessages.nsh"
!include "LogicLib.nsh"

Name "MCP Server"
OutFile "mcp_server_installer.exe"
Icon "staging\\MnLLogo.ico"
InstallDir "$PROGRAMFILES\\MCP Server"
RequestExecutionLevel admin

!define STAGING_DIR "staging"
!define LAUNCHER "launcher.exe"

Var SM_CHECK
Var DESKTOP_CHECK
Var SERVICE_CHECK
Var INSTALL_SM
Var INSTALL_DESKTOP
Var INSTALL_SERVICE

!define MUI_ICON   "staging\\MnLLogo.ico"
!define MUI_UNICON "staging\\MnLLogo.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
Page custom OptionsPageCreate OptionsPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function OptionsPageCreate
  !insertmacro MUI_HEADER_TEXT "Install Options" "Choose shortcuts and service settings."
  nsDialogs::Create 1018
  Pop $0

  ${NSD_CreateLabel} 0 0 100% 12u "Shortcuts:"
  Pop $0
  ${NSD_CreateCheckbox} 8u 14u 100% 12u "Create Start Menu shortcut"
  Pop $SM_CHECK
  ${NSD_SetState} $SM_CHECK ${BST_CHECKED}
  ${NSD_CreateCheckbox} 8u 28u 100% 12u "Create Desktop shortcut"
  Pop $DESKTOP_CHECK
  ${NSD_SetState} $DESKTOP_CHECK ${BST_CHECKED}

  ${NSD_CreateLabel} 0 50u 100% 12u "Windows service (uses bundled nssm):"
  Pop $0
  ${NSD_CreateCheckbox} 8u 64u 100% 12u "Install and start MCPServer service after installation"
  Pop $SERVICE_CHECK
  ${NSD_SetState} $SERVICE_CHECK ${BST_CHECKED}

  nsDialogs::Show
FunctionEnd

Function OptionsPageLeave
  ${NSD_GetState} $SM_CHECK $R0
  ${If} $R0 == ${BST_CHECKED}
    StrCpy $INSTALL_SM "1"
  ${Else}
    StrCpy $INSTALL_SM "0"
  ${EndIf}

  ${NSD_GetState} $DESKTOP_CHECK $R0
  ${If} $R0 == ${BST_CHECKED}
    StrCpy $INSTALL_DESKTOP "1"
  ${Else}
    StrCpy $INSTALL_DESKTOP "0"
  ${EndIf}

  ${NSD_GetState} $SERVICE_CHECK $R0
  ${If} $R0 == ${BST_CHECKED}
    StrCpy $INSTALL_SERVICE "1"
  ${Else}
    StrCpy $INSTALL_SERVICE "0"
  ${EndIf}
FunctionEnd

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${STAGING_DIR}\*.*"

  ${If} $INSTALL_SM == "1"
    CreateDirectory "$SMPROGRAMS\\MCP Server"
    CreateShortCut "$SMPROGRAMS\\MCP Server\\MCP Server.lnk" "$INSTDIR\\${LAUNCHER}" "" "$INSTDIR\\MnLLogo.ico" 0
  ${EndIf}

  ${If} $INSTALL_DESKTOP == "1"
    CreateShortCut "$DESKTOP\\MCP Server.lnk" "$INSTDIR\\${LAUNCHER}" "" "$INSTDIR\\MnLLogo.ico" 0
  ${EndIf}

  WriteUninstaller "$INSTDIR\\Uninstall.exe"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MCP Server" "DisplayName" "MCP Server"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MCP Server" "UninstallString" "$INSTDIR\\Uninstall.exe"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MCP Server" "DisplayIcon" "$INSTDIR\\MnLLogo.ico"

  CreateDirectory "$INSTDIR\\logs"

  ${If} $INSTALL_SERVICE == "1"
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" install "MCPServer" "$INSTDIR\\mcp_stdio_server.exe"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" set "MCPServer" AppDirectory "$INSTDIR"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" set "MCPServer" AppStdout "$INSTDIR\\logs\\stdout.log"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" set "MCPServer" AppStderr "$INSTDIR\\logs\\stderr.log"'
    ExecWait '"$INSTDIR\\nssm\\nssm.exe" start "MCPServer"'
  ${EndIf}
SectionEnd

Section "Uninstall"
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
