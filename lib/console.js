'use strict';

const util = require('util');

const consoleTracingCategory = 'console';

// From trace_event_common.h
const tracingEventTypeInstant = 'I'.charCodeAt(0);
const tracingEventTypeAsyncBegin = 'S'.charCodeAt(0);
const tracingEventTypeAsyncEnd = 'F'.charCodeAt(0);
const tracingEventTypeCounter = 'C'.charCodeAt(0);

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

  prop.writable = false;
  prop.value = {
    logTracingEvents: true,
    traceLogMessages: false,
  };
  Object.defineProperty(this, 'options', prop);

  // Define a getter for a _tracing member that lazily-initializes the tracing module.
  Object.defineProperty(this, '_tracing', {
    enumerable: false,
    configurable: true,
    get: function () {
      let tracing = require('tracing');
      Object.defineProperty(this, '_tracing', {
        enumerable: false,
        configurable: true,
        value: tracing,
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
  const message = util.format.apply(null, args);
  this._stdout.write(message + '\n');

  if (this.options.traceLogMessages) {
    if (this._tracing.isEnabled(consoleTracingCategory)) {
      this._tracing._bindingEmit(
        tracingEventTypeInstant, consoleTracingCategory, 'log', null, { message });
    }
  }
};


Console.prototype.info = function info(...args) {
  const message = util.format.apply(null, args);
  this._stdout.write(message + '\n');

  if (this.options.traceLogMessages && this._tracing.isEnabled(consoleTracingCategory)) {
    this._tracing._bindingEmit(
      tracingEventTypeInstant, consoleTracingCategory, 'info', null, { message });
  }
};


Console.prototype.warn = function warn(...args) {
  const message = util.format.apply(null, args);
  this._stderr.write(message + '\n');

  if (this.options.traceLogMessages && this._tracing.isEnabled(consoleTracingCategory)) {
    this._tracing._bindingEmit(
      tracingEventTypeInstant, consoleTracingCategory, 'warn', null, { message });
  }
};


Console.prototype.error = function error(...args) {
  const message = util.format.apply(null, args);
  this._stderr.write(message + '\n');

  if (this.options.traceLogMessages && this._tracing.isEnabled(consoleTracingCategory)) {
    this._tracing._bindingEmit(
      tracingEventTypeInstant, consoleTracingCategory, 'error', null, { message });
  }
};


Console.prototype.dir = function dir(object, options) {
  options = Object.assign({customInspect: false}, options);
  const message = util.inspect(object, options);
  this._stdout.write(message + '\n');

  if (this.options.traceLogMessages && this._tracing.isEnabled(consoleTracingCategory)) {
    this._tracing._bindingEmit(
      tracingEventTypeInstant, consoleTracingCategory, 'dir', null, { message });
  }
};


Console.prototype.time = function time(label) {
  this._times.set(label, process.hrtime());
};


Console.prototype.time = function time(name, category) {
  this._times.set(name, process.hrtime());

  if (!category) category = consoleTracingCategory;
  if (this._tracing.isEnabled(category)) {
    this._tracing._bindingEmit(tracingEventTypeAsyncBegin, category, name);
  }
};


Console.prototype.timeEnd = function timeEnd(name, category) {
  const time = this._times.get(name);
  if (!time) {
    process.emitWarning(`No such label '${name}' for console.timeEnd()`);
    return;
  }

  if (this.options.logTracingEvents) {
    const duration = process.hrtime(time);
    const ms = duration[0] * 1000 + duration[1] / 1e6;
    this.log('%s: %sms', name, ms.toFixed(3));
  }

  this._times.delete(name);

  if (!category) category = consoleTracingCategory;
  if (this._tracing.isEnabled(category)) {
    this._tracing._bindingEmit(tracingEventTypeAsyncEnd, category, name);
  }
};


Console.prototype.timeStamp = function(name, category) {
  if (this.options.logTracingEvents) {
    const hrtime = process.hrtime();
    const ms = hrtime[0] * 1000 + hrtime[1] / 1e6;
    this.log('%s: %s', name, ms.toFixed(3));
  }

  if (!category) category = consoleTracingCategory;
  if (this._tracing.isEnabled(category)) {
    this._tracing._bindingEmit(tracingEventTypeInstant, category, name);
  }
};


Console.prototype.count = function(name, category) {
  let value = (this._counts.get(name) || 0) + 1;
  if (this.options.logTracingEvents) {
    this.log('%s: %s', name, value);
  }
  this._counts.set(name, value);

  if (!category) category = consoleTracingCategory;
  if (this._tracing.isEnabled(category)) {
    this._tracing._bindingEmit(tracingEventTypeCounter, category, name, null, value);
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


module.exports = new Console(process.stdout, process.stderr);
module.exports.Console = Console;
