'use strict';

const v8 = require('node:v8');
const addon = require('./build/Release/v8serial.node');

const runtimeVersion = v8.serialize(undefined)[1];
if (runtimeVersion !== addon.formatVersion) {
  throw new Error(
    `v8serial format ${addon.formatVersion} does not match runtime format ${runtimeVersion}`,
  );
}

module.exports = addon;
