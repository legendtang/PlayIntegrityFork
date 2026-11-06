import runpy
from pathlib import Path
import unittest

MODULE = runpy.run_path(str(Path(__file__).resolve().parents[1]/'tools/summarize-property-audit.py'))
parse, summarize = MODULE['parse'], MODULE['summarize']


def line(pid=101, api='callback', key='ro.build.type', caller='libfixture.so', offset='0x012a', resolved=1):
    return (f'11-06 10:00:00.100 {pid:5d}   200 D PIF/PropertyAudit: '
            f'api={api} key={key} mapped=1 caller={caller} offset={offset} resolved={resolved}')


class PropertyAuditTests(unittest.TestCase):
    def test_repeated_capture_rows_do_not_become_new_sites(self):
        events, malformed = parse(line()+'\n'+line(offset='0x12A'))
        report = summarize(events, events[:1])
        self.assertEqual(malformed, 0)
        self.assertEqual(report['duplicate_records'], 1)
        self.assertEqual(report['new_call_sites_vs_baseline'], [])

    def test_process_restart_is_separate_from_code_discovery(self):
        before, _ = parse(line())
        after, _ = parse(line(pid=202))
        report = summarize(after, before)
        self.assertEqual(report['new_processes_vs_baseline'], [202])
        self.assertEqual(report['new_call_sites_vs_baseline'], [])

    def test_distinct_native_callers_are_not_merged(self):
        before, _ = parse(line(caller='libframework-fixture.so'))
        after, _ = parse(line(caller='libother-fixture.so'))
        self.assertEqual(len(summarize(after, before)['new_call_sites_vs_baseline']), 1)

    def test_incomplete_and_unrelated_lines_are_not_treated_as_evidence(self):
        events, malformed = parse('some unrelated log\nPIF/PropertyAudit: api=get key=partial\n'+line())
        self.assertEqual(len(events), 1)
        self.assertEqual(malformed, 1)

    def test_unresolved_callers_remain_explicit(self):
        events, _ = parse(line(caller='unresolved', offset='0x0', resolved=0))
        self.assertEqual(summarize(events)['unresolved_process_call_sites'], 1)
        self.assertFalse(summarize(events)['events'][0]['resolved'])

    def test_deleted_memfd_module_name_keeps_spaces(self):
        caller = 'memfd:frida-agent-64.so (deleted)'
        events, malformed = parse(line(caller=caller))
        self.assertEqual(malformed, 0)
        self.assertEqual(events[0]['caller'], caller)
        self.assertEqual(summarize(events)['by_immediate_caller'], {caller: 1})


if __name__ == '__main__':
    unittest.main()
