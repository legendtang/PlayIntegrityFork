#!/usr/bin/env python3
"""Run isolated native hook tests over adb; never call Play Integrity."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--abi', action='append', choices=['x86', 'x86_64'])
    args = parser.parse_args()
    adb = ['adb', '-s', args.serial]
    for abi in args.abi or ['x86', 'x86_64']:
        binary = ROOT/'build/native-maintenance'/abi/'property_hook_test'
        if not binary.is_file():
            parser.error('Build the native test first: ' + str(binary))
        remote = '/data/local/tmp/pif-property-hook-test-' + abi
        subprocess.run(adb + ['push', str(binary), remote], check=True)
        subprocess.run(adb + ['shell', 'chmod', '755', remote], check=True)
        result = subprocess.run(adb + ['shell', remote], capture_output=True, text=True, timeout=30)
        print(abi + ': ' + result.stdout.strip())
        if result.returncode != 0 or 'callbacks=24003 failures=0 global_property_unchanged=true legacy_apis_consistent=true' not in result.stdout:
            raise RuntimeError('Native hook test failed: ' + result.stderr)
        audit = subprocess.run(adb + ['shell', remote, '--audit-only'], capture_output=True, text=True, timeout=30)
        print(abi + ': ' + audit.stdout.strip())
        if audit.returncode != 0 or 'audit_only=true failures=0 property_values_unchanged=true audit_events=3' not in audit.stdout:
            raise RuntimeError('Audit-only hook test failed: ' + audit.stderr)


if __name__ == '__main__':
    main()
