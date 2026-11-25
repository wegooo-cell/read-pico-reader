#!/usr/bin/env python3
"""中文：隔离运行PICO新协议与S31旧协议的升级回归。/ English: Isolate Pico MindReset and legacy S31 OTA regressions."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys
from board_source_view import materialize

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cjson', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--replay-dir', type=Path)
    parser.add_argument('--reference-image', type=Path)
    parser.add_argument('--live', action='store_true')
    parser.add_argument('--ca-file', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = (args.output or root.parent / 'host-validation').resolve()
    cjson = args.cjson.resolve()
    results = []
    for board in ('pico', 'metalio47'):
        view = materialize(root, output / (board + '-view'), board, tests=True)
        binaries = output / board
        binaries.mkdir(parents=True, exist_ok=True)
        for test in ('ota_release', 'ota_online'):
            binary = binaries / test
            command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
                       '-fsanitize=address,undefined', '-DKIIKO_BOARD_METALIO47=' + ('1' if board == 'metalio47' else '0'),
                       '-Itools/ota_stubs', '-Imain', '-I' + str(cjson), 'tools/' + test + '_host_test.c',
                       'main/ota_release.c', str(cjson / 'cJSON.c'), '-lm', '-pthread', '-o', str(binary)]
            for action in (command, [str(binary)]):
                result = subprocess.run(action, cwd=view, capture_output=True, text=True)
                if result.returncode:
                    print(result.stdout, result.stderr)
                    raise SystemExit(result.returncode)
            results.append({'board': board, 'test': test, 'passed': True, 'output': result.stdout.strip()})
    (output / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(results, ensure_ascii=False))
    if args.replay_dir or args.live:
        if sys.platform != 'darwin' or not args.reference_image or (args.live and not args.ca_file):
            parser.error('Native full-image harness needs macOS, --reference-image, and --ca-file for --live')
        if args.replay_dir and args.live:
            parser.error('Choose recorded replay or live HTTPS explicitly')
        view = output / 'pico-view'
        binary = output / 'pico/ota_mindreset_live'
        command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
                   '-Wno-unused-function', '-Wno-unused-variable', '-fsanitize=address,undefined',
                   '-DKIIKO_BOARD_METALIO47=0', '-Itools/ota_stubs', '-Imain', '-I' + str(cjson),
                   'tools/ota_mindreset_live_host.c', 'main/ota_release.c', str(cjson / 'cJSON.c'),
                   '-lm', '-pthread', '-o', str(binary)]
        subprocess.run(command, cwd=view, check=True)
        environment = dict(os.environ)
        environment.pop('MINDRESET_HOST_REPLAY_DIR', None)
        environment.pop('MINDRESET_HOST_CA_FILE', None)
        if args.replay_dir:
            environment['MINDRESET_HOST_REPLAY_DIR'] = str(args.replay_dir.resolve())
        if args.ca_file:
            environment['MINDRESET_HOST_CA_FILE'] = str(args.ca_file.resolve())
        result = subprocess.run([str(binary), str(args.reference_image.resolve())], env=environment,
                                capture_output=True, text=True)
        if result.returncode:
            print(result.stdout, result.stderr)
            raise SystemExit(result.returncode)
        record = json.loads(result.stdout)
        name = 'live-service.json' if args.live else 'recorded-service-replay.json'
        (output / name).write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps(record, ensure_ascii=False))

if __name__ == '__main__':
    main()
