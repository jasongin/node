'use strict';

const util = require('util');
const CategoryEventEmitter = require('internal/category_events');

// From trace_event_common.h
const tracingEventTypeInstant = 'I'.charCodeAt(0);
const tracingEventTypeAsyncBegin = 'S'.charCodeAt(0);
const tracingEventTypeAsyncEnd = 'F'.charCodeAt(0);
const tracingEventTypeCounter = 'C'.charCodeAt(0);

function Tracing() {
  CategoryEventEmitter.call(this);

  // bind the prototype functions to this Tracing instance
  var keys = Object.keys(Tracing.prototype);
  for (var v = 0; v < keys.length; v++) {
    var k = keys[v];
    this[k] = this[k].bind(this);
  }

  this._binding = process.binding('tracing');
  this._binding.onchange = this._onCategoriesChanged;
  this._binding.ontrace = this._onTracingEvent;
  this._enabledCategories = this._binding.getEnabledCategories();
  this._bindingEmit = this._binding.emit;

  this._addCategoryListeners();
}

util.inherits(Tracing, CategoryEventEmitter);


Tracing.prototype.removeAllListeners = function removeAllListeners(category) {
  CategoryEventEmitter.prototype.removeAllListeners.call(this, category);
  if (!category) {
    // If all listeners for all categories were removed, restore the category listeners.
    this._addCategoryListeners();
  }
}


Tracing.prototype.emit = function emit(category, tracingEvent, ...args) {
  if (!tracingEvent && typeof category === 'object') {
    // Allow just a tracing event object as a single argument to the emit() function.
    tracingEvent = category;
    category = tracingEvent.category;
  }

  // Return early if the category(s) are not enabled for tracing.
  // Instead of calling this.isEnabled(), this code is inlined for better performance.
  if (Array.isArray(category)) {
    if (!category.find(function (c) { return this[c]; }, this._enabledCategories)) return false;
  } else {
    if (!this._enabledCategories[category]) return false;
  }

  if (typeof tracingEvent === 'object' && tracingEvent !== null) {
    // An event object was supplied.
    let eventType;
    let eventArgs;
    switch (tracingEvent.eventType) {
      case 'begin':
        eventType = tracingEventTypeAsyncBegin;
        eventArgs = tracingEvent.args;
        break;
      case 'end':
        eventType = tracingEventTypeAsyncEnd;
        eventArgs = tracingEvent.args;
        break;
      case 'instant':
        eventType = tracingEventTypeInstant;
        eventArgs = tracingEvent.args;
        break;
      case 'count':
        eventType = tracingEventTypeCounter;
        eventArgs = tracingEvent.value;
        break;
      default: throw new TypeError('Tracing event must include one of the ' +
        'eventType values: begin, end, instant, count');
    }

    this._bindingEmit(
      eventType,
      category,
      tracingEvent.name,
      tracingEvent.id,
      eventArgs);
  } else if (typeof tracingEvent === 'string') {
    // Instead of an event object, the args can include a name (log level) optionally followed by
    // an args object, function that returns an args object, or formatted message.
    const name = tracingEvent;
    let eventArgs = undefined;
    if (args.length > 0) {
      if (typeof args[0] === 'object') {
        eventArgs = args[0];
      } else if (typeof args[0] === 'function') {
        eventArgs = args[0]();
      } else if (typeof args[0] === 'string') {
        eventArgs = { message: util.format.apply(null, args) };
      }
    }

    this._bindingEmit(
      tracingEventTypeInstant,
      category,
      name,
      null,
      eventArgs);
  } else {
    throw new TypeError('A tracing event object is required.');
  }

  return true;
};


Tracing.prototype._addCategoryListeners = function _addCategoryListeners() {
  // Update the binding when listener categories are added or removed.
  this.on('newListenerCategory', function(category) {
    this._binding.enableCategory(category, 4);
  });
  this.on('removeListenerCategory', function(category) {
    this._binding.enableCategory(category, 0);
  });
};


Tracing.prototype._onTracingEvent = function _onTracingEvent(tracingEvent) {
  CategoryEventEmitter.prototype.emit.call(this, tracingEvent.category, tracingEvent);
};


Tracing.prototype._onCategoriesChanged = function _onCategoriesChanged() {
    this._enabledCategories = this._binding.getEnabledCategories();
};


Tracing.prototype.isEnabled = function isEnabled(category) {
  if (Array.isArray(category)) {
    return !!category.find(function (c) { return this[c]; }, this._enabledCategories);
  } else {
    return !!this._enabledCategories[category];
  }
};


Tracing.prototype.recordingCategories = function recordingCategories() {
  if (!this._enabledCategories) {
    this._enabledCategories = this._binding.getEnabledCategories();
  }

  // Values in _enabledCategories are bit flags where 1 = recording, 4 = listening.
  return Object.keys(this._enabledCategories)
    .filter(function (c) { return this[c] & 1; }, this._enabledCategories);
};


Tracing.prototype.enableRecording = function enableRecording(category, enable) {
  if (typeof enable === 'undefined') {
    enable = true;
  }

  // This should trigger an onchange event that updates this._enabledCategories.
  this._binding.enableCategory(category, enable ? 1 : 0);
};

module.exports = new Tracing();
