#!/bin/bash
set -euo pipefail

OUTPUT="${1:?usage: embed_shaders.sh OUTPUT}"

emit_shader() {
    local symbol="$1"
    local source="$2"
    printf 'const unsigned char %s[] = {' "$symbol"
    od -An -v -t u1 "$source" | awk '{ for (i = 1; i <= NF; i++) printf "%s,", $i }'
    printf '0};\n'
}

{
    printf '#include "shader_sources.h"\n\n'
    emit_shader shader_backdrop_fs shaders/backdrop.fs
    emit_shader shader_craft_fs shaders/craft.fs
    emit_shader shader_craft_vs shaders/craft.vs
    emit_shader shader_post_fs shaders/post.fs
    emit_shader shader_terrain_fs shaders/terrain.fs
    emit_shader shader_terrain_vs shaders/terrain.vs
    emit_shader shader_tower_fs shaders/tower.fs
    emit_shader shader_tower_vs shaders/tower.vs
} > "$OUTPUT"