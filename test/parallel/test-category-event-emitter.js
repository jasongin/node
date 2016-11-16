// Flags: --expose_internals
'use strict';

require('../common');
const assert = require('assert');
const CategoryEventEmitter = require('internal/category_events');

function testEmitCategoryEvent(listenCategory, emitCategory, expected) {
  const message = 'listen=[' + listenCategory + '], ' +
    'emit=[' + emitCategory + '], expected=' + expected;

  const ee = new CategoryEventEmitter();
  const testEvent = { test: true };
  let receivedEvent = null;
  let receivedNewListener = null;
  let receivedRemoveListener = null;
  let receivedNewCategories = [];
  let receivedRemoveCategories = [];

  const listener = (e) => {
    assert.ok(e);
    assert.equal(receivedEvent, null);
    receivedEvent = e;
  };
  const listener2 = (e) => {
    assert.fail(e, null, 'Listener 2 should not receive an event.');
  };

  const newListenerListener = (l) => {
    assert.ok(l);
    assert.notEqual(l, receivedNewListener);
    receivedNewListener = l;
  };
  const removeListenerListener = (l) => {
    assert.ok(l);
    assert.notEqual(l, receivedRemoveListener);
    receivedRemoveListener = l;
  };
  ee.on('removeListener', removeListenerListener);
  ee.on('newListener', newListenerListener);

  const newCategoryListener = (c) => {
    assert.ok(c);
    assert.ok(receivedNewCategories.indexOf(c) < 0);
    receivedNewCategories.push(c);
  };
  const removeCategoryListener = (c) => {
    assert.ok(c);
    assert.ok(receivedRemoveCategories.indexOf(c) < 0);
    receivedRemoveCategories.push(c);
  };
  ee.on('removeListenerCategory', removeCategoryListener);
  ee.on('newListenerCategory', newCategoryListener);
  receivedNewListener = null;

  assert.equal(ee.listenerCount(listenCategory), 0, message);

  ee.on(listenCategory, listener);
  const expectedCount = (listenCategory.length === 0 ? 0 : 1);
  assert.equal(ee.listenerCount(listenCategory), expectedCount, message);
  assert.equal(receivedNewListener, listenCategory.length === 0 ? null : listener);
  assert.deepEqual(receivedNewCategories,
    Array.isArray(listenCategory) ? listenCategory : [listenCategory]);

  ee.on(['other'], listener2);
  assert.equal(ee.listenerCount(listenCategory), expectedCount, message);
  assert.equal(receivedNewListener, listener2);
  assert.deepEqual(receivedNewCategories,
    (Array.isArray(listenCategory) ? listenCategory : [listenCategory]).concat('other'));

  ee.emit(emitCategory, testEvent);
  assert.equal(receivedEvent, expected ? testEvent : null, message);

  ee.removeListener(listenCategory, listener);
  assert.equal(ee.listenerCount(listenCategory), 0, message);
  assert.equal(receivedRemoveListener, listenCategory.length === 0 ? null : listener);
  assert.deepEqual(receivedRemoveCategories,
    Array.isArray(listenCategory) ? listenCategory : [listenCategory]);
}

// Test that listeners receive events according to categories as expected.
testEmitCategoryEvent('test', 'test', true);
testEmitCategoryEvent('test1', ['test1', 'test2'], true);
testEmitCategoryEvent(['test1'], ['test1', 'test2'], true);
testEmitCategoryEvent(['test1', 'test2'], 'test1', true);
testEmitCategoryEvent(['test1', 'test2'], ['test1'], true);
testEmitCategoryEvent(['test1', 'test2'], ['test1', 'test2'], true);
testEmitCategoryEvent('test1', 'test2', false);
testEmitCategoryEvent('test', [], false);
testEmitCategoryEvent([], 'test', false);
testEmitCategoryEvent([], [], false);

// Test that per-category listener counts are correct when adding and removing.
var ee = new CategoryEventEmitter();
var listener = (e) => {};
ee.on('test1', listener);
ee.on('test2', listener);
assert.equal(ee.listenerCount('test1'), 1);
assert.equal(ee.listenerCount('test2'), 1);
assert.equal(ee.listenerCount(['test1', 'test2']), 1);
assert.equal(ee.listenerCount(['test1', 'test2', 'test3']), 1);
assert.equal(ee.listenerCount('test3'), 0);
ee.removeAllListeners('test3');
assert.equal(ee.listenerCount('test1'), 1);
ee.removeListener('test3', listener);
assert.equal(ee.listenerCount('test1'), 1);
ee.removeAllListeners('test1');
assert.equal(ee.listenerCount('test1'), 0);
assert.equal(ee.listenerCount('test2'), 1);
ee.removeAllListeners(['test1', 'test2']);
assert.equal(ee.listenerCount('test2'), 0);
assert.equal(ee.listenerCount(), 0);
ee.on(['test1', 'test2'], listener);
assert.equal(ee.listenerCount(), 1);
assert.equal(ee.listenerCount('test1'), 1);
assert.equal(ee.listenerCount('test2'), 1);
ee.removeAllListeners();
assert.equal(ee.listenerCount(), 0);
assert.equal(ee.listenerCount('test1'), 0);
assert.equal(ee.listenerCount('test2'), 0);
