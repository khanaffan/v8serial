'use strict';

const v8 = require('node:v8');

const cases = [
  ['undefined', undefined],
  ['null', null],
  ['true', true],
  ['false', false],
  ['int32', 42],
  ['negative int32', -42],
  ['double', 1.5],
  ['latin1 string', 'widget'],
  ['utf16 string', '\u0100'],
  ['long utf16 string', '\u0100'.repeat(64)],
  ['empty object', {}],
  ['object', { id: 42, name: 'widget' }],
  ['empty array', []],
  ['dense array', [null, true, 42]],
  ['array buffer', Uint8Array.from([1, 2, 3]).buffer],
  ['uint8 array', Uint8Array.from([1, 2, 3])],
  ['uint8 subarray', Uint8Array.from([9, 1, 2, 3, 9]).subarray(1, 4)],
];

for (const [name, value] of cases) {
  console.log(`${name.padEnd(20)} ${v8.serialize(value).toString('hex')}`);
}
