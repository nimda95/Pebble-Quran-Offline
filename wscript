# Pebble Quran build script.
import os
import subprocess
import sys

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')


def build(ctx):
    ctx.load('pebble_sdk')
    # Embed the renderer data into the phone-side JS (and stamp a shared data
    # version into a C header) before the JS bundle is assembled.
    root = ctx.path.abspath()
    subprocess.check_call([sys.executable, os.path.join(root, 'tools', 'gen_js_data.py')])
    # UI translations: lang/*.json -> LANG.bin resource, C string ids, page strings.
    subprocess.check_call([sys.executable, os.path.join(root, 'tools', 'gen_lang.py')])

    binaries = []
    for p in ctx.env.TARGET_PLATFORMS:
        ctx.set_env(ctx.all_envs[p])
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_program(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf)
        binaries.append({'platform': p, 'app_elf': app_elf})

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js', 'src/pkjs/**/*.json']),
                   js_entry_file='src/pkjs/index.js')
