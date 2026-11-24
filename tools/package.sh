# Sourced by the macOS build.sh; no dependency downloads or SDK files in archives.
audit_runtime() {
  local binary="$1" metadata dependency
  command -v otool >/dev/null || fail "otool is required to audit packages"
  lipo "$binary" -verify_arch "$arch" || fail "binary does not match package architecture: $binary"
  metadata="$(otool -arch "$arch" -L "$binary")"
  while IFS= read -r dependency; do
    case "$dependency" in /System/Library/*|/usr/lib/*) ;; *) fail "non-system dependency escapes package: $dependency ($binary)" ;; esac
  done < <(printf '%s\n' "$metadata" | sed '1d; s/^[[:space:]]*//; s/ (compatibility.*//')
}
install_runtime() {
  cmake --install "$build_dir" --config "$config" --prefix "$1" --component runtime
}
install_clippi() {
  local prefix="$1"
  cmake --install "$build_dir" --config "$config" --prefix "$prefix" --component sdk
  if ((core == 0)); then install_runtime "$prefix"; fi
  echo "Installed: $prefix"
}
package_clippi() {
  local version name stage prefix archive hash
  version="$(tr -d '\r\n' < "$root/VERSION")"
  [[ "$version" =~ ^[0-9A-Za-z.+-]+$ ]] || fail "invalid VERSION for package filename"
  name="clippi-$version-$os-$arch"
  stage="$build_dir/package-$$"
  mkdir "$stage"; owned_dirs+=("$stage")
  prefix="$stage/$name"
  install_runtime "$prefix"
  audit_runtime "$prefix/bin/clippi"
  mkdir -p "$build_dir/packages"
  archive="$build_dir/packages/$name.tar.gz"
  pending_files+=("$archive.$$.new" "$archive.sha256.$$.new")
  tar -czf "$archive.$$.new" -C "$stage" "$name"
  mv -f "$archive.$$.new" "$archive"
  hash="$(shasum -a 256 "$archive" | awk '{print $1}')"
  printf '%s  %s.tar.gz\n' "$hash" "$name" > "$archive.sha256.$$.new"
  mv -f "$archive.sha256.$$.new" "$archive.sha256"
  echo "Archive: $archive"
}
