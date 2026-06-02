bin/fetch-ninja

export PATH="$PWD/third_party/ninja:$PATH"
ulimit -n 8192

python3 tools/git-sync-deps

COMMON_ARGS='
    is_official_build=true
    is_component_build=false
    is_debug=false
    skia_enable_optimize_size=true
    skia_enable_tools=true
    skia_use_system_expat=false
    skia_use_system_icu=false
    skia_use_system_harfbuzz=false
    skia_use_system_libjpeg_turbo=false
    skia_use_system_libpng=false
    skia_use_system_libwebp=false
    skia_use_system_zlib=false
    skia_use_system_freetype2=false
    skia_use_dng_sdk=false
    skia_use_harfbuzz=true
    skia_use_icu=false
    skia_use_icu4x=false
    skia_use_libgrapheme=true
    skia_use_fontconfig=false
    skia_use_gl=false
    skia_use_zlib=true
    skia_use_libjpeg_turbo_decode=true
    skia_use_libjpeg_turbo_encode=true
    skia_use_libpng_encode=true
    skia_use_libpng_decode=true
    skia_use_libwebp_encode=true
    skia_use_libwebp_decode=true
    skia_enable_android_utils=false
    skia_enable_spirv_validation=false
    skia_enable_gpu=false
    skia_enable_gpu_debug_layers=false
    skia_enable_fontmgr_custom_directory=true
    skia_use_freetype=true
    skia_use_jpeg_gainmaps=false
    skia_use_libheif=false
    skia_use_lua=false
    skia_enable_svg=true
    skia_use_expat=true
    skia_enable_skshaper=true
    skia_enable_skunicode=true
    skia_pdf_subset_harfbuzz=true
    skia_enable_pdf=true
    skia_compile_modules=false
    skia_enable_ganesh=false
    skia_lex=false
    skia_use_fonthost_mac=true
    extra_cflags=["-fPIC", "-fno-rtti"]'

mkdir -p out/release
bin/gn gen out/release --args="$COMMON_ARGS"
ninja -C out/release skia svg skparagraph skresources
