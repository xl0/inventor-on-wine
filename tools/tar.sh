#!/bin/bash
# Add Windows' inbox tar.exe (bsdtar, in Windows since 10 1803) to $WINEPREFIX: Autodesk's
# Electrical Catalog CA runs `tar -xf ZIP -C OUT` (issue 050). Win11 ships bsdtar 3.8.8;
# libarchive publishes source only, so build it static with MinGW (zlib only) into deps/.
#   WINEPREFIX=... tools/tar.sh      (x86_64 -> system32, i686 -> syswow64)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd) D=$ROOT/deps
: "${WINEPREFIX:?}"

# sha256 = GitHub release asset digest / zlib.net
LA=libarchive-3.8.8 LA_SHA=3873a88801da067d0528a989af06877710529d50ee8fe6f3970cbb4302efb918
ZL=zlib-1.3.2 ZL_SHA=d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3

for arch in x86_64 i686; do
	exe=$D/bsdtar-3.8.8/$arch/bsdtar.exe
	[ -f "$exe" ] && continue
	[ -f "$D/$LA.tar.xz" ] || curl -fL -o "$D/$LA.tar.xz" https://github.com/libarchive/libarchive/releases/download/v3.8.8/$LA.tar.xz
	[ -f "$D/$ZL.tar.xz" ] || curl -fL -o "$D/$ZL.tar.xz" https://zlib.net/$ZL.tar.xz
	echo "$LA_SHA  $D/$LA.tar.xz" | sha256sum -c --quiet
	echo "$ZL_SHA  $D/$ZL.tar.xz" | sha256sum -c --quiet
	b=$D/bsdtar-3.8.8/build-$arch; rm -rf "$b"; mkdir -p "$b"
	tar -xf "$D/$LA.tar.xz" -C "$b"; tar -xf "$D/$ZL.tar.xz" -C "$b"
	make -C "$b/$ZL" -f win32/Makefile.gcc PREFIX=$arch-w64-mingw32- libz.a -j40
	(cd "$b/$LA" && ./configure --host=$arch-w64-mingw32 --disable-shared --enable-static \
		--disable-bsdcpio --disable-bsdcat --disable-bsdunzip --disable-acl --disable-xattr \
		--without-bz2lib --without-lzma --without-zstd --without-lz4 --without-libb2 \
		--without-iconv --without-xml2 --without-expat --without-openssl --without-mbedtls \
		--without-nettle CPPFLAGS="-I$b/$ZL" LDFLAGS="-L$b/$ZL -static" \
		&& make -j40 bsdtar.exe)
	mkdir -p "${exe%/*}"; cp "$b/$LA/bsdtar.exe" "$exe"; $arch-w64-mingw32-strip "$exe"
	rm -rf "$b"
done
cp "$D/bsdtar-3.8.8/x86_64/bsdtar.exe" "$WINEPREFIX/drive_c/windows/system32/tar.exe"
cp "$D/bsdtar-3.8.8/i686/bsdtar.exe" "$WINEPREFIX/drive_c/windows/syswow64/tar.exe"
