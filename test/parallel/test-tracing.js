'use strict';

require('../common');
const assert = require('assert');
const tracing = require('tracing');

assert(tracing);

assert.deepEqual(tracing.recordingCategories(), []);

assert(!tracing.emit('one', 'test', 'Test single category disabled'));
assert(!tracing.emit(['one', 'two'], 'test', 'Test multi category disabled'));

tracing.enableRecording(['one', 'two']);
assert.deepEqual(tracing.recordingCategories().sort(), ['one', 'two'].sort());

assert(tracing.emit('one', 'test', 'Test single category enabled'));
assert(tracing.emit(['one', 'two'], 'test', 'Test multi category enabled'));

console.timeStamp('test');
console.timeStamp('test1', ['one']);
console.timeStamp('test2', ['one', 'two']);

tracing.enableRecording(['three', 'four']);
assert.deepEqual(tracing.recordingCategories().sort(), ['one', 'two', 'three', 'four'].sort());

assert(tracing.emit('four', 'test', 'Test fourth category enabled'));

tracing.enableRecording(['four'], false);
assert.deepEqual(tracing.recordingCategories().sort(), ['one', 'two', 'three'].sort());

assert(!tracing.emit('four', 'test', 'Test fourth category disabled'));
