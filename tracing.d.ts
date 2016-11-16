/*
Node.js Tracing API Proposal

This file uses TypeScript and JSDoc syntax to describe proposed JavaScript APIs for tracing in
Node.js. Most APIs are new, with the exception of some existing methods on the Console module
that have new optional parameters.

API design considerations:

1) The Tracing class is similar to an EventEmitter, but it does not extend EventEmitter because
   it has slightly different semantics. Unfortunately EventEmitter cannot be used directly due to
   the way tracing events may be emitted for multiple categories simultaneously.

   a) Are the multi-category event semantics easy enough to understand?

   b) An alternative design could actually use an EventEmitter subclass that has a single 'trace'
      event and a separate categories filter. Event consumers would then instantiate that class
      seperately for each different categories filter they want to use when listening. But that
      is probably even more confusing, because it would be very strange to emit events using one
      EventEmitter and have them somehow delivered to listeners of other EventEmitters.

2) The JavaScript Console module (in Node.js and the web) already defines a set of basic tracing
   methods that align well with the new tracing capabilities. (But note these methods are NOT part
   of any ES standard.) The existing implementation of those methods simply logs trace events to
   the console. This proposal extends those methods with new optional parameters to enable full
   integration with the tracing framework.

   a) Does this extension of the Console methods make sense? Or should they be left unchanged?

   b) The Tracing instance is exposed as a property on the Console module. Should it be accessed
      as a separate module instead? Most users of tracing will just be emitting trace events, so
      they can just use the Console trace methods and don't need to access the Tracing class
      directly, but tracing listeners will need to access methods on the Tracing class itself.

   c) There is an option to retain the legacy console-logging behavior of those methods in case any
      applications rely on it. Is that needed? Should it be on by default?

   d) There is an option to generate tracing events from console.log(), console.error() and similar
      methods. Is that needed? Should it be on by default?

3) Performance is critical - Emitting trace events should have minimal impact to an application,
   especially when tracing is not enabled for the event categories because there are no registered
   JavaScript or C++ listeners categories.

   a) Checking whether tracing is enabled for a category is expected to require a simple lookup in
      a JavaScript string-to-boolean dictionary. So when emitting an event with N categories, that
      will mean 1-N lookups to determine whether or not the tracing event should be forwarded to
      the C++ tracing API.

   b) The C++ tracing implementation uses comma-separated category lists to optimize lookup of
      category groups. But it would probably be too unnatural to require JavaScript code to provide
      comma-separated category lists (instead of arrays), and dynamically joining arrays into
      comma-separated lists would be extremely inefficient.
*/

/**
 * Defines the valid string values for the eventType property of a TracingEvent object.
 */
type TracingEventType = "begin" | "end" | "instant" | "count";

/**
 * Event object received by a TracingEventListener or supplied to the Tracing.emit() method.
 */
interface TracingEvent {
    /**
     * Required type of event; must be one of the allowed event type string values.
     */
    eventType: TracingEventType;

    /**
     * Event category or category list. May be omitted if the TracingEvent object is passed to
     * Tracing.emit() where the category is specified as a separate parameter.
     */
    category?: string | string[];

    /**
     * Required name of the counter or timer event.
     */
    name: string;

    /**
     * Optional string identifier that is used to correlate paired "begin" and "end"" events and
     * distinguish them from other events with the same name. Not used for other event types.
     */
    id?: string;

    /**
     * Optional specific time for any type of tracing event; if unspecified then the current time
     * is recorded.
     */
    timestamp?: Date;

    /**
     * Optional for "count" events; not used for other event types. If unspecified for a "count"
     * event, it increments the value of the single-value counter; if it's a number then it sets
     * the value of the single-value counter. For a multi-value counter, this is must be a
     * dictionary object containing 2 name-value pairs. (The tracing system currently requires
     * multi-value counters to have exactly two values.)
     */
    value?: number | { [name: string]: number };

    /**
     * Optional arguments for "begin", "end", and "instant" events. If specified, this must be a
     * dictionary containing 1 or 2 name-value pairs. (The tracing system currently supports up to
     * 2 arguments.) Not used for "count" events.
     */
    args?: { [name: string]: any };
}

/**
 * Listener interface for tracing events.
 */
interface TracingEventListener {
    (e: TracingEvent): void;
}

/**
 * Allows JavaScript code to emit and listen to tracing events. Non-JavaScript components may also
 * emit and listen to tracing events via the C++ APIs. Events emitted in C++ can be received by
 * JavaScript listeners; the converse is also true.
 *
 * This interface has methods that are similar to EventEmitter, with an important difference:
 * events may be emitted under multiple categories simultaneously (instead of a single event name),
 * and listeners may listen to multiple categories while still only being called once per event.
 */
interface Tracing {
    /**
     * Checks whether tracing is enabled for a category or for any categories in an array.
     * Note tracing for a category may be enabled even when listenerCount returns 0, if there are
     * non-JavaScript listeners.
     *
     * @param category Required tracing category name or array of category names.
     * @returns True if tracing is enabled for the category or for any of the categories in the
     * array.
     */
    isEnabled(category: string | string[]): boolean;

    /**
     * Emits a tracing event using an event object. This is an alternative to calling one of the
     * event-type-specific methods on the Console module to emit an event. (Events emitted via
     * this method are also not logged to the console.)
     *
     * @param e The event to be emitted. The event object must specify one or more categories.
     * @returns True if the event was emitted; false if the event was not emitted becase tracing
     * was not enabled for any of the event categories.
     */
    emit(e: TracingEvent): boolean;

    /**
     * Emits a tracing event using an event object, with categories specified separately. This is
     * an alternative to calling one of the event-type-specific methods on the Console module to
     * emit an event. (Events emitted via this method are also not logged to the console.)
     *
     * @param category Required tracing category name or array of one or more category names for
     * the tracing event. Overrides any categories specified in the event object.
     * @param e The event to be emitted.
     * @returns True if the event was emitted; false if the event was not emitted becase tracing
     * was not enabled for any of the event categories.
     */
    emit(category: string | string[], e: TracingEvent): boolean;

    /**
     * Alias for addListener.
     * Adds a listener for one or more categories of tracing events. The action of adding a
     * listener automatically enables tracing for the specified category or categories, if it was
     * not already enabled by another listener.
     *
     * @param category Required tracing category name or array of one or more category names to
     * listen to.
     * @param listener Receives tracing events of the requested category or categories. A listener
     * is invoked only once per event, even for a multi-category event.
     */
    on(category: string | string[], listener: TracingEventListener): void;

    /**
     * Adds a listener for one or more categories of tracing events. The action of adding a
     * listener automatically enables tracing for the specified category or categories, if it was
     * not already enabled by another listener.
     *
     * @param category Required tracing category name or array of one or more category names to
     * listen to.
     * @param listener Receives tracing events of the requested category.
     * or categories. A listener is invoked only once per event, even for a multi-category event.
     */
    addListener(category: string | string[], listener: TracingEventListener): void;

    /**
     * Removes a listener for one or more categories of tracing events. The action of removing a
     * listener automatically disables tracing for the specified category or categories, if there
     * are no other listeners remaining.
     *
     * @param category Required tracing category name or array of one or more category names to
     * stop listening to.
     * @param listener The listener to remove.
     */
    removeListener(category: string | string[], listener: TracingEventListener): void;

    /**
     * Removes all JavaScript listeners for one or more categories of tracing events, or removes
     * all JavaScript listeners for all categories if no category argument is supplied. The action
     * of removing listeners automatically disables tracing for the specified category or
     * categories, if there are no other listeners remaining. Because method does not remove any
     * non-JavaScript listeners, it is possible for tracing to still be enabled for the categories
     * after calling this method.
     *
     * @param category Required tracing category name or array of one or more category names to
     * stop listening to.
     */
    removeAllListeners(category?: string | string[]): void;

    /**
     * Gets all the JavaScript listeners for one or more categories of tracing events.
     * Non-JavaScript listeners are not included in the returned list.
     *
     * @param category Required tracing category name or array of one or more
     * category names to retrieve listeners for.
     * @returns Array of listeners for the specified categories. Even if a listener is registered
     * for more than one of the categories, it is only included in the list once.
     */
    listeners(category: string | string[]): TracingEventListener[];

    /**
     * Gets a count of all the JavaScript listeners for one or more categories of tracing events.
     * Non-JavaScript listeners are not included in the count.
     *
     * @param category Required tracing category name or array of one or more category names to
     * count listeners for.
     * @returns Count of listeners for the specified categories. Even if a listener is registered
     * for more than one of the categories, it is only counted once.
     */
    listenerCount(category: string | string[]): number;
}

/**
 * Partial interface for the Node.js Console module showing added and updated methods for tracing.
 * @see {@link https://nodejs.org/dist/latest-v6.x/docs/api/console.html}
 */
interface Console {
    /**
     * Writes a formatted message to the console with a stack trace.
     *
     * NOTE: This is not actually related to tracing, but is listed here because its name may
     * cause some confusion.
     *
     * EXISTING NODE.JS & WEB METHOD (NO CHANGE)
     * @see {@link https://nodejs.org/dist/latest-v6.x/docs/api/console.html#console_console_trace_message_args}
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Console/trace}
     */
    trace(message?: any, ...optionalParams: any[]): void;

    /**
     * Emits a "count" tracing event.
     *
     * If Console.logTracingEvents is true, then the counter value is also logged to the console,
     * regardless of whether tracing is enabled for any category.
     *
     * @param name Required name of the counter.
     * @param [value] Optional counter value or values. If the value is unspecified then each call
     * to count() increments the value of the single-value counter; if the value is a number then
     * it sets the value of the single-value counter. For a multi-value counter, this is must be a
     * dictionary object containing 2 name-value pairs. (The tracing system currently requires
     * multi-value counters to have exactly two values.)
     * @param [category] Optional category name or array of category names for the tracing event.
     * If unspecified, then Console.defaultTracingCategory is used.
     *
     * NEW NODE.JS METHOD, EXISTING WEB METHOD + NEW OPTIONAL PARAMETERS FOR TRACING
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Console/count}
     */
    count(name: string,
        value?: number | { [name: string]: number },
        category?: string | string[]): void;

    /**
     * Emits a "begin" tracing event.
     *
     * Even if Console.logTracingEvents is true, nothing is logged to the console until the
     * corresponding timeEnd() is called.
     *
     * @param name Required name of the timer.
     * @param [id] Optional identifier that can be used to correlate begin and end events and
     * distinguish them from other events with the same name. If unspecified, the name is used as
     * the id.
     * @param [category] Optional category name or array of category names for the tracing event.
     * If unspecified, then Console.defaultTracingCategory is used.
     * @param [args] Optional additional arguments for the trace event. This can be a string,
     * or a dictionary containing 0-2 key-value pairs, or a function that returns either of those.
     * (The tracing system currently supports only up to 2 arguments.)
     *
     * EXISTING NODE.JS & WEB METHOD + NEW OPTIONAL PARAMETERS FOR TRACING
     * @see {@link https://nodejs.org/dist/latest-v6.x/docs/api/console.html#console_console_time_label}
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Console/time}
     */
    time(name: string,
        id?: string,
        category?: string | string[],
        args?: string | { [name: string]: any } | (() => string | { [name: string]: any })): void;

    /**
     * Emits an "end" tracing event.
     *
     * If Console.logTracingEvents is true, then the event name and duration are also logged to the
     * console, regardless of whether tracing is enabled for any category.
     *
     * @param name Required name of the timer; must match the name from a prior call to time().
     * @param [id] Optional identifier that can be used to correlate timer begin and end events and
     * distinguish them from other timer events with the same name. If unspecified, the name is
     * used as the id.
     * @param [category] Optional category name or array of category names for the tracing event.
     * If unspecified, then Console.defaultTracingCategory is used.
     * @param [args] Optional additional arguments for the trace event. This can be a string,
     * or a dictionary containing 0-2 key-value pairs, or a function that returns either of those.
     * (The tracing system currently supports only up to 2 arguments.)
     *
     * EXISTING NODE.JS & WEB METHOD + NEW OPTIONAL PARAMETERS FOR TRACING
     * @see {@link https://nodejs.org/dist/latest-v6.x/docs/api/console.html#console_console_timeend_label}
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Console/timeEnd}
     */
    timeEnd(name: string,
        id?: string,
        category?: string | string[],
        args?: string | { [name: string]: any } | (() => string | { [name: string]: any })): void;

    /**
     * Emits an "instant" tracing event.
     *
     * If Console.logTracingEvents is true, then the event name and timestamp are also logged to
     * the console, regardless of whether tracing is enabled for any category.
     *
     * @param name Required name of the time stamp.
     * @param [category] Optional category name or array of category names for the tracing event.
     * If unspecified, then Console.defaultTracingCategory is used.
     * @param [args] Optional additional arguments for the trace event. This can be a string,
     * or a dictionary containing 0-2 key-value pairs, or a function that returns either of those.
     * (The tracing system currently supports only up to 2 arguments.)
     *
     * NEW NODE.JS METHOD, EXISTING WEB METHOD + NEW OPTIONAL PARAMETERS FOR TRACING
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Console/timeStamp}
     */
    timeStamp(name: string,
        category?: string | string[],
        args?: string | { [name: string]: any } | (() => string | { [name: string]: any })): void;

    /**
     * True to retain legacy behavior of writing timeEnd/timeStamp/count info to stdout.
     * False to suppress. Either way those events are also directed to the tracing system.
     */
    logTracingEvents: boolean;

    /**
     * True to generate tracing events with message in event args for log(), info(), warn() and
     * error() calls. False to suppress. Either way those messages are also written to the console.
     */
    traceLogMessages: boolean;

    /**
     * Default tracing category to use when calls to one of the tracing methods on Console do not
     * specify a category.
     */
    defaultTracingCategory: string;

    /**
     * Provices access to the tracing methods via the Console module.
     */
    tracing: Tracing;

    //
    // Non-tracing-related Console methods are omitted here.
    //
}
