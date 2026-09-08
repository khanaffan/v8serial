'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const v8 = require('node:v8');

const root = path.resolve(__dirname, '..');
const addonPath = path.join(
  root,
  'build',
  'Release',
  'v8serial_bench_napi.node',
);

const armLabels = {
  'per-cell': 'Per-cell calls + per-row step',
  'per-row': 'Per-row variadic call + step',
  'bulk-array-generic': 'One call, validated N-API array traversal',
  'bulk-array-optimized': 'One call, optimized N-API array traversal',
  'bulk-object': 'One call, N-API object traversal',
  'serialized-tree': 'One buffer, owning value tree',
  'serialized-stream-copy': 'One buffer, copied streaming rows',
  'serialized-stream-borrow': 'One buffer, borrowed streaming rows',
  native: 'Native-generated ceiling',
};

function makeRow(rowIndex, columnCount) {
  const row = new Array(columnCount);
  for (let column = 0; column < columnCount; ++column) {
    switch (column % 5) {
      case 0:
        row[column] = rowIndex;
        break;
      case 1:
        row[column] = `row-${rowIndex & 1023}`;
        break;
      case 2:
        row[column] = rowIndex * 0.25 + 0.5;
        break;
      case 3:
        row[column] = (rowIndex & 1) === 0;
        break;
      default:
        row[column] = rowIndex % 17 === 0 ? null : rowIndex;
        break;
    }
  }
  return row;
}

function makeRows(rowCount, columnCount) {
  return Array.from({ length: rowCount }, (_, rowIndex) =>
    makeRow(rowIndex, columnCount),
  );
}

function makeObjectRows(rowCount, columnCount) {
  return Array.from({ length: rowCount }, (_, rowIndex) => {
    const values = makeRow(rowIndex, columnCount);
    const row = {};
    for (let column = 0; column < columnCount; ++column) {
      row[`c${column}`] = values[column];
    }
    return row;
  });
}

function elapsedNanoseconds(start) {
  return Number(process.hrtime.bigint() - start);
}

function runOperation(addon, arm, rows, rowCount, columnCount, serialized) {
  switch (arm) {
    case 'per-cell':
      for (const row of rows) {
        for (const value of row) addon.consumeCell(value);
        addon.consumeStep();
      }
      return;
    case 'per-row':
      for (const row of rows) {
        addon.consumeArgs(...row);
        addon.consumeStep();
      }
      return;
    case 'bulk-array-generic':
      addon.consumeRowsGeneric(rows);
      return;
    case 'bulk-array-optimized':
      addon.consumeRowsOptimized(rows);
      return;
    case 'bulk-object':
      addon.consumeObjectRows(rows);
      return;
    case 'serialized-tree':
      addon.consumeSerializedTree(serialized);
      return;
    case 'serialized-stream-copy':
      addon.consumeSerializedRowsCopied(serialized);
      return;
    case 'serialized-stream-borrow':
      addon.consumeSerializedRows(serialized);
      return;
    case 'native':
      addon.consumeNativeRows(rowCount, columnCount);
      return;
    default:
      throw new Error(`unknown boundary benchmark arm: ${arm}`);
  }
}

function runWorker(spec) {
  if (!fs.existsSync(addonPath)) {
    throw new Error(
      `missing ${addonPath}; run npm run build before the benchmark`,
    );
  }
  const addon = require(addonPath);
  const usesObjects = spec.arm === 'bulk-object';
  const usesRows = spec.arm !== 'native';
  const usesSerialized = spec.arm.startsWith('serialized-');

  const buildStart = process.hrtime.bigint();
  const rows = usesRows
    ? usesObjects
      ? makeObjectRows(spec.rows, spec.columns)
      : makeRows(spec.rows, spec.columns)
    : null;
  const buildNs = elapsedNanoseconds(buildStart);

  let serialized = null;
  let serializeNs = 0;
  if (usesSerialized) {
    const serializeStart = process.hrtime.bigint();
    serialized = v8.serialize(rows);
    serializeNs = elapsedNanoseconds(serializeStart);
  }

  const warmRows = usesObjects
    ? makeObjectRows(Math.min(spec.rows, 100), spec.columns)
    : makeRows(Math.min(spec.rows, 100), spec.columns);
  const warmSerialized = usesSerialized ? v8.serialize(warmRows) : null;
  runOperation(
    addon,
    spec.arm,
    spec.arm === 'native' ? null : warmRows,
    Math.min(spec.rows, 100),
    spec.columns,
    warmSerialized,
  );

  addon.resetSink();
  const calibrationStart = process.hrtime.bigint();
  runOperation(
    addon,
    spec.arm,
    rows,
    spec.rows,
    spec.columns,
    serialized,
  );
  const calibrationNs = elapsedNanoseconds(calibrationStart);
  const singleSink = addon.getSink().toString();
  const operationRepetitions = Math.min(
    100,
    Math.max(1, Math.ceil(100_000_000 / calibrationNs)),
  );

  if (global.gc) global.gc();
  addon.resetSink();
  const operationStart = process.hrtime.bigint();
  for (let iteration = 0; iteration < operationRepetitions; ++iteration) {
    runOperation(
      addon,
      spec.arm,
      rows,
      spec.rows,
      spec.columns,
      serialized,
    );
  }
  const operationNs =
    elapsedNanoseconds(operationStart) / operationRepetitions;
  const sink = addon.getSink().toString();

  const gcStart = process.hrtime.bigint();
  if (global.gc) global.gc();
  const postGcNs = elapsedNanoseconds(gcStart);

  return {
    ...spec,
    build_ns: buildNs,
    serialize_ns: serializeNs,
    operation_repetitions: operationRepetitions,
    operation_ns: operationNs,
    total_ns: serializeNs + operationNs,
    post_gc_ns: postGcNs,
    wire_bytes: serialized?.byteLength ?? 0,
    max_rss_kib: process.resourceUsage().maxRSS,
    sink,
    single_sink: singleSink,
  };
}

function median(values) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.floor(sorted.length / 2)];
}

function percentile(values, fraction) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.ceil((sorted.length - 1) * fraction)];
}

function coefficientOfVariation(values) {
  const mean = values.reduce((sum, value) => sum + value, 0) / values.length;
  const variance =
    values.reduce((sum, value) => sum + (value - mean) ** 2, 0) /
    values.length;
  return mean === 0 ? 0 : Math.sqrt(variance) / mean;
}

function runSample(spec) {
  const child = spawnSync(
    process.execPath,
    ['--expose-gc', __filename, '--worker', JSON.stringify(spec)],
    {
      encoding: 'utf8',
      maxBuffer: 1024 * 1024,
    },
  );
  if (child.status !== 0) {
    throw new Error(
      `boundary benchmark worker failed for ${spec.arm}: ${
        child.stderr.trim() || child.stdout.trim() || `exit ${child.status}`
      }`,
    );
  }
  return JSON.parse(child.stdout);
}

function summarizeSamples(spec, samples) {
  const singleSinks = new Set(samples.map((sample) => sample.single_sink));
  if (singleSinks.size !== 1) {
    throw new Error(
      `boundary benchmark checksum changed between samples for ${spec.arm}`,
    );
  }
  const operations = samples.map((sample) => sample.operation_ns);
  const totals = samples.map((sample) => sample.total_ns);
  const serializations = samples.map((sample) => sample.serialize_ns);
  const operationMedian = median(operations);
  const totalMedian = median(totals);
  const values = spec.rows * spec.columns;
  return {
    kind: spec.kind,
    scenario: `${spec.rows.toLocaleString('en-US')} rows x ${spec.columns} columns`,
    arm: spec.arm,
    label: armLabels[spec.arm],
    rows: spec.rows,
    columns: spec.columns,
    values,
    operation_ns: operationMedian,
    serialize_ns: median(serializations),
    total_ns: totalMedian,
    p90_ns: percentile(operations, 0.9),
    cv: coefficientOfVariation(operations),
    rows_per_second: (spec.rows * 1e9) / operationMedian,
    values_per_second: (values * 1e9) / operationMedian,
    end_to_end_rows_per_second: (spec.rows * 1e9) / totalMedian,
    wire_bytes: median(samples.map((sample) => sample.wire_bytes)),
    max_rss_kib: Math.max(...samples.map((sample) => sample.max_rss_kib)),
    operation_repetitions: median(
      samples.map((sample) => sample.operation_repetitions),
    ),
    single_sink: samples[0].single_sink,
    operation_samples_ns: operations,
    total_samples_ns: totals,
    post_gc_samples_ns: samples.map((sample) => sample.post_gc_ns),
  };
}

function benchmarkSpec(spec, sampleCount) {
  const samples = [];
  for (let sample = 0; sample < sampleCount; ++sample) {
    samples.push(runSample(spec));
  }
  return summarizeSamples(spec, samples);
}

function runBoundaryBenchmarks(options = {}) {
  const sampleCount =
    options.sampleCount ??
    Number.parseInt(process.env.V8SERIAL_BENCH_SAMPLES ?? '5', 10);
  const macroRows =
    options.macroRows ??
    Number.parseInt(process.env.V8SERIAL_BENCH_ROWS ?? '100000', 10);
  const largeRows =
    options.largeRows ??
    Number.parseInt(process.env.V8SERIAL_BENCH_LARGE_ROWS ?? '1000000', 10);
  if (!Number.isInteger(sampleCount) || sampleCount < 1) {
    throw new Error('V8SERIAL_BENCH_SAMPLES must be a positive integer');
  }
  if (!Number.isInteger(macroRows) || macroRows < 1) {
    throw new Error('V8SERIAL_BENCH_ROWS must be a positive integer');
  }
  if (!Number.isInteger(largeRows) || largeRows < 0) {
    throw new Error(
      'V8SERIAL_BENCH_LARGE_ROWS must be a non-negative integer',
    );
  }

  const arityValues = options.arityValues ?? 500000;
  const arityRows = options.arityRows ?? 50000;
  const arities = [1, 5, 12, 32];
  const specs = [];
  for (const columns of arities) {
    const rows = Math.max(1, Math.floor(arityValues / columns));
    specs.push(
      { kind: 'arity', arm: 'per-cell', rows, columns },
      { kind: 'arity', arm: 'per-row', rows, columns },
      {
        kind: 'arity-fixed-calls',
        arm: 'per-row',
        rows: arityRows,
        columns,
      },
    );
  }

  const bulkArms = [
    'per-cell',
    'per-row',
    'bulk-array-generic',
    'bulk-array-optimized',
    'bulk-object',
    'serialized-tree',
    'serialized-stream-copy',
    'serialized-stream-borrow',
    'native',
  ];
  for (const columns of [5, 12]) {
    for (const arm of bulkArms) {
      specs.push({ kind: 'bulk', arm, rows: macroRows, columns });
    }
  }

  if (largeRows > 0) {
    for (const arm of [
      'per-cell',
      'per-row',
      'bulk-array-generic',
      'bulk-array-optimized',
      'serialized-stream-copy',
      'serialized-stream-borrow',
      'native',
    ]) {
      specs.push({ kind: 'large', arm, rows: largeRows, columns: 5 });
    }
  }

  const results = specs.map((spec) => benchmarkSpec(spec, sampleCount));
  const sinksByDataset = new Map();
  for (const result of results) {
    const dataset = `${result.rows}:${result.columns}`;
    const expected = sinksByDataset.get(dataset);
    if (expected !== undefined && expected !== result.single_sink) {
      throw new Error(
        `boundary benchmark arms produced different checksums for ${result.rows} rows x ${result.columns} columns`,
      );
    }
    sinksByDataset.set(dataset, result.single_sink);
  }

  return {
    environment: {
      generated_at: new Date().toISOString(),
      platform: process.platform,
      arch: process.arch,
      cpu: os.cpus()[0]?.model ?? 'unknown CPU',
      node: process.version,
      v8: process.versions.v8,
      samples: sampleCount,
      macro_rows: macroRows,
      large_rows: largeRows,
      arity_values: arityValues,
      arity_rows: arityRows,
    },
    results,
  };
}

function printBoundaryTable(report) {
  console.table(
    report.results.map((result) => ({
      kind: result.kind,
      rows: result.rows,
      columns: result.columns,
      arm: result.label,
      'rows/s': Math.round(result.rows_per_second).toLocaleString('en-US'),
      'values/s': Math.round(result.values_per_second).toLocaleString('en-US'),
      'CV %': (result.cv * 100).toFixed(1),
      'peak MiB': (result.max_rss_kib / 1024).toFixed(1),
    })),
  );
}

if (process.argv[2] === '--worker') {
  try {
    const spec = JSON.parse(process.argv[3]);
    process.stdout.write(JSON.stringify(runWorker(spec)));
  } catch (error) {
    console.error(error.stack ?? error.message);
    process.exitCode = 1;
  }
} else if (require.main === module) {
  const report = runBoundaryBenchmarks();
  printBoundaryTable(report);
}

module.exports = {
  makeRows,
  runBoundaryBenchmarks,
};
