#!/usr/bin/env python3
"""Real CAKE/kernel integration; MUST run inside a new network namespace."""
import json, os, pathlib, subprocess, sys
binary = str(pathlib.Path(sys.argv[1]).resolve())
if os.geteuid() != 0 or os.readlink('/proc/self/ns/net') == os.environ.get('CAKE_TEST_PARENT_NETNS', os.readlink('/proc/self/ns/net')):
    raise SystemExit('Refusing to create test devices outside an isolated network namespace')

def command(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout

def snapshot(names, ok=True):
    r = subprocess.run([binary, *names], capture_output=True, text=True, timeout=3)
    if not ok:
        assert r.returncode != 0 and not r.stdout, r
        return
    assert r.returncode == 0, r.stderr
    data = json.loads(r.stdout)
    assert data['schema_version'] == 1 and 0 <= data['duration_ms'] < 750
    assert [r['dev'] for r in data['rates']] == names
    for row in data['rates']:
        kernel = json.loads(command('tc', '-j', 'qdisc', 'show', 'dev', row['dev']))[0]
        assert kernel['kind'] == 'cake' and kernel['root']
        rate = kernel['options']['bandwidth']
        assert int(row['bytes_per_second']) == rate
        assert int(row['bits_per_second']) == rate * 8
    return data

names = ['cswan', 'ifb4pppoe-wan', 'vlan.123', 'quote"dev', 'slash\\dev']
names += [f'cs{i}' for i in range(64-len(names))]
for i, name in enumerate(names):
    command('ip', 'link', 'add', 'name', name, 'type', 'dummy')
    command('tc', 'qdisc', 'add', 'dev', name, 'root', 'cake', 'bandwidth', f'{1234567 + i * 100000003}bit')
before = command('tc', '-j', 'qdisc', 'show')
for count in [1, 2, 5, 10, 12, 64]:
    snapshot(names[:count])
assert command('tc', '-j', 'qdisc', 'show') == before, 'reader changed qdiscs'
snapshot(['lo'], False)
snapshot([names[0], names[0]], False)
command('tc', 'qdisc', 'replace', 'dev', names[1], 'root', 'fq_codel')
snapshot(names[:2], False)
command('tc', 'qdisc', 'replace', 'dev', names[1], 'root', 'cake', 'unlimited')
snapshot(names[:2], False)
command('tc', 'qdisc', 'replace', 'dev', names[1], 'root', 'cake', 'bandwidth', '1kbit')
snapshot(names[:2])
print(json.dumps({'passed': True, 'realKernel': True, 'isolatedNetworkNamespace': True,
                  'devices': 64, 'successfulSnapshots': 7, 'negativeCases': 4,
                  'exactKernelRates': True, 'readOnlySnapshot': True}))
