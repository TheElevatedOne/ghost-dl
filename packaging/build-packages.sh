#!/usr/bin/env bash
# Build Debian (.deb), RPM (.rpm), and pacman (.pkg.tar.zst) packages
# plus a generic Linux tarball from the C tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${VERSION:-$(sed -n 's/^VERSION ?= //p' "$ROOT/Makefile" | head -1)}"
RELEASE="${RELEASE:-1}"
DIST="${DIST:-"$ROOT/dist"}"
MAINTAINER="Adam Mladý <admin@elevated.ovh>"
URL="https://github.com/TheElevatedOne/ghost-dl"
DESC_SHORT="KHInsider game OST downloader"

HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
  x86_64|amd64)
    HOST_ARCH=x86_64
    DEB_ARCH=amd64
    RPM_ARCH=x86_64
    PKG_ARCH=x86_64
    ;;
  aarch64|arm64)
    HOST_ARCH=aarch64
    DEB_ARCH=arm64
    RPM_ARCH=aarch64
    PKG_ARCH=aarch64
    ;;
  *)
    DEB_ARCH="$HOST_ARCH"
    RPM_ARCH="$HOST_ARCH"
    PKG_ARCH="$HOST_ARCH"
    ;;
esac

usage() {
  echo "Usage: $0 [all|deb|rpm|pkg|tar]" >&2
  exit 2
}

need() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "ghost-dl: missing tool '$1' (needed to build $2)" >&2
    exit 1
  fi
}

stage_tree() {
  local dest="$1"
  rm -rf "$dest"
  make -C "$ROOT" DESTDIR="$dest" PREFIX=/usr install
  gzip -n -9 -f "$dest/usr/share/man/man1/ghost-dl.1"
  if command -v strip >/dev/null 2>&1; then
    strip --strip-unneeded "$dest/usr/bin/ghost-dl" 2>/dev/null || strip "$dest/usr/bin/ghost-dl"
  fi
}

build_tar() {
  local stage="$DIST/stage-tar"
  stage_tree "$stage"
  local name="ghost-dl-${VERSION}-linux-${PKG_ARCH}"
  mkdir -p "$stage/$name"
  cp "$stage/usr/bin/ghost-dl" "$stage/$name/ghost-dl"
  cp "$ROOT/LICENSE" "$stage/$name/LICENSE"
  cp "$ROOT/README.md" "$stage/$name/README.md"
  mkdir -p "$stage/$name/man"
  cp "$stage/usr/share/man/man1/ghost-dl.1.gz" "$stage/$name/man/"
  tar -C "$stage" -czf "$DIST/${name}.tar.gz" "$name"
  echo "wrote $DIST/${name}.tar.gz"
}

build_deb() {
  need dpkg-deb deb
  local stage="$DIST/stage-deb"
  stage_tree "$stage"
  mkdir -p "$stage/usr/share/doc/ghost-dl"
  cp "$ROOT/packaging/debian/copyright" "$stage/usr/share/doc/ghost-dl/copyright"
  rm -rf "$stage/usr/share/licenses"
  cat > "$stage/usr/share/doc/ghost-dl/changelog.Debian" <<EOF
ghost-dl (${VERSION}-${RELEASE}) unstable; urgency=medium

  * Native C package for Debian/Ubuntu.

 -- ${MAINTAINER}  $(date -u '+%a, %d %b %Y %H:%M:%S +0000')
EOF
  gzip -n -9 -f "$stage/usr/share/doc/ghost-dl/changelog.Debian"

  local size
  size="$(du -sk "$stage/usr" | awk '{print $1}')"
  mkdir -p "$stage/DEBIAN"
  sed \
    -e "s/@@VERSION@@/${VERSION}/g" \
    -e "s/@@ARCH@@/${DEB_ARCH}/g" \
    -e "s/@@SIZE@@/${size}/g" \
    "$ROOT/packaging/debian/control" > "$stage/DEBIAN/control"

  local out="$DIST/ghost-dl_${VERSION}-${RELEASE}_${DEB_ARCH}.deb"
  dpkg-deb --root-owner-group --build "$stage" "$out"
  echo "wrote $out"
}

build_rpm() {
  need rpmbuild rpm
  local stage="$DIST/stage-rpm"
  stage_tree "$stage"
  local top="$DIST/rpmbuild"
  rm -rf "$top"
  mkdir -p "$top"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
  rpmbuild -bb \
    --define "_topdir $top" \
    --define "_ghost_version $VERSION" \
    --define "_ghost_arch $RPM_ARCH" \
    --define "_ghost_stage $stage" \
    "$ROOT/packaging/rpm/ghost-dl.spec"
  find "$top/RPMS" -name '*.rpm' -exec cp -v {} "$DIST/" \;
}

build_pkg() {
  need tar pkg
  need zstd pkg
  local stage="$DIST/stage-pkg"
  local pkgdir="$DIST/pkgroot"
  rm -rf "$pkgdir"
  mkdir -p "$pkgdir"
  stage_tree "$stage"
  cp -a "$stage/usr" "$pkgdir/usr"

  local size
  size="$(du -sb "$pkgdir/usr" | awk '{print $1}')"
  cat > "$pkgdir/.PKGINFO" <<EOF
pkgname = ghost-dl
pkgbase = ghost-dl
pkgver = ${VERSION}-${RELEASE}
pkgdesc = ${DESC_SHORT}
url = ${URL}
builddate = $(date +%s)
packager = ${MAINTAINER}
size = ${size}
arch = ${PKG_ARCH}
license = GPL-3.0-or-later
depend = glibc
depend = curl
depend = ncurses
EOF

  if command -v bsdtar >/dev/null 2>&1; then
    (cd "$pkgdir" && bsdtar \
      --format=mtree \
      --options='!all,use-set,type,uid,gid,mode,time,size,sha256,link' \
      -c -f .MTREE .PKGINFO usr)
  fi

  local out="$DIST/ghost-dl-${VERSION}-${RELEASE}-${PKG_ARCH}.pkg.tar.zst"
  local wrap=()
  if command -v fakeroot >/dev/null 2>&1; then
    wrap=(fakeroot)
  fi
  (
    cd "$pkgdir"
    if [ -f .MTREE ]; then
      "${wrap[@]}" tar --zstd -cf "$out" .PKGINFO .MTREE usr
    else
      "${wrap[@]}" tar --zstd -cf "$out" .PKGINFO usr
    fi
  )
  echo "wrote $out"
}

mkdir -p "$DIST"
make -C "$ROOT" -j"$(nproc 2>/dev/null || echo 4)" VERSION="$VERSION"

targets="${1:-all}"
case "$targets" in
  all)
    build_tar
    build_deb
    build_rpm
    build_pkg
    ;;
  tar) build_tar ;;
  deb) build_deb ;;
  rpm) build_rpm ;;
  pkg) build_pkg ;;
  *) usage ;;
esac

echo
echo "packages in $DIST:"
ls -lh "$DIST"/*.{deb,rpm,zst,gz} 2>/dev/null || ls -lh "$DIST"
