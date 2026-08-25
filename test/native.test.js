'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

test('standalone C++ reader and writer invariants', () => {
  const executable = path.resolve(
    __dirname,
    '..',
    'build',
    'Release',
    process.platform === 'win32'
      ? 'v8serial_native_test.exe'
      : 'v8serial_native_test',
  );
  const result = spawnSync(executable, [], { encoding: 'utf8' });
  assert.equal(
    result.status,
    0,
    `native test failed\nstdout: ${result.stdout}\nstderr: ${result.stderr}`,
  );
});
