#!/bin/bash
#arg1: folder to clone into
#arg2 git repo
#arg3: tag/branch to checkout
function getrepo {
  if [ ! -d $1 ];
  then
    mkdir $1
  fi
  cd $1
  git pull
  if [ $? -eq 128 ];
  then
# repo doesnt exist
    git clone $2 .
  fi
  git checkout $3
}
obs_version=0.16.2-ftl.11
do_browser=false;
#do_opus=false;
#do_vp8=false;
#do_ffmpeg=false;
#do_ftlsdk=false;
#do_x264=false;

while getopts ":b" opt; do
  case $opt in
    b)
      do_browser=true;
      ;;
    \?)
      echo "Invalid option: -$OPTARG" >&2
      ;;
  esac
done
INSTALL_PREFIX='/opt/obsftl'
echo "Installing OBS Studio FTL and it's dependancies to $INSTALL_PREFIX"
if [ -d "$INSTALL_PREFIX" ]; then
  echo -e "$INSTALL_PREFIX already exists.  Is it okay to completely remove this folder [Y/n]? "
  read response
  if [ $response == 'Y' ]
  then
    rm -fr $INSTALL_PREFIX
  fi
fi
mkdir -p $INSTALL_PREFIX
export PKG_CONFIG_PATH=$INSTALL_PREFIX/lib/pkgconfig/:$PKG_CONFIG_PATH
cd ..
pushd .
cd ..
##################
#opus#
##################
getrepo opus https://git.xiph.org/opus.git v1.1.2
./autogen.sh
./configure --prefix=$INSTALL_PREFIX
make
if [ $? -ne 0 ];
then
  echo "Make failed, exiting"
  exit
fi
make install
if [ $? -ne 0 ];
then
  echo "Make Install failed, exiting"
  exit
fi
cd ..
#######################################
####ffmpeg with opus
########################################
getrepo ffmpeg https://git.ffmpeg.org/ffmpeg.git n3.0.2
./configure --enable-shared --enable-libopus --prefix=$INSTALL_PREFIX
if [ $? -ne 0 ];
then
  echo "ffmpeg configure failed, exiting"
  exit
fi
make
if [ $? -ne 0 ];
then
  echo "Make failed, exiting"
  exit
fi
make install
if [ $? -ne 0 ];
then
  echo "Make Install failed, exiting"
  exit
fi
cd ..
##########################
#libx264
##########################
getrepo x264 http://git.videolan.org/git/x264.git 2102de2 
./configure --enable-shared --prefix=$INSTALL_PREFIX
if [ $? -ne 0 ];
then
  echo "cmake failure or ftl-sdk, exiting"
  exit
fi
make -j$(grep -c '^processor' /proc/cpuinfo)
if [ $? -ne 0 ];
then
  echo "Make failed, exiting"
  exit
fi
make install
if [ $? -ne 0 ];
then
  echo "Make Install failed, exiting"
  exit
fi
cd ..
##########################
#OBS Studio FTL
##########################
popd
git submodule update --init
mkdir build
cd build
cmake -DUNIX_STRUCTURE=1 -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX -DOBS_VERSION_OVERRIDE=$obs_version ..
if [ $? -ne 0 ];
then
  echo "cmake failure of OBS, exiting"
  exit
fi
make -j$(grep -c '^processor' /proc/cpuinfo)
if [ $? -ne 0 ];
then
  echo "Make failed, exiting"
  exit
fi
make install
if [ $? -ne 0 ];
then
  echo "Make Install failed, exiting"
  exit
fi
cd ../.. 
##################################
# Browser
##################################
if [ $do_browser == true ]
then
  getrepo obs-qtwebkit https://github.com/bazukas/obs-qtwebkit.git HEAD
  make
  make install
  cd ..
fi


