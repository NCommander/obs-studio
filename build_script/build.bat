rem @echo off
SET build_config=Release
SET obs_version=19.0.2-ftl.0.9.4
SET coredeps=C:\beam\tachyon_deps
SET QTDIR64=C:\Qt\Qt5.9.0\5.9\msvc2015_64
SET QTDIR32=C:\Qt\Qt5.9.0\5.9\msvc2015
SET build_browser=OFF
SET cef_root_64=C:\beam\cef_binary_3.3071.1634.g9cc59c8_windows64
SET cef_root_32=C:\beam\cef_binary_3.3071.1634.g9cc59c8_windows32
SET browser=C:\beam\obs-browser-1.29
SET PATH=%path%;C:\Program Files ^(x86^)\MSBuild\14.0\Bin;C:\Program Files\CMake\bin
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
SET install_browser=true
)
if "%1" == "win64" (
SET build64=true
)
if "%1" == "package" (
SET package=true
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
IF NOT EXIST "plugins\libftl\ftl-sdk\libftl" call git submodule update --init
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
	cmake -G "Visual Studio 14 2015" -DCOMPILE_D3D12_HOOK=true -DCEF_ROOT_DIR=%cef_root_32% -DBUILD_BROWSER=%build_browser% -DOBS_VERSION_OVERRIDE=%obs_version% -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true .. || goto DONE
	call msbuild /p:Configuration=%build_config% ALL_BUILD.vcxproj
	copy %coredeps%\win32\bin\postproc-54.dll rundir\%build_config%\bin\32bit
)
if defined build64 (
	ECHO Building 64bit OBS-Studio FTL
	echo Currently in Directory %cd%	
    rmdir CMakeFiles /s /q
	del CMakeCache.txt
	cmake -G "Visual Studio 14 2015 Win64" -DCEF_ROOT_DIR=%cef_root_64% -DBUILD_BROWSER=%build_browser% -DCOMPILE_D3D12_HOOK=true -DOBS_VERSION_OVERRIDE=%obs_version% -DCOPIED_DEPENDENCIES=false -DCOPY_DEPENDENCIES=true .. || goto DONE
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
	call git clone https://github.com/mixer/ftl-sdk.git
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

