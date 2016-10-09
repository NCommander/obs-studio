REM @Echo Off
SET build_config=Release
SET obs_version=0.16.2-ftl.11
SET coredeps=C:\beam\tachyon_deps
SET QTDIR64=C:\Qt\5.6\msvc2015_64
SET QTDIR32=C:\Qt\5.6\msvc2015
SET browser=C:\beam\obs-browser-1.23
SET PATH=%PATH%;C:\Program Files (x86)\MSBuild\14.0\Bin;C:\Program Files (x86)\CMake\bin
SET startingPath=%cd%
SET DepsPath32=%coredeps%\win32
SET DepsPath64=%coredeps%\win64
SET build32=
SET build64=
SET package=
SET install_browser=
if "%1" == "all" (
SET build32=true
SET build64=true
SET package=true
REM SET install_browser=true
)
if "%1" == "win64" (
SET build64=true
)
if "%1" == "package" (
SET package=true
REM SET install_browser=true
)
if "%1" == "clean" (
   IF NOT EXIST ..\build GOTO NOBUILDDIR
   rmdir ..\build /s /q
   :NOBUILDDIR
   goto DONE
)
pushd .
cd ..
pushd .
call git submodule update --init
popd .
IF EXIST build GOTO BUILD_DIR_EXISTS
mkdir build
:BUILD_DIR_EXISTS
cd build
if defined build32 (
	ECHO Building 32bit OBS-Studio FTL
	echo Currently in Directory %cd%	
    rmdir CMakeFiles /s /q
	del CMakeCache.txt
	cmake -G "Visual Studio 14 2015" -DOBS_VERSION_OVERRIDE=%obs_version% -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true .. || goto DONE
	call msbuild /p:Configuration=%build_config% ALL_BUILD.vcxproj
	copy %coredeps%\win32\bin\postproc-54.dll rundir\%build_config%\bin\32bit
)
if defined build64 (
	ECHO Building 64bit OBS-Studio FTL
	echo Currently in Directory %cd%	
    rmdir CMakeFiles /s /q
	del CMakeCache.txt
	cmake -G "Visual Studio 14 2015 Win64" -DOBS_VERSION_OVERRIDE=%obs_version% -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true .. || goto DONE
	call msbuild /p:Configuration=%build_config%,Platform=x64 ALL_BUILD.vcxproj || goto DONE
	copy %coredeps%\win64\bin\postproc-54.dll rundir\%build_config%\bin\64bit
)
if defined package (
	ECHO Packaging OBS-Studio FTL
	echo Currently in Directory %cd%	
	SET obsInstallerTempDir=%cd:\=/%/rundir/%build_config%
    rmdir CMakeFiles /s /q
	del CMakeCache.txt
	cmake -G "Visual Studio 14 2015" -DOBS_VERSION_OVERRIDE=%obs_version% -DINSTALLER_RUN=true ..
	if defined install_browser (
		if defined build64 (
			xcopy %browser%\obs-plugins\64bit\* %cd%\rundir\%build_config%\obs-plugins\64bit\ /s /e /y
		)
		if defined build32 (
			xcopy %browser%\obs-plugins\32bit\* %cd%\rundir\%build_config%\obs-plugins\32bit\ /s /e /y
		)
	)
	call msbuild /p:Configuration=%build_config% PACKAGE.vcxproj || goto DONE
)
GOTO DONE
:SUB_FTLSDK
    ECHO Building FTL SDK
	pushd .
	call git clone https://github.com/WatchBeam/ftl-sdk.git
	cd ftl-sdk
	call git checkout xsplit
	mkdir build
	cd build
	call cmake -G "Visual Studio 14 2015 Win64" .. || goto DONE
	call msbuild /p:Configuration=%build_config%,Platform=x64 ALL_BUILD.vcxproj || goto DONE
	SET ftl_lib_dir=%cd%\%build_config%\ftl.lib
	SET ftl_inc_dir=%cd%\..\libftl	
	popd .
	GOTO:EOF 
:DONE
cd %startingPath%

