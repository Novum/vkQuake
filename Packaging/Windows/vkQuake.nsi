!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_USE_PROGRAMFILES64

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

Var StartMenuFolder

Function .onInit
	!insertmacro MULTIUSER_INIT
	SetRegView 64
FunctionEnd

Function un.onInit
	!insertmacro MULTIUSER_UNINIT
	SetRegView 64
FunctionEnd

!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY

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
	File "${SRCDIR}\vkQuake.pdb"
	File "${SRCDIR}\*.dll"
	File "..\..\LICENSE.txt"
	
	!insertmacro MUI_STARTMENU_WRITE_BEGIN Application
		CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
		# the engine locates Steam/GOG/Epic Quake installs at startup or asks for the folder, no -basedir needed
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\vkQuake (classic).lnk" $OUTDIR\vkQuake.exe "-multiuser -original" \
			"$OUTDIR\vkQuake.exe" "" "" "" ""
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\vkQuake (rerelease).lnk" $OUTDIR\vkQuake.exe "-multiuser -remastered" \
			"$OUTDIR\vkQuake.exe" "" "" "" ""
		CreateShortCut "$INSTDIR\vkQuake (classic).lnk" $OUTDIR\vkQuake.exe "-multiuser -original" \
			"$OUTDIR\vkQuake.exe" "" "" "" ""
		CreateShortCut "$INSTDIR\vkQuake (rerelease).lnk" $OUTDIR\vkQuake.exe "-multiuser -remastered" \
			"$OUTDIR\vkQuake.exe" "" "" "" ""
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
	RMDir /r "$SMPROGRAMS\$StartMenuFolder"
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
