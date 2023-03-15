!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!if ${PLATFORM} == x64
	!define MULTIUSER_USE_PROGRAMFILES64
!endif
!define MULTIUSER_INSTALLMODE_INSTDIR "vkQuake"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\vkQuake"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "InstallDir"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "Software\vkQuake"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME "InstallMode"

!include "MultiUser.nsh"
!include "MUI2.nsh"
!include LogicLib.nsh

Name "vkQuake"
ManifestDPIAware true
Unicode True
SetCompressor lzma
OutFile vkQuake-Installer-${PLATFORM}-${VERSION}.exe
ShowInstDetails show

Var NextButton
Var RereleaseDir
Var ClassicDir
Var RereleaseDirRequest
Var ClassicDirRequest
Var RereleaseDirBrowse
Var ClassicDirBrowse
Var StartMenuFolder

Function GetQuakePathSteam
	SetRegView 32
	ClearErrors
	ReadRegStr $0 HKLM "SOFTWARE\GOG.com\Games\1435828198" "PATH"
	${IfNot} ${Errors}
		StrCpy $ClassicDir $0
	${EndIf}
	SetRegView 64
	ClearErrors
	# Steam doesn't always properly update this sigh
	ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 2310" "InstallLocation"
	${IfNot} ${Errors}
		StrCpy $ClassicDir $0
		StrCpy $RereleaseDir "$0\rerelease"
	${EndIf}
FunctionEnd

Function .onInit
	!insertmacro MULTIUSER_INIT
	Call GetQuakePathSteam
	!if ${PLATFORM} == x64
		SetRegView 64
	!else
		SetRegView 32
	!endif
FunctionEnd

Function OnBrowseRereleaseDir
    ${NSD_GetText} $RereleaseDirRequest $RereleaseDir
    nsDialogs::SelectFolderDialog "Select rerelease Quake directory" $RereleaseDir
    Pop $0
    ${If} $0 != error
        ${NSD_SetText} $RereleaseDirRequest "$0"
		StrCpy $RereleaseDir "$0"
    ${EndIf}
FunctionEnd

Function OnBrowseClassicDir
    ${NSD_GetText} $ClassicDirRequest $ClassicDir
    nsDialogs::SelectFolderDialog "Select classic Quake directory" $ClassicDir
    Pop $0
    ${If} $0 != error
        ${NSD_SetText} $ClassicDirRequest "$0"
		StrCpy $ClassicDir "$0"
    ${EndIf}
FunctionEnd

Function ValidateQuakeDirectories
	IfFileExists "$ClassicDir\id1\pak0.pak" ok
	IfFileExists "$RereleaseDir\QuakeEX.kpf" ok
	EnableWindow $NextButton 0
	Return
	ok:
	EnableWindow $NextButton 1
FunctionEnd

Function QuakeDirectories
	GetDlgItem $NextButton $HWNDPARENT 1

	!insertmacro MUI_HEADER_TEXT "Quake Directories" "Choose game directories. At least one needs to be set."
	nsDialogs::Create 1018
	Pop $0
	
	${NSD_CreateLabel} 0 0 100% 12u "Choose rerelease Quake folder"
	Pop $0

	${NSD_CreateDirRequest} 0 13u 240u 13u "$RereleaseDir"
	Pop $RereleaseDirRequest
	
	${NSD_CreateBrowseButton} 242u 13u 50u 13u "Browse..."
	Pop $RereleaseDirBrowse
	${NSD_OnClick} $RereleaseDirBrowse OnBrowseRereleaseDir
	
	${NSD_CreateLabel} 0 34u 100% 12u "Choose classic Quake folder"
	Pop $0

	${NSD_CreateDirRequest} 0 47u 240u 13u "$ClassicDir"
	Pop $ClassicDirRequest
	
	${NSD_CreateBrowseButton} 242u 47u 50u 13u "Browse..."
	Pop $ClassicDirBrowse
	${NSD_OnClick} $ClassicDirBrowse OnBrowseClassicDir

	Call ValidateQuakeDirectories	
	nsDialogs::Show
FunctionEnd

Function LeaveQuakeDirectories
	${NSD_GetText} $RereleaseDirRequest $RereleaseDir
    ${NSD_GetText} $ClassicDirRequest $ClassicDir
FunctionEnd

Function un.onInit
	!insertmacro MULTIUSER_UNINIT
	!if ${PLATFORM} == x64
		SetRegView 64
	!else
		SetRegView 32
	!endif
FunctionEnd

!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY
Page Custom QuakeDirectories LeaveQuakeDirectories

!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\vkQuake"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "vkQuake"

!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section "Game" GAME
	SectionIn RO
	SetOutPath "$INSTDIR"
    File "${SRCDIR}\*.exe"
	File "${SRCDIR}\*.dll"
	File "..\..\LICENSE.txt"
	
	!insertmacro MUI_STARTMENU_WRITE_BEGIN Application
		CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
		${If} $RereleaseDir != ""
			CreateShortCut "$SMPROGRAMS\$StartMenuFolder\vkQuake (rerelease).lnk" $OUTDIR\vkQuake.exe '-multiuser -basedir "$RereleaseDir"' \
				"$OUTDIR\vkQuake.exe" "" "" "" ""
			CreateShortCut "$INSTDIR\vkQuake (rerelease).lnk" $OUTDIR\vkQuake.exe '-multiuser -basedir "$RereleaseDir"' \
				"$OUTDIR\vkQuake.exe" "" "" "" ""
		${Endif}
		${If} $ClassicDir != ""
			CreateShortCut "$SMPROGRAMS\$StartMenuFolder\vkQuake (classic).lnk" $OUTDIR\vkQuake.exe '-multiuser -basedir "$ClassicDir"' \
				"$OUTDIR\vkQuake.exe" "" "" "" ""
			CreateShortCut "$INSTDIR\vkQuake (classic).lnk" $OUTDIR\vkQuake.exe '-multiuser -basedir "$ClassicDir"' \
				"$OUTDIR\vkQuake.exe" "" "" "" ""
		${Endif}
	!insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

;***************************
;Uninstaller Sections
;***************************
Section "-Uninstaller"
	WriteUninstaller $INSTDIR\uninstaller.exe
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake" "DisplayName" "vkQuake"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake" "UninstallString" "$INSTDIR\uninstaller.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake" "QuietUninstallString" "$\"$INSTDIR\uninstaller.exe$\" /S"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake" "Publisher" "vkQuake developers"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake" "URLInfoAbout" "https://github.com/Novum/vkQuake"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake" "NoModify" "1"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake" "NoRepair" "1"
SectionEnd

!macro Clean UN
Function ${UN}Clean
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\vkQuake"
	DeleteRegKey HKLM "Software\vkQuake"
	RMDir /r $INSTDIR
	!insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
	Delete "$SMPROGRAMS\$StartMenuFoldervkQuake (rerelease).lnk"
	Delete "$SMPROGRAMS\$StartMenuFolder\vkQuake (classic).lnk"
	RMDir "$SMPROGRAMS\$StartMenuFolder"
	SetAutoClose true
FunctionEnd
!macroend

!insertmacro Clean ""
!insertmacro Clean "un."

Section "Uninstall"
	Call un.Clean
SectionEnd

Function .onInstFailed
	Call Clean
FunctionEnd
