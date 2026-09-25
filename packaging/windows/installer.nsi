; SkunkCrafts Updater - Windows installer (NSIS 3, built with makensis on Linux)
; Expects: /DVERSION=x.y.z /DSTAGE=<folder with SkunkcraftsUpdater.exe, sku.exe, README.txt, USER_GUIDE.html, THIRD_PARTY_NOTICES.txt> /DOUT=<output exe>
Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel admin

!include "MUI2.nsh"
!include "x64.nsh"

Name "SkunkCrafts Updater ${VERSION}"
OutFile "${OUT}"
InstallDir "$PROGRAMFILES64\SkunkCrafts Updater"
InstallDirRegKey HKLM "Software\SkunkCrafts\Updater" "InstallDir"
BrandingText "SkunkCrafts Updater ${VERSION}"

!define MUI_ICON "${STAGE}\icon.ico"
!define MUI_UNICON "${STAGE}\icon.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\SkunkcraftsUpdater.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start SkunkCrafts Updater"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"

Function .onInit
	${IfNot} ${RunningX64}
		MessageBox MB_OK|MB_ICONSTOP "SkunkCrafts Updater requires 64-bit Windows 10 or 11."
		Abort
	${EndIf}
FunctionEnd

Section "SkunkCrafts Updater" SecMain
	SectionIn RO
	SetOutPath "$INSTDIR"
	File "${STAGE}\SkunkcraftsUpdater.exe"
	File "${STAGE}\sku.exe"
	File "${STAGE}\README.txt"
	File "${STAGE}\USER_GUIDE.html"
	File "${STAGE}\THIRD_PARTY_NOTICES.txt"
	File "${STAGE}\icon.ico"
	WriteUninstaller "$INSTDIR\Uninstall.exe"

	CreateDirectory "$SMPROGRAMS\SkunkCrafts Updater"
	CreateShortcut "$SMPROGRAMS\SkunkCrafts Updater\SkunkCrafts Updater.lnk" "$INSTDIR\SkunkcraftsUpdater.exe" "" "$INSTDIR\icon.ico"
	CreateShortcut "$SMPROGRAMS\SkunkCrafts Updater\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
	CreateShortcut "$DESKTOP\SkunkCrafts Updater.lnk" "$INSTDIR\SkunkcraftsUpdater.exe" "" "$INSTDIR\icon.ico"

	WriteRegStr HKLM "Software\SkunkCrafts\Updater" "InstallDir" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater" "DisplayName" "SkunkCrafts Updater"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater" "DisplayVersion" "${VERSION}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater" "Publisher" "SkunkCrafts"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater" "DisplayIcon" "$INSTDIR\icon.ico"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater" "UninstallString" '"$INSTDIR\Uninstall.exe"'
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater" "InstallLocation" "$INSTDIR"
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater" "NoRepair" 1
SectionEnd

Section "Uninstall"
	; the program only: the user's preferences (%APPDATA%\SkunkCrafts) and the
	; per-X-Plane data (Output\skunkcrafts) are left alone
	Delete "$INSTDIR\SkunkcraftsUpdater.exe"
	Delete "$INSTDIR\sku.exe"
	Delete "$INSTDIR\README.txt"
	Delete "$INSTDIR\USER_GUIDE.html"
	Delete "$INSTDIR\THIRD_PARTY_NOTICES.txt"
	Delete "$INSTDIR\icon.ico"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"
	Delete "$SMPROGRAMS\SkunkCrafts Updater\SkunkCrafts Updater.lnk"
	Delete "$SMPROGRAMS\SkunkCrafts Updater\Uninstall.lnk"
	RMDir "$SMPROGRAMS\SkunkCrafts Updater"
	Delete "$DESKTOP\SkunkCrafts Updater.lnk"
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SkunkCraftsUpdater"
	DeleteRegKey HKLM "Software\SkunkCrafts\Updater"
SectionEnd
