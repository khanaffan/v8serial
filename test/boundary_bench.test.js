'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const test = require('node:test');
const v8 = require('node:v8');

const addon = require(path.join(
  __dirname,
  '..',
  'build',
  'Release',
  'v8serial_bench_napi.node',
));

const rows = [
  [1, 'alpha', 1.5, true, null],
  [2, 'beta', -2.25, false, 7],
];

function sinkAfter(operation) {
  addon.resetSink();
  operation();
  return addon.getSink();
}

test('boundary benchmark arms consume equivalent positional rows', () => {
  const perCell = sinkAfter(() => {
    for (const row of rows) {
      for (const value of row) addon.consumeCell(value);
      addon.consumeStep();
    }
  });
  const perRow = sinkAfter(() => {
    for (const row of rows) {
      addon.consumeArgs(...row);
      addon.consumeStep();
    }
  });
  const generic = sinkAfter(() => addon.consumeRowsGeneric(rows));
  const optimized = sinkAfter(() => addon.consumeRowsOptimized(rows));

  assert.equal(perRow, perCell);
  assert.equal(generic, perCell);
  assert.equal(optimized, perCell);
});

test('streaming copy and borrow arms consume the same serialized rows', () => {
  const serialized = v8.serialize(rows);
  const borrowed = sinkAfter(() => addon.consumeSerializedRows(serialized));
  const copied = sinkAfter(() =>
    addon.consumeSerializedRowsCopied(serialized),
  );

  assert.notEqual(borrowed, 0n);
  assert.equal(copied, borrowed);
  assert.notEqual(
    sinkAfter(() => addon.consumeSerializedTree(serialized)),
    0n,
  );
});

test('object and native benchmark arms consume data', () => {
  const objectRows = rows.map((row) =>
    Object.fromEntries(row.map((value, index) => [`c${index}`, value])),
  );
  assert.notEqual(
    sinkAfter(() => addon.consumeObjectRows(objectRows)),
    0n,
  );
  assert.notEqual(
    sinkAfter(() => addon.consumeNativeRows(rows.length, rows[0].length)),
    0n,
  );
});

test('streaming rows reject inconsistent widths and nested values', () => {
  assert.throws(
    () => addon.consumeSerializedRows(v8.serialize([[1], [2, 3]])),
    /unexpected column count/,
  );
  assert.throws(
    () => addon.consumeSerializedRows(v8.serialize([[{ value: 1 }]])),
    /primitive scalar values/,
  );
  const invalidUtf8 = Buffer.from([
    0xff,
    0x0f,
    0x41,
    0x01,
    0x41,
    0x01,
    0x53,
    0x01,
    0xff,
    0x24,
    0x00,
    0x01,
    0x24,
    0x00,
    0x01,
  ]);
  assert.throws(
    () => addon.consumeSerializedRows(invalidUtf8),
    /invalid UTF-8 leading byte/,
  );
});

test('streaming rows accept a complete sparse-array wire form', () => {
  const sparseRows = Buffer.from([
    0xff,
    0x0f,
    0x61,
    0x01,
    0x49,
    0x00,
    0x61,
    0x02,
    0x49,
    0x00,
    0x49,
    0x02,
    0x49,
    0x02,
    0x49,
    0x04,
    0x40,
    0x02,
    0x02,
    0x40,
    0x01,
    0x01,
  ]);
  assert.doesNotThrow(() => addon.consumeSerializedRows(sparseRows));
});
