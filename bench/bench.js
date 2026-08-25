'use strict';

const v8 = require('node:v8');
const { performance } = require('node:perf_hooks');
const { encodeSync } = require('..');

function measure(iterations, operation) {
  const start = performance.now();
  for (let index = 0; index < iterations; ++index) operation();
  return ((performance.now() - start) * 1e6) / iterations;
}

function jsonEncode(value, encodeBinary) {
  return Buffer.from(
    JSON.stringify(
      value,
      encodeBinary
        ? (_key, item) => {
            if (!(item instanceof Uint8Array)) return item;
            return {
              $uint8array: Buffer.from(
                item.buffer,
                item.byteOffset,
                item.byteLength,
              ).toString('base64'),
            };
          }
        : undefined,
    ),
  );
}

function jsonDecode(buffer, decodeBinary) {
  return JSON.parse(
    buffer.toString(),
    decodeBinary
      ? (_key, item) => {
          if (
            item !== null &&
            typeof item === 'object' &&
            typeof item.$uint8array === 'string'
          ) {
            return new Uint8Array(Buffer.from(item.$uint8array, 'base64'));
          }
          return item;
        }
      : undefined,
  );
}

function benchmark(name, value, iterations, hasBinary = false) {
  const nativeWire = encodeSync(value);
  const jsonWire = jsonEncode(value, hasBinary);

  const rows = [
    {
      case: name,
      codec: 'v8serial',
      encode_ns: Math.round(measure(iterations, () => encodeSync(value))),
      decode_ns: Math.round(
        measure(iterations, () => v8.deserialize(nativeWire)),
      ),
      wire_bytes: nativeWire.length,
    },
    {
      case: name,
      codec: 'JSON',
      encode_ns: Math.round(
        measure(iterations, () => jsonEncode(value, hasBinary)),
      ),
      decode_ns: Math.round(
        measure(iterations, () => jsonDecode(jsonWire, hasBinary)),
      ),
      wire_bytes: jsonWire.length,
    },
  ];
  console.table(rows);
}

const plain = {
  id: 42,
  name: 'widget',
  enabled: true,
  values: Array.from({ length: 100 }, (_, index) => index),
};
benchmark('plain object', plain, 10_000);

const geometry = {
  id: 42,
  name: 'structural-member-42',
  category: 'beam',
  material: 'steel',
  origin2d: { x: 12.25, y: -8.5 },
  position3d: { x: 12.25, y: -8.5, z: 104.75 },
  rect: { left: 10, top: 20, right: 310, bottom: 220 },
  bounds: {
    low: { x: -5.5, y: -6.25, z: 0 },
    high: { x: 125.75, y: 80.5, z: 210.25 },
  },
  description: 'Exterior frame member with connection metadata',
  blob: Uint8Array.from({ length: 64 }, (_, index) => (index * 17) & 0xff),
};
if (Object.keys(geometry).length !== 10) {
  throw new Error('geometry benchmark must contain exactly 10 top-level properties');
}
benchmark('10-property geometry + 64-byte blob', geometry, 25_000, true);

for (const size of [1024, 1024 * 1024, 10 * 1024 * 1024]) {
  const blob = Buffer.alloc(size, 0x5a);
  const value = { id: 42, blob: new Uint8Array(blob) };
  benchmark(
    `${size} byte blob`,
    value,
    size < 1024 * 1024 ? 1_000 : size < 10 * 1024 * 1024 ? 30 : 5,
    true,
  );
}
