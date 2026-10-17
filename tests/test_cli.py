#!/usr/bin/env python3
"""No privileges, interface creation or live traffic required."""
import json, subprocess, sys, unittest
BINARY = sys.argv.pop(1)

class CLI(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([BINARY, *args], capture_output=True, text=True, timeout=3)

    def test_help(self):
        r = self.run_cli('--help')
        self.assertEqual(r.returncode, 0)
        self.assertIn('read-only', r.stdout)

    def test_invalid_arguments(self):
        for args in [[], ['--unknown'], [''], ['bad/name'], ['..'], ['a'*16],
                     ['--timeout-ms'], ['--timeout-ms', '0', 'lo'],
                     ['--timeout-ms', '60001', 'lo'], ['--timeout-ms', '5x', 'lo'],
                     ['--timeout-ms', '', 'lo'], ['lo']*65]:
            with self.subTest(args=args):
                r = self.run_cli(*args)
                self.assertEqual(r.returncode, 2)
                self.assertEqual(r.stdout, '')

    def test_incomplete_is_not_a_snapshot(self):
        for args in [('lo',), ('lo', 'lo'), ('cs-nonexistent',), ('--', '-cs-none')]:
            r = self.run_cli(*args)
            self.assertNotEqual(r.returncode, 0)
            self.assertEqual(r.stdout, '')
            self.assertIn('incomplete', r.stderr)

if __name__ == '__main__':
    unittest.main()
