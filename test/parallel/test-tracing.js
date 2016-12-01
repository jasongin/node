'use strict';

require('../common');
const assert = require('assert');

console.timeStamp('test');
console.timeStamp('test1', ['one']);
console.timeStamp('test2', ['one', 'two']);

console.tracing.enableRecording(['three', 'four']);
console.log('Recording categories: ' + console.tracing.recordingCategories());
