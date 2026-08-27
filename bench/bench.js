'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { performance } = require('node:perf_hooks');
const v8 = require('node:v8');
const { decodeSync, encodeSync } = require('..');

const root = path.resolve(__dirname, '..');
const nativeExecutable = (target) =>
  path.join(
    root,
    'build',
    'Release',
    process.platform === 'win32' ? `${target}.exe` : target,
  );

let sink = 0;

function makeBlob(size) {
  return Uint8Array.from({ length: size }, (_, index) => (index * 17) & 0xff);
}

function geometry(includeBlob) {
  const value = {
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
  };
  if (includeBlob) value.blob = makeBlob(64);
  return value;
}

const scenarios = [
  { id: 'scalar', label: 'Scalar int32', value: 42, iterations: 300_000 },
  {
    id: 'point3d',
    label: 'Point3d',
    value: { x: 12.25, y: -8.5, z: 104.75 },
    iterations: 150_000,
  },
  {
    id: 'geometry',
    label: 'Nested geometry',
    value: geometry(false),
    iterations: 25_000,
  },
  {
    id: 'geometry-64b',
    label: 'Geometry + 64 B',
    value: geometry(true),
    iterations: 25_000,
    binary: true,
  },
  {
    id: 'points-100',
    label: '100 Point3d objects',
    value: Array.from({ length: 100 }, (_, index) => ({
      x: index * 1.25,
      y: index * -0.5,
      z: index * 2,
    })),
    iterations: 1_500,
  },
  {
    id: 'latin1-4k',
    label: '4 KiB Latin-1 string',
    value: '\xe9'.repeat(4096),
    iterations: 10_000,
  },
  {
    id: 'blob-1k',
    label: '1 KiB blob',
    value: { id: 42, blob: makeBlob(1024) },
    iterations: 15_000,
    binary: true,
  },
  {
    id: 'blob-1m',
    label: '1 MiB blob',
    value: { id: 42, blob: makeBlob(1024 * 1024) },
    iterations: 40,
    binary: true,
  },
];

function consume(value) {
  if (Buffer.isBuffer(value) || value instanceof Uint8Array) {
    sink = (sink + value.length + 1) >>> 0;
  } else if (value && typeof value === 'object') {
    sink = (sink + Object.keys(value).length + 1) >>> 0;
  } else if (typeof value === 'number') {
    sink = (sink + (Number.isFinite(value) ? Math.trunc(value) : 1) + 1) >>> 0;
  } else {
    sink = (sink + 1) >>> 0;
  }
}

function median(values) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.floor(sorted.length / 2)];
}

function measure(iterations, operation) {
  const warmup = Math.min(iterations, 10_000);
  for (let index = 0; index < warmup; ++index) consume(operation());

  const samples = [];
  for (let round = 0; round < 7; ++round) {
    if (global.gc) global.gc();
    const start = performance.now();
    for (let index = 0; index < iterations; ++index) consume(operation());
    samples.push(((performance.now() - start) * 1e6) / iterations);
  }
  return { median_ns: median(samples), samples_ns: samples };
}

function jsonBytes(value, binaryEncoding) {
  return Buffer.from(
    JSON.stringify(value, (_key, item) => {
      if (!(item instanceof Uint8Array)) return item;
      if (binaryEncoding === 'array') {
        return { $uint8array: Array.from(item) };
      }
      if (binaryEncoding === 'base64') {
        return {
          $uint8array: Buffer.from(
            item.buffer,
            item.byteOffset,
            item.byteLength,
          ).toString('base64'),
        };
      }
      throw new Error('plain JSON cannot preserve Uint8Array values');
    }),
  );
}

function jsonValue(bytes, binaryEncoding) {
  return JSON.parse(bytes.toString(), (_key, item) => {
    if (
      item === null ||
      typeof item !== 'object' ||
      !Object.hasOwn(item, '$uint8array')
    ) {
      return item;
    }
    if (binaryEncoding === 'array') {
      return Uint8Array.from(item.$uint8array);
    }
    return new Uint8Array(Buffer.from(item.$uint8array, 'base64'));
  });
}

function benchmarkCodec(scenario, codec, encode, decode) {
  const wire = encode(scenario.value);
  const encodeResult = measure(scenario.iterations, () => encode(scenario.value));
  const decodeResult = measure(scenario.iterations, () => decode(wire));
  return {
    scenario: scenario.id,
    label: scenario.label,
    codec,
    encode_ns: encodeResult.median_ns,
    decode_ns: decodeResult.median_ns,
    roundtrip_ns: encodeResult.median_ns + decodeResult.median_ns,
    wire_bytes: wire.length,
    encode_samples_ns: encodeResult.samples_ns,
    decode_samples_ns: decodeResult.samples_ns,
  };
}

function readNativeOutput(executable, codec) {
  const output = execFileSync(executable, [], { encoding: 'utf8' });
  const results = new Map();
  const writerReuse = [];
  for (const line of output.trim().split('\n')) {
    const [scenario, encodeNs, decodeNs, wireBytes] = line.split('\t');
    if (scenario.startsWith('writer-reuse-')) {
      writerReuse.push({
        scenario: scenario.slice('writer-reuse-'.length),
        codec,
        fresh_ns: Number(encodeNs),
        reused_ns: Number(decodeNs),
        wire_bytes: Number(wireBytes),
      });
      continue;
    }
    results.set(scenario, {
      scenario,
      codec,
      encode_ns: Number(encodeNs),
      decode_ns: Number(decodeNs),
      roundtrip_ns: Number(encodeNs) + Number(decodeNs),
      wire_bytes: Number(wireBytes),
    });
  }
  return { results, writerReuse };
}

function formatTime(nanoseconds) {
  if (nanoseconds < 1000) return `${nanoseconds.toFixed(0)} ns`;
  if (nanoseconds < 1e6) return `${(nanoseconds / 1e3).toFixed(2)} us`;
  return `${(nanoseconds / 1e6).toFixed(2)} ms`;
}

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(2)} KiB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MiB`;
}

function formatLatencyChange(freshNs, reusedNs) {
  const percent = (1 - reusedNs / freshNs) * 100;
  return percent >= 0
    ? `${percent.toFixed(1)}% lower`
    : `${(-percent).toFixed(1)}% higher`;
}

function escapeXml(text) {
  return String(text)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');
}

const colors = {
  'C++ headers (SIMD)': '#0969da',
  'C++ headers (scalar)': '#6e7781',
  'v8serial addon': '#54aeff',
  'Node v8': '#8250df',
  'JSON text': '#1a7f37',
  'JSON byte array': '#bf8700',
  'JSON base64': '#cf222e',
};

function renderChart(results) {
  const width = 1440;
  const height = 1060;
  const left = 95;
  const right = 25;
  const top = 105;
  const panelHeight = 210;
  const panelGap = 105;
  const plotWidth = width - left - right;
  const codecOrder = Object.keys(colors);
  function panel(metric, y, title, formatter, minimum) {
    const maximum = Math.max(...results.map((result) => result[metric]));
    const logMin = Math.log10(minimum);
    const logMax = Math.log10(maximum);
    const scale = (value) =>
      y +
      panelHeight -
      ((Math.log10(Math.max(value, minimum)) - logMin) /
        (logMax - logMin)) *
        panelHeight;
    const groupWidth = plotWidth / scenarios.length;
    const barWidth = Math.min(20, (groupWidth - 18) / codecOrder.length);
    let svg = `<text x="${left}" y="${y - 24}" class="panel">${escapeXml(title)}</text>`;

    for (let power = Math.ceil(logMin); power <= Math.floor(logMax); ++power) {
      const value = 10 ** power;
      const lineY = scale(value);
      svg += `<line x1="${left}" y1="${lineY}" x2="${width - right}" y2="${lineY}" class="grid"/>`;
      svg += `<text x="${left - 10}" y="${lineY + 4}" class="axis" text-anchor="end">${escapeXml(formatter(value))}</text>`;
    }

    scenarios.forEach((scenario, scenarioIndex) => {
      const rows = results.filter((result) => result.scenario === scenario.id);
      const center = left + groupWidth * (scenarioIndex + 0.5);
      const totalWidth = rows.length * barWidth;
      rows.forEach((result, codecIndex) => {
        const x = center - totalWidth / 2 + codecIndex * barWidth;
        const barY = scale(result[metric]);
        svg += `<rect x="${x}" y="${barY}" width="${barWidth - 2}" height="${y + panelHeight - barY}" fill="${colors[result.codec]}"><title>${escapeXml(`${scenario.label} - ${result.codec}: ${formatter(result[metric])}`)}</title></rect>`;
      });
      svg += `<text x="${center}" y="${y + panelHeight + 18}" class="scenario" text-anchor="middle">${escapeXml(scenario.label)}</text>`;
    });
    return svg;
  }

  let legend = '';
  Object.entries(colors).forEach(([name, color], index) => {
    const x = left + index * 205;
    legend += `<rect x="${x}" y="55" width="14" height="14" fill="${color}"/>`;
    legend += `<text x="${x + 20}" y="67" class="legend">${escapeXml(name)}</text>`;
  });

  return `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">
<style>
  text { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; fill: #1f2328; }
  .title { font-size: 24px; font-weight: 600; }
  .panel { font-size: 18px; font-weight: 600; }
  .legend, .axis { font-size: 12px; }
  .scenario { font-size: 11px; }
  .grid { stroke: #d0d7de; stroke-width: 1; }
</style>
<rect width="100%" height="100%" fill="white"/>
<text x="${left}" y="32" class="title">V8 serialization performance (median, logarithmic scale)</text>
${legend}
${panel('encode_ns', top, 'Encode latency', formatTime, 10)}
${panel('decode_ns', top + panelHeight + panelGap, 'Decode latency', formatTime, 10)}
${panel('wire_bytes', top + (panelHeight + panelGap) * 2, 'Wire size', formatBytes, 1)}
</svg>
`;
}

function renderMarkdown(report) {
  const result = (scenario, codec) =>
    report.results.find(
      (row) => row.scenario === scenario && row.codec === codec,
    );
  const faster = (baseline, candidate) =>
    (baseline.roundtrip_ns / candidate.roundtrip_ns).toFixed(1);
  const geometryHeaders = result('geometry-64b', 'C++ headers (SIMD)');
  const geometryAddon = result('geometry-64b', 'v8serial addon');
  const geometryBase64 = result('geometry-64b', 'JSON base64');
  const blobHeaders = result('blob-1m', 'C++ headers (SIMD)');
  const blobAddon = result('blob-1m', 'v8serial addon');
  const blobBase64 = result('blob-1m', 'JSON base64');
  const latin1Simd = result('latin1-4k', 'C++ headers (SIMD)');
  const latin1Scalar = result('latin1-4k', 'C++ headers (scalar)');
  const writerReuse = report.writer_reuse ?? [];
  const reuseResult = (scenario, codec) =>
    writerReuse.find(
      (row) => row.scenario === scenario && row.codec === codec,
    );
  const scalarReuse = reuseResult('scalar', 'C++ headers (SIMD)');
  const geometryReuse = reuseResult('geometry', 'C++ headers (SIMD)');

  const lines = [
    '# Performance',
    '',
    `Generated ${report.environment.generated_at} on ${report.environment.cpu} (${report.environment.arch}), Node ${report.environment.node}, V8 ${report.environment.v8}.`,
    '',
    '![Performance comparison](performance.svg)',
    '',
    '## Methodology',
    '',
    '- Seven timed rounds per operation; tables report the median nanoseconds per operation.',
    '- Every operation is warmed up first and its result is consumed.',
    '- Encode and decode are measured separately; round trip is the sum of their medians.',
    ...(writerReuse.length > 0
      ? [
          '- The writer-reuse comparison measures only C++ encoding: the fresh path constructs and destroys a capacity-reserved `Writer` per message, while the reuse path keeps one `Writer` and calls `reset()`. Both consume `size()` and exclude copying bytes to a downstream consumer.',
        ]
      : []),
    '- `C++ headers (SIMD)` calls `Writer` and `Reader` directly with native values and enables the architecture-specific string paths.',
    '- `C++ headers (scalar)` builds the same benchmark with `V8SERIAL_DISABLE_SIMD=1` for a like-for-like baseline.',
    '- `v8serial addon` includes generic JavaScript object traversal and N-API boundary cost.',
    '- `Node v8` is Node.js `v8.serialize()` and `v8.deserialize()`.',
    '- `JSON text` applies only to payloads without binary values.',
    '- `JSON byte array` preserves Uint8Array as JSON decimal byte arrays.',
    '- `JSON base64` preserves Uint8Array using base64 text and includes conversion in both timings.',
    '- Results are machine-specific; regenerate with `npm run bench`.',
    '- The C++ reader returns owning strings and byte vectors. Node may reconstruct a host-object typed array as a view into the serialized input, so its large-blob decode has different ownership semantics.',
    '',
    '## Highlights',
    '',
    '- JSON text is fastest for scalar and small plain JavaScript values because V8 has highly optimized built-in JSON paths.',
    `- SIMD makes the 4 KiB Latin-1 native round trip ${faster(latin1Scalar, latin1Simd)}x faster than the scalar path.`,
    `- For geometry with a 64-byte blob, direct C++ headers are ${faster(geometryBase64, geometryHeaders)}x faster than JSON base64. The generic addon bridge is ${(geometryAddon.roundtrip_ns / geometryBase64.roundtrip_ns).toFixed(1)}x the JSON-base64 latency because JavaScript property traversal dominates this small payload.`,
    `- For a 1 MiB blob, direct C++ headers are ${faster(blobBase64, blobHeaders)}x faster and the addon bridge is ${faster(blobBase64, blobAddon)}x faster than JSON base64, while avoiding base64's wire-size expansion.`,
    '- Node V8 is exceptionally fast for large typed arrays because its host-object deserializer may return a view into the serialized input; the standalone reader instead returns owning native bytes.',
    ...(scalarReuse && geometryReuse
      ? [
          `- On this run, reusing one reserved \`Writer\` with \`reset()\` measured ${formatLatencyChange(scalarReuse.fresh_ns, scalarReuse.reused_ns)} latency for scalar encoding and ${formatLatencyChange(geometryReuse.fresh_ns, geometryReuse.reused_ns)} for nested geometry versus constructing and destroying a writer for every message.`,
        ]
      : []),
    '',
  ];

  if (writerReuse.length > 0) {
    const labels = new Map([
      ['scalar', 'Scalar int32'],
      ['geometry', 'Nested geometry'],
    ]);
    lines.push(
      '## Writer buffer reuse',
      '',
      '`reset()` clears message state and re-emits the wire header while retaining the writer buffer capacity. The table isolates that lifecycle benefit; it does not include copying the completed bytes into a queue, socket, or consumer-owned buffer. See the [writer guide](writer.md#reusing-buffer-capacity) for the ownership rules.',
      '',
      '| Build | Payload | Fresh writer | Reused writer | Latency change | Speedup | Wire size |',
      '|---|---|---:|---:|---:|---:|---:|',
    );
    for (const scenario of ['scalar', 'geometry']) {
      for (const codec of ['C++ headers (SIMD)', 'C++ headers (scalar)']) {
        const row = reuseResult(scenario, codec);
        if (!row) continue;
        lines.push(
          `| ${codec} | ${labels.get(scenario)} | ${formatTime(row.fresh_ns)} | ${formatTime(row.reused_ns)} | ${formatLatencyChange(row.fresh_ns, row.reused_ns)} | ${(row.fresh_ns / row.reused_ns).toFixed(2)}x | ${formatBytes(row.wire_bytes)} |`,
        );
      }
    }
    lines.push('');
  }

  lines.push('## Results', '');

  for (const scenario of scenarios) {
    const rows = report.results.filter(
      (result) => result.scenario === scenario.id,
    );
    lines.push(`### ${scenario.label}`, '');
    lines.push('| Codec | Encode | Decode | Round trip | Operations/s | Wire size |');
    lines.push('|---|---:|---:|---:|---:|---:|');
    for (const result of rows) {
      lines.push(
        `| ${result.codec} | ${formatTime(result.encode_ns)} | ${formatTime(result.decode_ns)} | ${formatTime(result.roundtrip_ns)} | ${Math.round(1e9 / result.roundtrip_ns).toLocaleString('en-US')} | ${formatBytes(result.wire_bytes)} |`,
      );
    }
    lines.push('');
  }

  lines.push(
    '## Interpretation',
    '',
    'The direct C++ figures represent the intended worker-thread use case: native code writes or reads the header-only value model without first crossing the JavaScript object boundary. Addon figures intentionally include that boundary and therefore isolate its cost.',
    '',
    'JSON text is highly optimized for small plain JavaScript values. Binary payloads require an additional representation: decimal byte arrays preserve bytes without base64 but greatly increase size and CPU work, while base64 adds approximately one-third wire-size overhead plus conversion cost.',
    '',
    'Raw measurements, including all JavaScript timing samples, are stored in [`../bench/results.json`](../bench/results.json).',
    '',
  );
  return `${lines.join('\n')}\n`;
}

function writeReport(report) {
  fs.writeFileSync(
    path.join(__dirname, 'results.json'),
    `${JSON.stringify(report, null, 2)}\n`,
  );
  fs.writeFileSync(
    path.join(root, 'docs', 'performance.md'),
    renderMarkdown(report),
  );
  fs.writeFileSync(
    path.join(root, 'docs', 'performance.svg'),
    renderChart(report.results),
  );
}

if (process.argv.includes('--render-only')) {
  const report = JSON.parse(
    fs.readFileSync(path.join(__dirname, 'results.json'), 'utf8'),
  );
  writeReport(report);
  console.log('Rendered docs/performance.md and docs/performance.svg');
  process.exit(0);
}

const nativeOutputs = [
  readNativeOutput(
    nativeExecutable('v8serial_native_bench'),
    'C++ headers (SIMD)',
  ),
  readNativeOutput(
    nativeExecutable('v8serial_native_bench_scalar'),
    'C++ headers (scalar)',
  ),
];
const results = [];

for (const scenario of scenarios) {
  for (const nativeOutput of nativeOutputs) {
    const native = nativeOutput.results.get(scenario.id);
    if (!native) throw new Error(`missing native result for ${scenario.id}`);
    native.label = scenario.label;
    results.push(native);
  }
  results.push(
    benchmarkCodec(scenario, 'v8serial addon', encodeSync, decodeSync),
    benchmarkCodec(scenario, 'Node v8', v8.serialize, v8.deserialize),
  );

  if (scenario.binary) {
    results.push(
      benchmarkCodec(
        scenario,
        'JSON byte array',
        (value) => jsonBytes(value, 'array'),
        (bytes) => jsonValue(bytes, 'array'),
      ),
      benchmarkCodec(
        scenario,
        'JSON base64',
        (value) => jsonBytes(value, 'base64'),
        (bytes) => jsonValue(bytes, 'base64'),
      ),
    );
  } else {
    results.push(
      benchmarkCodec(
        scenario,
        'JSON text',
        (value) => Buffer.from(JSON.stringify(value)),
        (bytes) => JSON.parse(bytes.toString()),
      ),
    );
  }
}

const writerReuseLabels = new Map([
  ['scalar', 'Scalar int32'],
  ['geometry', 'Nested geometry'],
]);
const writerReuse = nativeOutputs.flatMap((output) =>
  output.writerReuse.map((result) => ({
    ...result,
    label: writerReuseLabels.get(result.scenario) ?? result.scenario,
  })),
);

const report = {
  environment: {
    generated_at: new Date().toISOString(),
    platform: process.platform,
    arch: process.arch,
    cpu: os.cpus()[0]?.model ?? 'unknown CPU',
    node: process.version,
    v8: process.versions.v8,
    rounds: 7,
  },
  scenarios: scenarios.map(({ id, label, iterations, binary }) => ({
    id,
    label,
    iterations,
    binary: Boolean(binary),
  })),
  results,
  writer_reuse: writerReuse,
};

writeReport(report);

console.table(
  results.map((result) => ({
    scenario: result.label,
    codec: result.codec,
    encode: formatTime(result.encode_ns),
    decode: formatTime(result.decode_ns),
    roundtrip: formatTime(result.roundtrip_ns),
    bytes: formatBytes(result.wire_bytes),
  })),
);
console.log(`benchmark sink: ${sink}`);
