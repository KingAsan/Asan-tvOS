#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ASAN_TORRENT_BUILD_ROOT:-$ROOT/.torrent-build}"
OUT="$ROOT/Vendor/AsanTorrent.xcframework"
LT_REV=1eb18faeae156d8dbbab42935c082f8b81f50989
BOOST_SHA=7009fe1faa1697476bdc7027703a2badb84e849b7b0baad5086b087b971f8617
[[ "$(uname -s)" == Darwin ]] || { echo 'Requires macOS and Xcode Apple SDKs' >&2; exit 1; }
for tool in git curl cmake xcrun xcodebuild libtool shasum; do command -v "$tool" >/dev/null; done
mkdir -p "$BUILD/src" "$BUILD/archives" "$ROOT/Vendor"
if [[ ! -d "$BUILD/src/libtorrent/.git" ]]; then
  git clone --depth 1 --branch v2.0.15 --recurse-submodules https://github.com/arvidn/libtorrent.git "$BUILD/src/libtorrent"
fi
[[ "$(git -C "$BUILD/src/libtorrent" rev-parse HEAD)" == "$LT_REV" ]] || { echo 'libtorrent pin mismatch' >&2; exit 1; }
[[ -z "$(git -C "$BUILD/src/libtorrent" status --porcelain)" ]] || { echo 'libtorrent sources must be clean' >&2; exit 1; }
BOOST_ARCHIVE="$BUILD/archives/boost_1_85_0.tar.bz2"
if [[ ! -f "$BOOST_ARCHIVE" ]]; then
  curl --fail --location --retry 3 --proto '=https' --tlsv1.2 https://archives.boost.io/release/1.85.0/source/boost_1_85_0.tar.bz2 -o "$BOOST_ARCHIVE"
fi
echo "$BOOST_SHA  $BOOST_ARCHIVE" | shasum -a 256 -c -
if [[ ! -f "$BUILD/src/boost_1_85_0/boost/version.hpp" ]]; then
  tar -xjf "$BOOST_ARCHIVE" -C "$BUILD/src"
fi
grep -q '^#define BOOST_VERSION 108500' "$BUILD/src/boost_1_85_0/boost/version.hpp"
declare -a LIB_ARGS=()
build_slice() {
  local sdk="$1" arch="$2" system="$3" floor="$4" identifier="$5"
  local dir="$BUILD/$identifier" stage="$BUILD/stage/$identifier"
  mkdir -p "$stage/Headers"
  cmake -S "$ROOT/Native/TorrentBridge" -B "$dir" -G Xcode \
    -DCMAKE_SYSTEM_NAME="$system" \
    -DCMAKE_OSX_SYSROOT="$(xcrun --sdk "$sdk" --show-sdk-path)" \
    -DCMAKE_OSX_ARCHITECTURES="$arch" -DCMAKE_OSX_DEPLOYMENT_TARGET="$floor" \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DASAN_LIBTORRENT_SOURCE="$BUILD/src/libtorrent" \
    -DASAN_BOOST_SOURCE="$BUILD/src/boost_1_85_0"
  cmake --build "$dir" --config Release --target AsanTorrentBridge -- -quiet CODE_SIGNING_ALLOWED=NO
  local bridge torrent
  bridge="$(find "$dir" -type f -name libAsanTorrentBridge.a -path '*Release*' -print -quit)"
  torrent="$(find "$dir" -type f -name libtorrent-rasterbar.a -path '*Release*' -print -quit)"
  [[ -n "$bridge" && -n "$torrent" ]] || { echo "Missing native archives for $identifier" >&2; exit 1; }
  libtool -static -o "$stage/libAsanTorrent.a" "$bridge" "$torrent"
  cp "$ROOT/Native/TorrentBridge/include/AsanTorrent.h" "$stage/Headers/"
  printf 'module AsanTorrent { header "AsanTorrent.h" export * }\n' > "$stage/Headers/module.modulemap"
  LIB_ARGS+=(-library "$stage/libAsanTorrent.a" -headers "$stage/Headers")
}
build_slice appletvos arm64 tvOS 17.0 tvos-arm64
build_slice appletvsimulator x86_64 tvOS 17.0 tvos-x86_64-simulator
build_slice macosx x86_64 Darwin 13.0 macos-x86_64
# Replacing only this generated XCFramework; source, app data, other Vendor files are untouched.
if [[ -e "$OUT" ]]; then
  [[ "$OUT" == "$ROOT/Vendor/AsanTorrent.xcframework" ]] || exit 1
  rm -rf -- "$OUT"
fi
xcodebuild -create-xcframework "${LIB_ARGS[@]}" -output "$OUT"
mkdir -p "$OUT/Licenses"
cp "$BUILD/src/libtorrent/COPYING" "$OUT/Licenses/libtorrent-BSD-3-Clause.txt"
cp "$BUILD/src/boost_1_85_0/LICENSE_1_0.txt" "$OUT/Licenses/Boost-License-1.0.txt"
cat > "$OUT/BUILD-PROVENANCE.txt" <<EOF
libtorrent v2.0.15 $LT_REV
Boost 1.85.0 source SHA256 $BOOST_SHA
Source builds: tvOS17 arm64; tvOS17 simulator x86_64; macOS13 x86_64
No writable mmap. CommonCrypto SHA. No OpenSSL/GnuTLS, HTTPS trackers or protocol encryption.
EOF
echo "Built $OUT"
