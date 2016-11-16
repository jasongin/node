'use strict';

const util = require('util');
const CategoryEventEmitter = require('events').CategoryEventEmitter;

const tracing = new Tracing();

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

  this.tracing = tracing;
  this.logTracingEvents = true;
  this.traceLogMessages = false;
  this.defaultTracingCategory = 'nodejs';

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
  let message = `${util.format.apply(null, args)}\n`;
  this._stdout.write(message);

  if (this.traceLogMessages) {
    this.tracing._emitTracingEvent('instant', 'log', null, this.defaultTracingCategory,
      { message });
  }
};


Console.prototype.info = function info(...args) {
  let message = `${util.format.apply(null, args)}\n`;
  this._stdout.write(message);

  if (this.traceLogMessages) {
    this.tracing._emitTracingEvent('instant', 'info', null, this.defaultTracingCategory,
      { message });
  }
};


Console.prototype.warn = function warn(...args) {
  let message = `${util.format.apply(null, args)}\n`;
  this._stderr.write(message);

  if (this.traceLogMessages) {
    this.tracing._emitTracingEvent('instant', 'warn', null, this.defaultTracingCategory,
      { message });
  }
};


Console.prototype.error = function error(...args) {
  let message = `${util.format.apply(null, args)}\n`;
  this._stderr.write(message);

  if (this.traceLogMessages) {
    this.tracing._emitTracingEvent('instant', 'error', null, this.defaultTracingCategory,
      { message });
  }
};


Console.prototype.dir = function dir(object, options) {
  options = Object.assign({customInspect: false}, options);
  let message = `${util.inspect(object, options)}\n`;
  this._stdout.write(message);

  if (this.traceLogMessages) {
    this.tracing._emitTracingEvent('instant', 'dir', null, this.defaultTracingCategory,
      { message });
  }
};


Console.prototype.time = function time(label) {
  this._times.set(label, process.hrtime());
};


Console.prototype.time = function time(name, id, category, args) {
  this._times.set(name, process.hrtime());
  this.tracing._emitTracingEvent('begin', name, id, category || this.defaultTracingCategory, args);
};


Console.prototype.timeEnd = function timeEnd(name, id, category, args) {
  const time = this._times.get(name);
  if (!time) {
    process.emitWarning(`No such label '${name}' for console.timeEnd()`);
    return;
  }

  if (this.logTracingEvents) {
    const duration = process.hrtime(time);
    const ms = duration[0] * 1000 + duration[1] / 1e6;
    this.log('%s: %sms', name, ms.toFixed(3));
  }

  this._times.delete(name);
  this.tracing._emitTracingEvent('end', name, id, category || this.defaultTracingCategory, args);
};


Console.prototype.timeStamp = function(name, category, args) {
  if (this.logTracingEvents) {
    const hrtime = process.hrtime();
    const ms = hrtime[0] * 1000 + hrtime[1] / 1e6;
    this.log('%s: %s', name, ms.toFixed(3));
  }

  this.tracing._emitTracingEvent('instant', name, null,
    category || this.defaultTracingCategory, args);
};


Console.prototype.count = function(name, value, category) {
  if (value) {
    if (this.logTracingEvents) {
      if (typeof value === "object") {
        this.log('%s: %s', name, JSON.stringify(value));
      } else if (typeof value === "number") {
        this.log('%s: %s', name, value);
      }
    }
  } else {
    value = (this._counts.get(name) || 0) + 1;
    if (this.logTracingEvents) {
        this.log('%s: %s', name, value);
    }
    this._counts.set(name, value);
  }

  this.tracing._emitTracingEvent('count', name, null,
    category || this.defaultTracingCategory, value);
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
  EventEmitter.call(this);

  this._binding = process.binding('trace');

  // Update the binding when listeners are added or removed.
  this.on('newListener', function(category) {
    this._binding.addTraceListener(category);
  });
  this.on('removeListener', function(category) {
    this._binding.removeTraceListener(category);
  });

  // bind the prototype functions to this Tracing instance
  var keys = Object.keys(Tracing.prototype);
  for (var v = 0; v < keys.length; v++) {
    var k = keys[v];
    this[k] = this[k].bind(this);
  }

  this._binding.onchange = this._onTracingCategoriesChanged;
  this._binding.ontrace = this._onTracingEvent;
}

util.inherits(Tracing, CategoryEventEmitter);


Tracing.prototype.emit = function emit(category, tracingEvent) {
  if (!tracingEvent && typeof category === "object") {
    // Allow just a trace event as a single argument to the emit() function.
    tracingEvent = category;
  } else {
    tracingEvent.category = category;
  }

  this._emitTracingEvent(
    tracingEvent.eventType,
    tracingEvent.name,
    tracingEvent.id,
    tracingEvent.category,
    tracingEvent.eventType === 'count' ? value : tracingEvent.args,
    tracingEvent.timestamp);
};


Tracing.prototype._emitTracingEvent = function _emitTracingEvent(
    eventType, name, id, category, args, timestamp) {
  if (this.isEnabled(category)) {
    this._binding.emitTracingEvent(eventType, name, id, category, args, timestamp);
  }
};


Tracing.prototype._onTracingEvent = function _onTracingEvent(tracingEvent) {
  this.emit(tracingEvent.category, tracingEvent);
};


Tracing.prototype._onTracingCategoriesChanged = function _onTracingCategoriesChanged() {
  // TODO: Update the set of tracing-enabled categories.
};

Tracing.prototype.isEnabled = function isEnabled(category) {
  // TODO: Check the tracing-enabled categories
  if (Array.isArray(category)) {

  } else {

  }
  return false;
}

Tracing.prototype.flush = function flush(callback) {
  // TODO: Flush trace logs, with optional callback.
};

module.exports = new Console(process.stdout, process.stderr);
module.exports.Console = Console;
