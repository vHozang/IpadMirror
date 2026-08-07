#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
tools_root=${PADMIRROR_TOOLS_ROOT:-/d/PadMirrorTools}
source_dir="$tools_root/uxplay-src"
build_dir="$source_dir/build-ucrt64"
bundle_dir="$tools_root/uxplay-bundle"
runtime_root=/ucrt64
revision=8fa09eaec76a64726fd21becad5dcc82a4c0851c
patch_file="$repo_root/third_party/uxplay/PadMirror-mDNS-interface.patch"

if [[ ! -d "$source_dir/.git" ]]; then
  git clone https://github.com/FDH2/UxPlay.git "$source_dir"
fi
current_revision=$(git -C "$source_dir" rev-parse HEAD)
if [[ "$current_revision" != "$revision" ]]; then
  if [[ -n "$(git -C "$source_dir" -c core.filemode=false status --short --untracked-files=no)" ]]; then
    echo "UxPlay source has local tracked changes: $source_dir" >&2
    exit 1
  fi
  git -C "$source_dir" fetch origin "$revision"
  git -C "$source_dir" checkout --detach "$revision"
fi
if git -C "$source_dir" apply --reverse --check "$patch_file" 2>/dev/null; then
  :
elif git -C "$source_dir" apply --check "$patch_file"; then
  git -C "$source_dir" apply "$patch_file"
else
  echo "Cannot apply the PadMirror UxPlay mDNS patch" >&2
  exit 1
fi

cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DNO_MARCH_NATIVE=ON
cmake --build "$build_dir" --parallel

if [[ -d "$bundle_dir" ]]; then
  rm -rf -- "$bundle_dir"
fi
mkdir -p "$bundle_dir/gstreamer-1.0" \
  "$bundle_dir/libexec/gstreamer-1.0" \
  "$bundle_dir/licenses/UxPlay/llhttp"

cp "$build_dir/uxplay.exe" "$bundle_dir/uxplay.exe"

plugins=(
  libgstapp.dll
  libgstautodetect.dll
  libgstaudioconvert.dll
  libgstaudioresample.dll
  libgstcoreelements.dll
  libgstlibav.dll
  libgstplayback.dll
  libgstrtp.dll
  libgsttypefindfunctions.dll
  libgstudp.dll
  libgstvideoparsersbad.dll
  libgstvolume.dll
)
for plugin in "${plugins[@]}"; do
  cp "$runtime_root/lib/gstreamer-1.0/$plugin" "$bundle_dir/gstreamer-1.0/$plugin"
done

cp "$runtime_root/libexec/gstreamer-1.0/gst-plugin-scanner.exe" \
  "$bundle_dir/libexec/gstreamer-1.0/gst-plugin-scanner.exe"
cp "$source_dir/LICENSE" "$bundle_dir/licenses/UxPlay/LICENSE-GPLv3.txt"
cp "$source_dir/README.md" "$bundle_dir/licenses/UxPlay/README.md"
cp "$source_dir/lib/llhttp/LICENSE-MIT" "$bundle_dir/licenses/UxPlay/llhttp/LICENSE-MIT.txt"
cp "$repo_root/third_party/uxplay/SOURCE_NOTICE.txt" \
  "$bundle_dir/licenses/UxPlay/SOURCE_NOTICE.txt"

source_snapshot=$(mktemp -d)
trap 'rm -rf -- "$source_snapshot"' EXIT
git -C "$source_dir" archive HEAD | tar -x -C "$source_snapshot"
cp "$source_dir/lib/mdnsd/mdnsd.c" "$source_snapshot/lib/mdnsd/mdnsd.c"
(
  cd "$source_snapshot"
  cmake -E tar cf "$bundle_dir/licenses/UxPlay/UxPlay-1.74-source.zip" \
    --format=zip -- .
)

declare -A runtime_dlls=()
for runtime_dll in "$runtime_root/bin/"*.dll; do
  runtime_dlls["$(basename "$runtime_dll" | tr '[:upper:]' '[:lower:]')"]=$runtime_dll
done

declare -A queued=()
queue=(
  "$bundle_dir/uxplay.exe"
  "$bundle_dir/libexec/gstreamer-1.0/gst-plugin-scanner.exe"
  "$bundle_dir/gstreamer-1.0/"*.dll
)
for file in "${queue[@]}"; do
  queued["${file,,}"]=1
done

index=0
while ((index < ${#queue[@]})); do
  file=${queue[$index]}
  ((index += 1))
  while IFS= read -r dependency; do
    [[ -z "$dependency" ]] && continue
    dependency_path=${runtime_dlls["${dependency,,}"]:-}
    [[ -z "$dependency_path" ]] && continue
    target="$bundle_dir/$(basename "$dependency_path")"
    if [[ ! -f "$target" ]]; then
      cp "$dependency_path" "$target"
    fi
    key=${target,,}
    if [[ -z "${queued[$key]:-}" ]]; then
      queued[$key]=1
      queue+=("$target")
    fi
  done < <(objdump -p "$file" 2>/dev/null | sed -n 's/.*DLL Name: //p')
done

strip "$bundle_dir/uxplay.exe"
printf 'UxPlay bundle: %s files, %s\n' \
  "$(find "$bundle_dir" -type f | wc -l)" \
  "$(du -sh "$bundle_dir" | cut -f1)"
