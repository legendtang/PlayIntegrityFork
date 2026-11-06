#!/usr/bin/env python3
"""Summarize value-free property traces offline; never infer a server verdict."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

LINE = re.compile(
    r'^\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+\s+(?P<pid>\d+)\s+\d+\s+[VDIWEF]\s+'
    r'PIF/PropertyAudit:\s+api=(?P<api>callback|get|read) key=(?P<key>\S+) '
    r'mapped=(?P<mapped>[01]) caller=(?P<caller>\S(?:[^\r\n]*?\S)?) offset=(?P<offset>0x[0-9a-fA-F]+) '
    r'resolved=(?P<resolved>[01])$'
)


def parse(text):
    events = []
    malformed = 0
    for line in text.splitlines():
        if 'PIF/PropertyAudit:' not in line:
            continue
        match = LINE.fullmatch(line.strip())
        if not match:
            malformed += 1
            continue
        event = match.groupdict()
        event['pid'] = int(event['pid'])
        event['mapped'] = event['mapped'] == '1'
        event['resolved'] = event['resolved'] == '1'
        event['offset'] = hex(int(event['offset'], 16))
        events.append(event)
    return events, malformed


def call_site(event):
    # PID is deliberately separate: the same site in a restarted process is
    # not newly discovered detector code, but it is a newly observed process.
    return event['api'], event['key'], event['caller'], event['offset'], event['resolved']


def summarize(events, baseline=()):
    unique = {}
    for event in events:
        unique[(event['pid'], *call_site(event), event['mapped'])] = event
    rows = sorted(unique.values(), key=lambda e: (e['pid'], *call_site(e), e['mapped']))
    known_sites = {call_site(e) for e in baseline}
    known_processes = {e['pid'] for e in baseline}
    return {
        'records': len(events),
        'distinct_process_call_sites': len(rows),
        'duplicate_records': len(events) - len(rows),
        'by_pid': dict(sorted(Counter(e['pid'] for e in rows).items())),
        'by_immediate_caller': dict(sorted(Counter(e['caller'] for e in rows).items())),
        'unresolved_process_call_sites': sum(not e['resolved'] for e in rows),
        'new_processes_vs_baseline': sorted({e['pid'] for e in rows} - known_processes),
        'new_call_sites_vs_baseline': [e for e in rows if call_site(e) not in known_sites],
        'events': rows,
        'interpretation_limits': [
            'Immediate native caller attribution is not a full Java/native call chain.',
            'A read or mapping match does not prove it caused a server rejection.',
            'No record does not prove absence of reads: capture and per-process audit budgets are bounded.',
            'PID reuse across boots cannot be distinguished without external process/boot metadata.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    events, malformed = parse(args.log.read_text(errors='replace'))
    if not events:
        parser.error('No complete attributed property audit records found')
    baseline, baseline_malformed = parse(args.baseline.read_text(errors='replace')) if args.baseline else ([], 0)
    report = summarize(events, baseline)
    report['malformed_audit_lines'] = malformed
    report['baseline_malformed_audit_lines'] = baseline_malformed
    payload = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(payload)
    else:
        print(payload, end='')


if __name__ == '__main__':
    main()
