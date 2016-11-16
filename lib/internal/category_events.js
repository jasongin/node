'use strict';

// A CategoryEventEmitter is like an EventEmitter except it supports emitting events to
// multiple "categories" simultaneously, instead of to a single event name. Correspondingly,
// one listener may listen to multiple event categories without risk of being invoked
// more than once per event.
function CategoryEventEmitter() {
    this._listeners = [];
}
module.exports = CategoryEventEmitter;

CategoryEventEmitter.prototype._listeners = undefined;

CategoryEventEmitter.prototype.emit = function emit(category) {
  if (typeof category === 'string') {
    category = [category];
  } else if (!Array.isArray(category)) {
    throw new TypeError('"category" argument must be a string or string array');
  }

  const forEachListenerWithCategory = (listenerAction) => {
    var found = false;
    this._listeners.forEach((listenerInfo) => {
      for (var i = 0; i < category.length; i++) {
        if (listenerInfo.categories[category[i]]) {
          listenerAction(listenerInfo.listener);
          found = true;
          break;
        }
      }
    });
    return found;
  }

  var len = arguments.length;
  switch (len) {
    // fast cases
    case 1:
      return forEachListenerWithCategory((listener) => {
        listener.call(this);
      });
    case 2:
      return forEachListenerWithCategory((listener) => {
        listener.call(this, arguments[1]);
      });
    // slower
    default:
      return forEachListenerWithCategory((listener) => {
        listener.apply(this, arguments.slice(1));
      });
  }
};

CategoryEventEmitter.prototype._emitMetaEvent =
function _emitMetaEvent(name, value) {
  // Prevent subclasses from overriding the meta-event emit behavior.
  CategoryEventEmitter.prototype.emit.call(this, name, value);
}

CategoryEventEmitter.prototype.addListener =
function addListener(category, listener) {
  if (typeof category === 'string') {
    category = [category];
  } else if (!Array.isArray(category)) {
    throw new TypeError(
      '"category" argument must be a string or string array');
  }

  if (typeof listener !== 'function')
    throw new TypeError('"listener" argument must be a function');

  // Filter out empty-string categories.
  category = category.filter((c) => c);
  if (category.length === 0) {
    return this;
  }

  // Emit events for any completely-new categories.
  category.forEach((c) => {
    if (!this._listeners.find((info) => info.categories[c]) &&
      !isMetaCategoryName(c)) {
        this._emitMetaEvent('newListenerCategory', c);
      }
  });

  // Check if the listener was already added to the listeners array.
  var listenerInfo = this._listeners.find((info) => info.listener === listener);
  if (!listenerInfo) {
    this._emitMetaEvent('newListener', listener);

    listenerInfo = {
      listener,
      categories: {},
    };
    this._listeners.push(listenerInfo);
  }

  // Ensure each of the categories is enabled for the listener.
  category.forEach((c) => {
    listenerInfo.categories[c] = true;
  });

  return this;
};

CategoryEventEmitter.prototype.on = CategoryEventEmitter.prototype.addListener;

CategoryEventEmitter.prototype.removeListener =
function removeListener(category, listener) {
  if (typeof category === 'string') {
    category = [category];
  } else if (!Array.isArray(category)) {
    throw new TypeError('"category" argument must be a string or string array');
  }

  if (typeof listener !== 'function')
    throw new TypeError('"listener" argument must be a function');

  // Check if the listener is in the listeners array.
  var position = this._listeners.findIndex(
    (listenerInfo) => listenerInfo.listener === listener);
  if (position < 0) {
    return this;
  }

  var listenerInfo = this._listeners[position];

  // Remove each of the specified categories for the listener.
  category.forEach((c) => {
    delete listenerInfo.categories[c];
  });

  // Remove the listener if it's not listening to any categories now.
  if (Object.keys(listenerInfo.categories).length === 0) {
    this._listeners.splice(position, 1);
    this._emitMetaEvent('removeListener', listener);
  }

  // Emit events for any completely-removed categories.
  category.forEach((c) => {
    if (!this._listeners.find((info) => info.categories[c]) &&
      !isMetaCategoryName(c)) {
        this._emitMetaEvent('removeListenerCategory', c);
      }
  });

  return this;
};

CategoryEventEmitter.prototype.removeAllListeners =
function removeAllListeners(category) {
  let removedCategories;

  if (category) {
    if (typeof category === 'string') {
      category = [category];
    } else if (!Array.isArray(category)) {
      throw new TypeError(
          '"category" argument must be a string or string array');
    }

    removedCategories = category;
  } else {
    removedCategories = this.listenerCategories();
  }

  // Remove listeners in LIFO order
  for (var i = this._listeners.length - 1; i >= 0; i--) {
    var listenerInfo = this._listeners[i];

    let remove;
    if (category) {
      // Remove each of the specified categories for the listener.
      category.forEach((c) => {
        delete listenerInfo.categories[c];
      });

      // Remove the listener if it's not listening to any categories now.
      remove = Object.keys(listenerInfo.categories).length === 0
    } else {
      remove = true;
    }

    if (remove) {
      this._listeners.splice(i, 1);
      this._emitMetaEvent('removeListener', listenerInfo.listener);
    }
  }

  // Emit events for removed categories.
  removedCategories.forEach((c) => {
    if (!isMetaCategoryName(c)) {
      this._emitMetaEvent('removeListenerCategory', c);
    }
  });

  return this;
};

CategoryEventEmitter.prototype.listeners = function listeners(category) {
  if (category) {
    if (typeof category === 'string') {
      category = [category];
    } else if (!Array.isArray(category)) {
      throw new TypeError(
        '"category" argument must be a string or string array');
    }
  }

  var result = [];

  this._listeners.forEach((listenerInfo) => {
    if (category) {
      for (var i = 0; i < category.length; i++) {
        if (listenerInfo.categories[category[i]]) {
          result.push(listenerInfo.listener);
          break;
        }
      }
    } else {
      result.push(listenerInfo.listener);
    }
  });

  return result;
};

CategoryEventEmitter.prototype.listenerCount =
function listenerCount(category) {
  if (category) {
    if (typeof category === 'string') {
      category = [category];
    } else if (!Array.isArray(category)) {
      throw new TypeError(
        '"category" argument must be a string or string array');
    }
  }

  var count = 0;

  if (category) {
    this._listeners.forEach((listenerInfo) => {
      for (var i = 0; i < category.length; i++) {
        if (listenerInfo.categories[category[i]]) {
          count++;
          break;
        }
      }
    });
  } else {
    count = this._listeners.length;
  }

  return count;
};

CategoryEventEmitter.prototype.listenerCategories =
function listenerCategories() {
  var allCategories = {};

  this._listeners.forEach((listenerInfo) => {
    Object.keys(listenerInfo.categories).forEach((c) => {
      allCategories[c] = true;
    });
  });

  return Object.keys(allCategories);
};

function isMetaCategoryName(c) {
  return c === 'newListener' || c === 'removeListener' ||
    c === 'newListenerCategory' || c === 'removeListenerCategory';
}
