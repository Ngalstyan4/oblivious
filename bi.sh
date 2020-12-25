time KBUILD_BUILD_VERSION=44 KBUILD_BUILD_TIMESTAMP='lunchtime' make CC="ccache gcc" -j12 && sudo make headers_install -j12 &&  sudo make modules_install -j12 
