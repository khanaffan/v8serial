'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const v8 = require('node:v8');
const {
  decodeAsync,
  decodeSync,
  encodeAsync,
  encodeSync,
} = require('..');

function hex(value) {
  return Buffer.from(value.replaceAll(/\s/g, ''), 'hex');
}

test('serializer matches source-derived version-15 golden vectors', () => {
  const vectors = [
    [undefined, 'ff0f5f'],
    [null, 'ff0f30'],
    [true, 'ff0f54'],
    [false, 'ff0f46'],
    [0, 'ff0f4900'],
    [-1, 'ff0f4901'],
    [1, 'ff0f4902'],
    [42, 'ff0f4954'],
    [-42, 'ff0f4953'],
    [2147483647, 'ff0f49feffffff0f'],
    [-2147483648, 'ff0f49ffffffff0f'],
    [-0, 'ff0f4e0000000000000080'],
    [1.5, 'ff0f4e000000000000f83f'],
    ['', 'ff0f2200'],
    ['A', 'ff0f220141'],
    ['\u00ff', 'ff0f2201ff'],
    ['\u0100', 'ff0f63020001'],
    [{}, 'ff0f6f7b00'],
    [[], 'ff0f4100240000'],
    [[null, true, 42], 'ff0f410330544954240003'],
    [Uint8Array.from([1, 2, 3]).buffer, 'ff0f4203010203'],
  ];

  for (const [value, expected] of vectors) {
    assert.deepStrictEqual(encodeSync(value), hex(expected));
    assert.deepStrictEqual(decodeSync(hex(expected)), value);
  }
});

test('serializer matches V8 choices across numeric and string boundaries', () => {
  const values = [
    Number.MIN_VALUE,
    Number.MAX_VALUE,
    Number.EPSILON,
    Number.NaN,
    Infinity,
    -Infinity,
    -0,
    -2147483648,
    2147483647,
    2147483648,
    'x'.repeat(127),
    'x'.repeat(128),
    '\u0100'.repeat(63),
    '\u0100'.repeat(64),
    '\u0100'.repeat(8192),
    { alpha: 1, beta: '\u0100', gamma: [true, false, null] },
  ];

  for (const value of values) {
    assert.deepStrictEqual(encodeSync(value), v8.serialize(value));
  }
});

test('native Uint8Array writer emits the standard buffer-plus-view grammar', () => {
  const bytes = Uint8Array.from([1, 2, 3]);
  assert.deepStrictEqual(
    encodeSync(bytes),
    hex('ff0f 42 03 010203 56 42 00 03 00'),
  );
  assert.deepStrictEqual(v8.deserialize(encodeSync(bytes)), bytes);

  const offsetView = hex('ff0f 42 05 0901020309 56 42 01 03 00');
  assert.deepStrictEqual(decodeSync(offsetView), bytes);
  assert.deepStrictEqual(v8.deserialize(offsetView), bytes);
});

test('reader accepts Node host-object forms for Uint8Array and Buffer', () => {
  for (const value of [
    Uint8Array.from([]),
    Uint8Array.from([1, 2, 3]),
    Buffer.from([]),
    Buffer.from([4, 5, 6]),
  ]) {
    const decoded = decodeSync(v8.serialize(value));
    assert.ok(decoded instanceof Uint8Array);
    assert.deepStrictEqual(decoded, new Uint8Array(value));
  }
});

function makePrng(seed) {
  let state = seed >>> 0;
  return () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 0x100000000;
  };
}

function randomValue(random, depth = 0) {
  const scalarFactories = [
    () => undefined,
    () => null,
    () => random() < 0.5,
    () => (random() * 0x100000000 - 0x80000000) | 0,
    () => (random() - 0.5) * Number.MAX_SAFE_INTEGER,
    () => {
      const alphabet = ['a', 'Z', '\u00ff', '\u0100', '\ud83d\ude80'];
      return Array.from(
        { length: Math.floor(random() * 12) },
        () => alphabet[Math.floor(random() * alphabet.length)],
      ).join('');
    },
    () =>
      Uint8Array.from(
        { length: Math.floor(random() * 12) },
        () => Math.floor(random() * 256),
      ),
    () =>
      Uint8Array.from(
        { length: Math.floor(random() * 12) },
        () => Math.floor(random() * 256),
      ).buffer,
  ];
  if (depth >= 4 || random() < 0.55) {
    return scalarFactories[Math.floor(random() * scalarFactories.length)]();
  }
  if (random() < 0.5) {
    return Array.from(
      { length: Math.floor(random() * 6) },
      () => randomValue(random, depth + 1),
    );
  }
  const output = {};
  const count = Math.floor(random() * 6);
  for (let index = 0; index < count; ++index) {
    output[`key_${index}_${String.fromCharCode(97 + index)}`] =
      randomValue(random, depth + 1);
  }
  return output;
}

test('500 deterministic trees are semantically compatible with V8', () => {
  const random = makePrng(0x15c0ffee);
  for (let index = 0; index < 500; ++index) {
    const value = randomValue(random);
    const encoded = encodeSync(value);
    assert.deepStrictEqual(v8.deserialize(encoded), value);
    assert.deepStrictEqual(decodeSync(v8.serialize(value)), value);
  }
});

test('no truncation decodes to the intended complete value', () => {
  const corpus = [
    encodeSync({ alpha: [1, 2, 3], beta: '\u0100'.repeat(64) }),
    encodeSync(Uint8Array.from([1, 2, 3, 4])),
    v8.serialize({ blob: Buffer.from([5, 6, 7]) }),
  ];

  for (const complete of corpus) {
    const expected = decodeSync(complete);
    for (let length = 0; length < complete.length; ++length) {
      try {
        const partial = decodeSync(complete.subarray(0, length));
        assert.notDeepStrictEqual(
          partial,
          expected,
          'a truncated native view may be a valid backing ArrayBuffer, but not the intended view',
        );
      } catch (error) {
        assert.match(error.message, /V8 decode error/);
      }
    }
    assert.doesNotThrow(() => decodeSync(complete));
  }
});

test('reader rejects malformed structural fields and unsupported tags', () => {
  const invalid = [
    'ff0f', // missing root
    'ff0f80', // unsupported tag
    'ff0f498080808010', // uint32 varint overflow
    'ff0f5302c0af', // overlong UTF-8
    'ff0f630100', // odd UTF-16 byte length
    'ff0f6f7b01', // object terminal count mismatch
    'ff0f410130240000', // dense array repeated length mismatch
    'ff0f420201', // ArrayBuffer payload truncated
    'ff0f42030102035642020300', // view exceeds backing buffer
    'ff0f42030102035642000301', // unsupported view flags
    'ff0f5c0200', // unsupported Node host-object type
    'ff0f30ff', // trailing non-padding data
  ];
  for (const bytes of invalid) {
    assert.throws(() => decodeSync(hex(bytes)), /V8 decode error/);
  }
});

test('1000 deterministic malformed payloads fail safely', () => {
  const random = makePrng(0xbadf00d);
  for (let index = 0; index < 1000; ++index) {
    const bytes = Buffer.alloc(3 + Math.floor(random() * 64));
    bytes[0] = 0xff;
    bytes[1] = 0x0f;
    for (let offset = 2; offset < bytes.length; ++offset) {
      bytes[offset] = Math.floor(random() * 256);
    }
    try {
      decodeSync(bytes);
    } catch (error) {
      assert.match(error.message, /V8 decode error/);
    }
  }
});

test('unsupported V8 types fail explicitly instead of being misdecoded', () => {
  const shared = {};
  const unsupported = [
    1n,
    new Date(0),
    /x/gi,
    new Map([['x', 1]]),
    new Set([1]),
    [shared, shared],
  ];
  for (const value of unsupported) {
    assert.throws(() => decodeSync(v8.serialize(value)), /V8 decode error/);
  }
});

test('parallel async encode and decode operations preserve values', async () => {
  const values = Array.from({ length: 64 }, (_, index) => ({
    index,
    text: `value-${index}-\u0100`,
    data: Uint8Array.from([index, index + 1, index + 2]),
  }));

  const encoded = await Promise.all(
    values.map(
      (value) =>
        new Promise((resolve, reject) => {
          encodeAsync(value, (error, buffer) => {
            if (error) reject(error);
            else resolve(buffer);
          });
        }),
    ),
  );
  encoded.forEach((buffer, index) => {
    assert.deepStrictEqual(v8.deserialize(buffer), values[index]);
  });

  const decoded = await Promise.all(
    values.map(
      (value) =>
        new Promise((resolve, reject) => {
          decodeAsync(v8.serialize(value), (error, result) => {
            if (error) reject(error);
            else resolve(result);
          });
        }),
    ),
  );
  assert.deepStrictEqual(decoded, values);
});
