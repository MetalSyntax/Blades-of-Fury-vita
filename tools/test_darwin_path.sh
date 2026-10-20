#!/bin/sh
# Host test of darwin_path() (source/darwin.c): relative paths against the working directory,
# "dir/../" collapsing and Documents -> saves/.
# Uso: sh tools/test_darwin_path.sh
set -e
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
cd "$(dirname "$0")/.."
{
    printf '#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n'
    printf '#define DATA_PATH "ux0:data/bladesoffury/"\n#define DOCS_PATH DATA_PATH "saves/"\n'
    printf '#define BUNDLE_PATH DATA_PATH "BladesOfFury.app/"\n'
    sed -n '/^static char cwd\[/,/^}/p' source/darwin.c
    sed -n '/^const char \*darwin_path/,/^}/p' source/darwin.c
    cat <<'C'
static const char *t(const char *in) { static char o[512]; return darwin_path(in, o, sizeof(o)); }
int main(void) {
    assert(!strcmp(t("ux0:data/bladesoffury/BladesOfFury.app/../Documents/options.sav"),
                   "ux0:data/bladesoffury/saves/options.sav"));
    assert(!strcmp(t("ux0:data/bladesoffury/saves/profile.sav"), "ux0:data/bladesoffury/saves/profile.sav"));
    assert(!strcmp(t("/var/mobile/x.dat"), "ux0:data/bladesoffury/var/mobile/x.dat"));
    assert(!strcmp(t("ux0:data/bladesoffury/Documents"), "ux0:data/bladesoffury/saves/"));
    assert(!strcmp(t("ux0:data/bladesoffury/BladesOfFury.app/textures"),
                   "ux0:data/bladesoffury/BladesOfFury.app/textures"));
    assert(!strcmp(t("textures"), "ux0:data/bladesoffury/BladesOfFury.app/textures"));
    assert(!strcmp(t("./Tutorial.bin"), "ux0:data/bladesoffury/BladesOfFury.app/Tutorial.bin"));
    assert(!strcmp(t("../Documents/progress.sav"), "ux0:data/bladesoffury/saves/progress.sav"));
    puts("darwin_path ok");
    return 0;
}
C
} > "$D/t.c"
cc -o "$D/t" "$D/t.c" && "$D/t"
