'use strict';

const util = require('util');
const CategoryEventEmitter = require('internal/category_events');

var tracing = undefined;

function Console(stdout, stderr) {
  if (!(this instanceof Console)) {
    return new Console(stdout, stderr);
  }
  if (!stdout || typeof stdout.write !== 'function') {
    throw new TypeError('Console expects a writable stream instance');
  }
  if (!stderr) {
    stderr = stdout;
  } else if (typeof stderr.write !== 'function') {
    throw new TypeError('Console expects writable stream instances');
  }

  var prop = {
    writable: true,
    enumerable: false,
    configurable: true
  };
  prop.value = stdout;
  Object.defineProperty(this, '_stdout', prop);
  prop.value = stderr;
  Object.defineProperty(this, '_stderr', prop);
  prop.value = new Map();
  Object.defineProperty(this, '_times', prop);
  prop.value = new Map();
  Object.defineProperty(this, '_counts', prop);

  prop.value = false;
  Object.defineProperty(this, '_traceConsoleLogMessages', prop);

  // Define a getter for tracing that dynamically initializes the tracing module and then replaces
  // the _traceConsoleLogMessages property with a getter that redirects to tracing.options.
  Object.defineProperty(this, 'tracing', {
    enumerable: false,
    configurable: true,
    get: function () {
      if (!tracing) {
        tracing = new Tracing();
      }
      Object.defineProperty(this, 'tracing', {
        enumerable: false,
        configurable: true,
        value: tracing,
      });
      Object.defineProperty(this, '_traceConsoleLogMessages', {
        enumerable: false,
        configurable: true,
        get: function () { return this.tracing.options.traceConsoleLogMessages; }
      });
      return tracing;
    },
  });

  // bind the prototype functions to this Console instance
  var keys = Object.keys(Console.prototype);
  for (var v = 0; v < keys.length; v++) {
    var k = keys[v];
    this[k] = this[k].bind(this);
  }
}

// As of v8 5.0.71.32, the combination of rest param, template string
// and .apply(null, args) benchmarks consistently faster than using
// the spread operator when calling util.format.
Console.prototype.log = function log(...args) {
  const message = `${util.format.apply(null, args)}\n`;
  this._stdout.write(message);

  if (this._traceConsoleLogMessages) {
    let category = this.tracing.options.defaultConsoleTracingCategory;
    if (this.tracing.isEnabled(category)) {
      this.tracing._binding.emitInstantEvent(
        'log', null, category, { message });
    }
  }
};


Console.prototype.info = function info(...args) {
  const message = `${util.format.apply(null, args)}\n`;
  this._stdout.write(message);

  if (this._traceConsoleLogMessages) {
    let category = this.tracing.options.defaultConsoleTracingCategory;
    if (this.tracing.isEnabled(category)) {
      this.tracing._binding.emitInstantEvent(
        'info', null, category, { message });
    }
  }
};


Console.prototype.warn = function warn(...args) {
  const message = `${util.format.apply(null, args)}\n`;
  this._stderr.write(message);

  if (this._traceConsoleLogMessages) {
    let category = this.tracing.options.defaultConsoleTracingCategory;
    if (this.tracing.isEnabled(category)) {
      this.tracing._binding.emitInstantEvent(
        'warn', null, category, { message });
    }
  }
};


Console.prototype.error = function error(...args) {
  const message = `${util.format.apply(null, args)}\n`;
  this._stderr.write(message);

  if (this._traceConsoleLogMessages) {
    let category = this.tracing.options.defaultConsoleTracingCategory;
    if (this.tracing.isEnabled(category)) {
      this.tracing._binding.emitInstantEvent(
        'error', null, category, { message });
    }
  }
};


Console.prototype.dir = function dir(object, options) {
  options = Object.assign({customInspect: false}, options);
  const message = `${util.inspect(object, options)}\n`;
  this._stdout.write(message);

  if (this._traceConsoleLogMessages) {
    let category = this.tracing.options.defaultConsoleTracingCategory;
    if (this.tracing.isEnabled(category)) {
      this.tracing._binding.emitInstantEvent(
        'dir', null, category, { message });
    }
  }
};


Console.prototype.time = function time(label) {
  this._times.set(label, process.hrtime());
};


Console.prototype.time = function time(name, id, category, args) {
  this._times.set(name, process.hrtime());

  if (!category) category = this.tracing.options.defaultConsoleTracingCategory;
  if (this.tracing.isEnabled(category)) {
    this.tracing._binding.emitBeginEvent(name, id, category, args);
  }
};


Console.prototype.timeEnd = function timeEnd(name, id, category, args) {
  const time = this._times.get(name);
  if (!time) {
    process.emitWarning(`No such label '${name}' for console.timeEnd()`);
    return;
  }

  if (this.tracing.options.logConsoleTracingEvents) {
    const duration = process.hrtime(time);
    const ms = duration[0] * 1000 + duration[1] / 1e6;
    this.log('%s: %sms', name, ms.toFixed(3));
  }

  this._times.delete(name);

  if (!category) category = this.tracing.options.defaultConsoleTracingCategory;
  if (this.tracing.isEnabled(category)) {
    this.tracing._binding.emitEndEvent(name, id, category, args);
  }
};


Console.prototype.timeStamp = function(name, category, args) {
  if (this.tracing.options.logConsoleTracingEvents) {
    const hrtime = process.hrtime();
    const ms = hrtime[0] * 1000 + hrtime[1] / 1e6;
    this.log('%s: %s', name, ms.toFixed(3));
  }

  if (!category) category = this.tracing.options.defaultConsoleTracingCategory;
  if (this.tracing.isEnabled(category)) {
    this.tracing._binding.emitInstantEvent(name, null, category, args);
  }
};


Console.prototype.count = function(name, value, id, category) {
  if (value) {
    if (this.tracing.options.logConsoleTracingEvents) {
      if (typeof value === 'object') {
        this.log('%s: %s', name, JSON.stringify(value));
      } else if (typeof value === 'number') {
        this.log('%s: %s', name, value);
      }
    }
  } else {
    value = (this._counts.get(id || name) || 0) + 1;
    if (this.tracing.options.logConsoleTracingEvents) {
      this.log('%s: %s', name, value);
    }
    this._counts.set(id || name, value);
  }

  if (!category) category = this.tracing.options.defaultConsoleTracingCategory;
  if (this.tracing.isEnabled(category)) {
    this.tracing._binding.emitCountEvent(name, id, category, value);
  }
};


Console.prototype.trace = function trace(...args) {
  // TODO probably can to do this better with V8's debug object once that is
  // exposed.
  var err = new Error();
  err.name = 'Trace';
  err.message = util.format.apply(null, args);
  Error.captureStackTrace(err, trace);
  this.error(err.stack);
};


Console.prototype.assert = function assert(expression, ...args) {
  if (!expression) {
    require('assert').ok(false, util.format.apply(null, args));
  }
};


function Tracing() {
  CategoryEventEmitter.call(this);

  // bind the prototype functions to this Tracing instance
  var keys = Object.keys(Tracing.prototype);
  for (var v = 0; v < keys.length; v++) {
    var k = keys[v];
    this[k] = this[k].bind(this);
  }

  let options = {
    traceConsoleLogMessages: false,
    logConsoleTracingEvents: true,
    defaultConsoleTracingCategory: 'nodejs',
  };
  Object.defineProperty(this, 'options', { writable: false, value: options });

  this._binding = process.binding('tracing');
  this._binding.onchange = this._onTracingCategoriesChanged;
  this._binding.ontrace = this._onTracingEvent;

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


Tracing.prototype.emit = function emit(category, tracingEvent) {
  if (!tracingEvent && typeof category === 'object') {
    // Allow just a trace event as a single argument to the emit() function.
    tracingEvent = category;
    category = tracingEvent.category;
  }

  let bindingEmit;
  let args;
  switch (tracingEvent.eventType) {
    case 'begin':
      bindingEmit = this._binding.emitBeginEvent;
      args = tracingEvent.args;
      break;
    case 'end':
      bindingEmit = this._binding.emitEndEvent;
      args = tracingEvent.args;
      break;
    case 'instant':
      bindingEmit = this._binding.emitInstantEvent;
      args = tracingEvent.args;
      break;
    case 'count':
      bindingEmit = this._binding.emitCountEvent;
      args = tracingEvent.value;
      break;
    default: throw new TypeError('Event must include one of the eventType values: ' +
      'begin, end, instant, count');
  }

  if (this.isEnabled(category)) {
    bindingEmit(
      tracingEvent.name,
      tracingEvent.id,
      category,
      args,
      tracingEvent.timestamp);
  }
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


Tracing.prototype._onTracingCategoriesChanged =
function _onTracingCategoriesChanged() {
    this._enabledCategories = this._binding.getEnabledCategories();
};


Tracing.prototype.isEnabled = function isEnabled(category) {
  if (!this._enabledCategories) {
    this._enabledCategories = this._binding.getEnabledCategories();
  }

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


module.exports = new Console(process.stdout, process.stderr);
module.exports.Console = Console;
