#!/bin/bash

#
# Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
# This is free and unencumbered software released into the public domain.
#

# Backup current folder
pushd .
export CURDIR="$( cd "$(dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd "$CURDIR/"
export PATH="$PATH:$CURDIR/armv6-linux-musleabi-cross/bin"
export MUSL_PREFIX="$CURDIR/armv6-linux-musleabi-cross/armv6-linux-musleabi"

# Misc
MUSL_ARMv6_LINK="https://musl.cc/armv6-linux-musleabi-cross.tgz"
CURL_LINK="https://github.com/curl/curl/releases/download/curl-8_8_0/curl-8.8.0.tar.xz"
BEARSSL_REPO="https://www.bearssl.org/git/BearSSL"
BEARSSL_HASH="79c060eea3eea1257797f15ea1608a9a9923aa6f"

download_musl_armv6() {
	echo "[+] Downloading musl ..."
	wget "$MUSL_ARMv6_LINK" -O armv6-musl.tgz
	tar xvf armv6-musl.tgz
	popd
}

download_build_bearssl() {
	echo "[+] Cloning BearSSL ..."
	git clone "$BEARSSL_REPO"
	cd BearSSL/
	git checkout "$BEARSSL_HASH"
	cp ../armv6.mk conf/
	echo "[+] Building ..."
	make CONF=armv6
	# Copy to the right path
	echo "[+] Installing ..."
	cp armv6/libbearssl.a "$MUSL_PREFIX/lib"
	cp inc/* "$MUSL_PREFIX/include"
	popd
}

download_build_libcurl() {
	echo "[+] Downloading cURL ..."
	wget "$CURL_LINK" -O curl.tar.xz
	tar xvf curl.tar.xz

	echo "[+] Building cURL ..."
	cd curl*/
	mkdir build && cd build/
	../configure \
		--prefix="$MUSL_PREFIX" \
		--target=armv6-linux-musleabi \
		--host=armv6-linux-musleabi \
		--build=x86_64-linux-gnu \
		--with-bearssl \
		--disable-ipv6 \
		--disable-ftp \
		--disable-gopher \
		--disable-imap \
		--disable-ipfs \
		--disable-ipns \
		--disable-mqtt \
		--disable-pop3 \
		--disable-rtsp \
		--disable-smtp \
		--disable-telnet \
		--disable-tftp \
		--disable-hsts

	make -j$(nproc)
	make install
	popd
}

# This is slightly better than using the CI file
# because our env vars are already set!
build_alertik_armv6() {
	popd
	echo "[+] Building Alertik!"
	make CROSS=armv6
	echo "[+] File type:"
	file alertik
	echo "[+] File size:"
	ls -lah alertik
}

# Dispatcher
if [ "$1" == "download_musl_armv6" ]; then
	download_musl_armv6
elif [ "$1" == "download_build_bearssl" ]; then
	download_build_bearssl
elif [ "$1" == "download_build_libcurl" ]; then
	download_build_libcurl
elif [ "$1" == "build_alertik_armv6" ]; then
	build_alertik_armv6
else
	echo "No option found!"
	exit 1
fi
