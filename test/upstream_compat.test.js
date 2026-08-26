'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');
const v8 = require('node:v8');

const {
  decodeAsync,
  decodeSync,
  encodeAsync,
  encodeSync,
} = require('..');

function bytesOf(view) {
  if (view instanceof ArrayBuffer) return Buffer.from(view);
  return Buffer.from(view.buffer, view.byteOffset, view.byteLength);
}

function decodeAsyncPromise(input) {
  return new Promise((resolve, reject) => {
    decodeAsync(input, (error, value) => {
      if (error) reject(error);
      else resolve(value);
    });
  });
}

function encodeAsyncPromise(value) {
  return new Promise((resolve, reject) => {
    encodeAsync(value, (error, encoded) => {
      if (error) reject(error);
      else resolve(encoded);
    });
  });
}

test('decode accepts serialized bytes through every Node 22 ArrayBufferView', async () => {
  const serialized = v8.serialize(Uint8Array.from([1, 2, 3, 4]).buffer);
  assert.equal(serialized.byteLength, 8);

  const storage = new ArrayBuffer(serialized.byteLength + 16);
  new Uint8Array(storage, 8, serialized.byteLength).set(serialized);
  const inputs = [
    storage.slice(8, 8 + serialized.byteLength),
    Buffer.from(storage, 8, serialized.byteLength),
    new Int8Array(storage, 8, serialized.byteLength),
    new Uint8Array(storage, 8, serialized.byteLength),
    new Uint8ClampedArray(storage, 8, serialized.byteLength),
    new Int16Array(storage, 8, serialized.byteLength / 2),
    new Uint16Array(storage, 8, serialized.byteLength / 2),
    new Int32Array(storage, 8, serialized.byteLength / 4),
    new Uint32Array(storage, 8, serialized.byteLength / 4),
    new Float32Array(storage, 8, serialized.byteLength / 4),
    new Float64Array(storage, 8, serialized.byteLength / 8),
    new BigInt64Array(storage, 8, serialized.byteLength / 8),
    new BigUint64Array(storage, 8, serialized.byteLength / 8),
    new DataView(storage, 8, serialized.byteLength),
  ];

  for (const input of inputs) {
    const decoded = decodeSync(input);
    assert.ok(decoded instanceof ArrayBuffer);
    assert.deepStrictEqual(bytesOf(decoded), Buffer.from([1, 2, 3, 4]));
  }

  const decoded = await decodeAsyncPromise(
    new DataView(storage, 8, serialized.byteLength),
  );
  assert.deepStrictEqual(bytesOf(decoded), Buffer.from([1, 2, 3, 4]));

  for (const backing of [
    new ArrayBuffer(serialized.byteLength, {
      maxByteLength: serialized.byteLength + 8,
    }),
    new SharedArrayBuffer(serialized.byteLength),
  ]) {
    new Uint8Array(backing).set(serialized);
    assert.deepStrictEqual(
      bytesOf(decodeSync(new Uint8Array(backing))),
      Buffer.from([1, 2, 3, 4]),
    );
    assert.deepStrictEqual(
      bytesOf(decodeSync(new DataView(backing))),
      Buffer.from([1, 2, 3, 4]),
    );
    if (backing instanceof ArrayBuffer) {
      assert.deepStrictEqual(
        bytesOf(decodeSync(backing)),
        Buffer.from([1, 2, 3, 4]),
      );
    }
  }
});

test('reader safely copies an unaligned Node host-object payload', () => {
  const serialized = Buffer.from('ff0f5c0304addeefbe', 'hex');
  const storage = Buffer.alloc(serialized.byteLength + 1);
  serialized.copy(storage, 1);

  const decoded = decodeSync(storage.subarray(1));
  assert.ok(decoded instanceof Int16Array);
  assert.deepStrictEqual(bytesOf(decoded), Buffer.from('addeefbe', 'hex'));
});

test('reader handles representative large Node host-object views', async () => {
  const value = {
    int32: Int32Array.from({ length: 1024 }, (_, index) => -index),
    int16: Int16Array.from({ length: 8192 }, (_, index) => index),
    uint32: Uint32Array.from({ length: 1024 }, (_, index) => index * 17),
    uint16: Uint16Array.from({ length: 8192 }, (_, index) => index * 3),
  };
  const serialized = v8.serialize(value);

  assert.deepStrictEqual(decodeSync(serialized), value);
  assert.deepStrictEqual(await decodeAsyncPromise(serialized), value);
});

test('decode rejects detached input carriers explicitly', () => {
  const serialized = v8.serialize(null);
  for (const makeInput of [
    (buffer) => buffer,
    (buffer) => new Uint8Array(buffer),
    (buffer) => new DataView(buffer),
  ]) {
    const buffer = serialized.buffer.slice(
      serialized.byteOffset,
      serialized.byteOffset + serialized.byteLength,
    );
    const input = makeInput(buffer);
    structuredClone(buffer, { transfer: [buffer] });
    assert.throws(() => decodeSync(input), /detached ArrayBuffer/);
  }
});

test('decode rejects non-byte inputs before starting work', () => {
  assert.throws(
    () => decodeSync('not serialized bytes'),
    /serialized input must be an ArrayBuffer, typed array, or DataView/,
  );
  assert.throws(
    () => decodeAsync('not serialized bytes', () => assert.fail()),
    /serialized input must be an ArrayBuffer, typed array, or DataView/,
  );
});

test('object serialization follows the original enumerable-key snapshot', async () => {
  function deletesLaterKey() {
    return {
      get first() {
        delete this.second;
        return 1;
      },
      second: 2,
    };
  }

  function addsLaterKey() {
    return {
      get first() {
        this.second = 2;
        return 1;
      },
    };
  }

  function restoresLaterKey() {
    return {
      get first() {
        delete this.second;
        this.second = 3;
        return 1;
      },
      second: 2,
    };
  }

  assert.deepStrictEqual(v8.deserialize(encodeSync(deletesLaterKey())), {
    first: 1,
  });
  assert.deepStrictEqual(v8.deserialize(encodeSync(addsLaterKey())), {
    first: 1,
  });
  assert.deepStrictEqual(v8.deserialize(encodeSync(restoresLaterKey())), {
    first: 1,
    second: 3,
  });

  const asyncEncoded = await encodeAsyncPromise(deletesLaterKey());
  assert.deepStrictEqual(v8.deserialize(asyncEncoded), { first: 1 });
});

test('verification markers are consumed iteratively', () => {
  const markerCount = 100_000;
  const serialized = Buffer.allocUnsafe(2 + markerCount * 2);
  serialized[0] = 0xff;
  serialized[1] = 0x0f;
  for (let offset = 2; offset < serialized.length; offset += 2) {
    serialized[offset] = 0x3f;
    serialized[offset + 1] = 0;
  }

  assert.throws(() => decodeSync(serialized), /V8 decode error/);
});

test('reader rejects malformed native ArrayBufferView metadata', () => {
  const invalid = [
    'ff0f42005658000000',
    'ff0f420200005642030100',
    'ff0f420200005642010300',
    'ff0f4204000000005677010200',
    'ff0f4204000000005677000100',
    'ff0f4204000000005656010402',
  ];

  for (const serialized of invalid) {
    assert.throws(
      () => decodeSync(Buffer.from(serialized, 'hex')),
      /V8 decode error/,
    );
  }
});

test('small encode results do not retain external-buffer allocations', () => {
  const modulePath = path.resolve(__dirname, '..');
  const script = `
    const { encodeSync } = require(process.argv[1]);

    (async () => {
      for (let index = 0; index < 5; ++index) {
        global.gc();
        await new Promise(setImmediate);
      }
      const before = process.memoryUsage().rss;
      for (let index = 0; index < 1_000_000; ++index) encodeSync('');
      for (let index = 0; index < 10; ++index) {
        global.gc();
        await new Promise(setImmediate);
      }
      const after = process.memoryUsage().rss;
      if (before !== 0 && after >= before * 10) {
        throw new Error(\`RSS grew from \${before} to \${after}\`);
      }
    })().catch((error) => {
      console.error(error);
      process.exitCode = 1;
    });
  `;
  const result = spawnSync(
    process.execPath,
    ['--expose-gc', '-e', script, modulePath],
    { encoding: 'utf8', timeout: 30_000 },
  );

  assert.equal(result.status, 0, result.stderr || result.stdout);
});

test('large encode results remain valid with external ownership', async () => {
  const value = Uint8Array.from(
    { length: 16 * 1024 },
    (_, index) => index,
  );

  assert.deepStrictEqual(v8.deserialize(encodeSync(value)), value);
  assert.deepStrictEqual(
    v8.deserialize(await encodeAsyncPromise(value)),
    value,
  );
});
