'use strict';
const assert = require('assert');
const common = require('../common.js');

const trace = console.tracing.emit;
const traceCount = console.count;

let scenarios = {
    disabled(n) {
        traceBench(n, 'test1');
    },
    singleCategoryDisabled(n) {
        console.tracing.enableRecording('test2');
        traceBench(n, 'test1');
    },
    multiCategoryDisabled(n) {
        console.tracing.enableRecording('test3');
        traceBench(n, ['test1', 'test2']);
    },
    singleCategoryEnabled(n) {
        console.tracing.enableRecording('test1');
        traceBench(n, 'test1');
    },
    firstCategoryEnabled(n) {
        console.tracing.enableRecording('test1');
        traceBench(n, ['test1', 'test2']);
    },
    secondCategoryEnabled(n) {
        console.tracing.enableRecording('test2');
        traceBench(n, ['test1', 'test2']);
    },
    singleCategoryEnabledCount(n) {
        console.tracing.enableRecording('test1');
        console.tracing.options.logConsoleTracingEvents = false;
        traceCountBench(n, 'test1');
    },
    secondCategoryEnabledCount(n) {
        console.tracing.enableRecording('test2');
        console.tracing.options.logConsoleTracingEvents = false;
        traceCountBench(n, ['test1', 'test2']);
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

function traceBench(n, category) {
    for (let i = 0; i < n; i++) {
        trace({
            eventType: 'count',
            name: 'benchmark',
            category,
            value: i,
        });
    }
}

function traceCountBench(n, category) {
    for (let i = 0; i < n; i++) {
        traceCount('benchmark', i, null, category);
    }
}
