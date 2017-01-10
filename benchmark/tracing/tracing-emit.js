'use strict';
const assert = require('assert');
const tracing = require('tracing');
const common = require('../common.js');

let scenarios = {
    disabled(n) {
        traceEmitInstantBench(n, 'test1');
    },
    singleCategoryDisabled(n) {
        tracing.enableRecording('test2');
        traceEmitInstantBench(n, 'test1');
    },
    multiCategoryDisabled(n) {
        tracing.enableRecording('test3');
        traceEmitInstantBench(n, ['test1', 'test2']);
    },
    singleCategoryEnabled(n) {
        tracing.enableRecording('test1');
        traceEmitInstantBench(n, 'test1');
    },
    firstCategoryEnabled(n) {
        tracing.enableRecording('test1');
        traceEmitInstantBench(n, ['test1', 'test2']);
    },
    secondCategoryEnabled(n) {
        tracing.enableRecording('test2');
        traceEmitInstantBench(n, ['test1', 'test2']);
    },
    singleCategoryEnabledCount(n) {
        tracing.enableRecording('test1');
        console.options.logTracingEvents = false;
        traceConsoleCountBench(n, 'test1');
    },
    secondCategoryEnabledCount(n) {
        tracing.enableRecording('test2');
        console.options.logTracingEvents = false;
        traceConsoleCountBench(n, ['test1', 'test2']);
    },
};

const bench = common.createBenchmark(main, {
    scenario: Object.keys(scenarios),
});

function main(conf) {
  let n = 1000000;
  let scenarioName = conf.scenario;
  let scenarioFn = scenarios[scenarioName] || function () {};
  bench.start();
  scenarioFn(n);
  bench.end(n);
}

function traceEmitInstantBench(n, category) {
    for (let i = 0; i < n; i++) {
        tracing.emit(category, 'benchmark');
    }
}

function traceEmitCountBench(n, category) {
    for (let i = 0; i < n; i++) {
        tracing.emit({
            eventType: 'count',
            name: 'benchmark',
            category,
            value: i,
        });
    }
}

function traceConsoleInstantBench(n, category) {
    for (let i = 0; i < n; i++) {
        console.timeStamp('benchmark', category);
    }
}

function traceConsoleCountBench(n, category) {
    for (let i = 0; i < n; i++) {
        console.count('benchmark', category);
    }
}
