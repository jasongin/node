'use strict';
require('../common');
var assert = require('assert');
var events = require('events');

function testEmitCategoryEvent(listenCategory, emitCategory, expected) {
    let ee = new events.CategoryEventEmitter();
    let receivedEvent = null;
    let testEvent = { test: true };

    let listener = e => {
        assert.equal(receivedEvent, null);
        receivedEvent = e;
    };

    assert.equal(ee.listenerCount(listenCategory), 0);
    ee.on(listenCategory, listener);
    assert.equal(ee.listenerCount(listenCategory), 1);

    ee.emit(emitCategory);
    assert.equal(receivedEvent, expected ? testEvent : null);

    ee.removeListener(listenCategory, lisener);
    assert.equal(ee.listenerCount(listenCategory), 0);
}

testEmitCategoryEvent('test', 'test', true);
testEmitCategoryEvent('test1', 'test2', false);
testEmitCategoryEvent('test', [], false);
testEmitCategoryEvent([], 'test', false);
testEmitCategoryEvent([], [], false);
testEmitCategoryEvent('test1', ['test1', 'test2'], true);
testEmitCategoryEvent(['test1'], ['test1', 'test2'], true);
testEmitCategoryEvent(['test1', 'test2'], 'test1', true);
testEmitCategoryEvent(['test1', 'test2'], ['test1'], true);
testEmitCategoryEvent(['test1', 'test2'], ['test1', 'test2'], true);
