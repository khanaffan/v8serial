'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const test = require('node:test');
const v8 = require('node:v8');
const { Worker } = require('node:worker_threads');
const {
  decodeAsync,
  decodeSync,
  encodeAsync,
  encodeSync,
  formatVersion,
} = require('..');

const scalarCases = [
  undefined,
  null,
  true,
  false,
  42,
  -42,
  -0,
  1.5,
  Number.NaN,
  Infinity,
  'widget',
  '\u0100',
  '\u0100'.repeat(64),
];

test('reports the runtime wire-format version', () => {
  assert.equal(formatVersion, v8.serialize(undefined)[1]);
});

test('round-trips supported scalar values', () => {
  for (const value of scalarCases) {
    assert.deepStrictEqual(v8.deserialize(encodeSync(value)), value);
  }
});

test('round-trips Date values', () => {
  for (const value of [new Date(1234.5), new Date(Number.NaN)]) {
    const fromWriter = v8.deserialize(encodeSync(value));
    assert.ok(fromWriter instanceof Date);
    assert.ok(Object.is(fromWriter.valueOf(), value.valueOf()));

    const fromReader = decodeSync(v8.serialize(value));
    assert.ok(fromReader instanceof Date);
    assert.ok(Object.is(fromReader.valueOf(), value.valueOf()));
  }
});

test('matches deterministic V8 bytes for scalar values', () => {
  for (const value of scalarCases) {
    assert.deepStrictEqual(encodeSync(value), v8.serialize(value));
  }
});

test('round-trips nested dense arrays and plain objects', () => {
  const value = {
    id: 42,
    name: 'widget',
    unicode: '\u0100',
    flags: [true, false, null],
    nested: { value: 1.5 },
  };
  assert.deepStrictEqual(v8.deserialize(encodeSync(value)), value);
  assert.deepStrictEqual(encodeSync(value), v8.serialize(value));
});

test('aligns long UTF-16 values and keys like V8', () => {
  const longString = '\u0100'.repeat(64);
  const value = { [longString]: longString };
  assert.deepStrictEqual(encodeSync(value), v8.serialize(value));
  assert.deepStrictEqual(v8.deserialize(encodeSync(value)), value);
});

test('round-trips ArrayBuffer and Uint8Array without base64', () => {
  const arrayBuffer = Uint8Array.from([1, 2, 3]).buffer;
  const bytes = Uint8Array.from([4, 5, 6]);

  const decodedBuffer = v8.deserialize(encodeSync(arrayBuffer));
  assert.ok(decodedBuffer instanceof ArrayBuffer);
  assert.deepStrictEqual(new Uint8Array(decodedBuffer), new Uint8Array(arrayBuffer));

  const decodedBytes = v8.deserialize(encodeSync(bytes));
  assert.ok(decodedBytes instanceof Uint8Array);
  assert.deepStrictEqual(decodedBytes, bytes);
});

function viewBytes(value) {
  return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
}

test('round-trips every Node 22 typed array and DataView', () => {
  const views = [
    new Int8Array([-1, 2]),
    new Uint8Array([1, 2]),
    new Uint8ClampedArray([1, 255]),
    new Int16Array([-1, 2]),
    new Uint16Array([1, 65535]),
    new Int32Array([-1, 2]),
    new Uint32Array([1, 0xffffffff]),
    new Float32Array([1.5, -2.25]),
    new Float64Array([Math.PI]),
    new BigInt64Array([-1n, 2n]),
    new BigUint64Array([1n, 2n]),
    new DataView(Uint8Array.from([9, 1, 2, 9]).buffer, 1, 2),
  ];

  for (const value of views) {
    const encoded = encodeSync(value);
    const fromWriter = v8.deserialize(encoded);
    assert.equal(fromWriter.constructor, value.constructor);
    assert.deepStrictEqual(viewBytes(fromWriter), viewBytes(value));

    const nativeRoundTrip = decodeSync(encoded);
    assert.equal(nativeRoundTrip.constructor, value.constructor);
    assert.deepStrictEqual(viewBytes(nativeRoundTrip), viewBytes(value));

    const fromReader = decodeSync(v8.serialize(value));
    assert.equal(fromReader.constructor, value.constructor);
    assert.deepStrictEqual(viewBytes(fromReader), viewBytes(value));
  }
});

test('rejects detached buffers and views instead of encoding empty values', () => {
  for (const value of [
    new ArrayBuffer(2),
    new Int16Array([1, 2]),
    new DataView(Uint8Array.from([1, 2]).buffer),
  ]) {
    const buffer = value instanceof ArrayBuffer ? value : value.buffer;
    structuredClone(buffer, { transfer: [buffer] });
    assert.throws(() => encodeSync(value), /detached ArrayBuffer/);
  }
});

test('rejects SharedArrayBuffer-backed views', () => {
  const view = new Int16Array(new SharedArrayBuffer(4));
  assert.throws(() => encodeSync(view), /SharedArrayBuffer/);
});

test('rejects resizable ArrayBuffers and their views', () => {
  const buffer = new ArrayBuffer(4, { maxByteLength: 8 });
  Object.defineProperty(buffer, 'resizable', { value: false });
  assert.throws(() => encodeSync(buffer), /resizable ArrayBuffer/);
  assert.throws(
    () => encodeAsync(buffer, () => {}),
    /resizable ArrayBuffer/,
  );

  const viewBuffer = new ArrayBuffer(4, { maxByteLength: 8 });
  assert.throws(
    () => encodeSync(new Int16Array(viewBuffer)),
    /resizable ArrayBuffer/,
  );
});

test('encodes on a worker and returns through a thread-safe callback', async () => {
  const value = {
    id: 7,
    created: new Date(1234),
    samples: new Int16Array([-1, 2]),
  };
  const encoded = await new Promise((resolve, reject) => {
    encodeAsync(value, (error, buffer) => {
      if (error) reject(error);
      else resolve(buffer);
    });
  });
  assert.deepStrictEqual(v8.deserialize(encoded), value);
});

test('rejects values that cannot be represented by the v1 format', () => {
  const cyclic = {};
  cyclic.self = cyclic;
  assert.throws(() => encodeSync(cyclic), /cyclic/);
  assert.throws(() => encodeSync([, 1]), /sparse arrays/);

  const arrayWithProperty = [1];
  arrayWithProperty.extra = true;
  assert.throws(() => encodeSync(arrayWithProperty), /array properties/);
});

test('rejects excessive nesting instead of overflowing the native stack', () => {
  let value = null;
  for (let depth = 0; depth < 600; ++depth) value = [value];
  assert.throws(() => encodeSync(value), /maximum nesting depth/);
});

test('worker termination waits for an active encoder thread', async () => {
  const modulePath = path.resolve(__dirname, '..');
  const worker = new Worker(
    `
      const { parentPort } = require('node:worker_threads');
      const { encodeAsync } = require(${JSON.stringify(modulePath)});
      encodeAsync(Array.from({ length: 100000 }, (_, index) => index), () => {});
      parentPort.postMessage('started');
    `,
    { eval: true },
  );

  await new Promise((resolve, reject) => {
    worker.once('message', resolve);
    worker.once('error', reject);
  });
  await worker.terminate();
});

test('standalone reader decodes V8 scalar, object, array, and buffer output', () => {
  const values = [
    ...scalarCases,
    { id: 42, name: 'widget', nested: [true, null, 1.5] },
    Uint8Array.from([1, 2, 3]).buffer,
  ];
  for (const value of values) {
    assert.deepStrictEqual(decodeSync(v8.serialize(value)), value);
  }
});

test('reader creates __proto__ as an own data property', () => {
  const value = JSON.parse('{"__proto__":{"polluted":true}}');
  const decoded = decodeSync(v8.serialize(value));
  assert.equal(Object.getPrototypeOf(decoded), Object.prototype);
  assert.equal(Object.hasOwn(decoded, '__proto__'), true);
  assert.deepStrictEqual(decoded, value);
});

test('reader handles Node host-object and native Uint8Array encodings', () => {
  const bytes = Uint8Array.from([4, 5, 6]);
  assert.deepStrictEqual(decodeSync(v8.serialize(bytes)), bytes);
  assert.deepStrictEqual(decodeSync(encodeSync(bytes)), bytes);

  const buffer = Buffer.from([7, 8, 9]);
  const decoded = decodeSync(v8.serialize(buffer));
  assert.ok(decoded instanceof Uint8Array);
  assert.deepStrictEqual(decoded, new Uint8Array(buffer));
});

test('decodes in a native worker without accessing V8 off-thread', async () => {
  const value = {
    id: 7,
    created: new Date(1234),
    samples: new Int16Array([-1, 2]),
  };
  const decoded = await new Promise((resolve, reject) => {
    decodeAsync(v8.serialize(value), (error, result) => {
      if (error) reject(error);
      else resolve(result);
    });
  });
  assert.deepStrictEqual(decoded, value);
});

test('worker termination waits for an active decoder thread', async () => {
  const modulePath = path.resolve(__dirname, '..');
  const serialized = v8.serialize(
    Array.from({ length: 100000 }, (_, index) => index),
  );
  const worker = new Worker(
    `
      const { parentPort, workerData } = require('node:worker_threads');
      const { decodeAsync } = require(${JSON.stringify(modulePath)});
      decodeAsync(workerData, () => {});
      parentPort.postMessage('started');
    `,
    { eval: true, workerData: serialized },
  );

  await new Promise((resolve, reject) => {
    worker.once('message', resolve);
    worker.once('error', reject);
  });
  await worker.terminate();
});

test('reader rejects malformed, unsupported, and trailing data', () => {
  assert.throws(
    () => decodeSync(Buffer.from('ff1060', 'hex')),
    /only V8 wire-format version 15/,
  );
  assert.throws(
    () => decodeSync(Buffer.from('ff0f6f7b01', 'hex')),
    /property count mismatch/,
  );
  assert.throws(
    () => decodeSync(Buffer.from('ff0f30ff', 'hex')),
    /trailing bytes/,
  );

  const shared = {};
  assert.throws(
    () => decodeSync(v8.serialize([shared, shared])),
    /unsupported or invalid serialization tag/,
  );
});
