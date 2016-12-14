'use strict';
const assert = require('assert');
const common = require('../common.js');
const tracing = console.tracing;

let scenarios = {
    disabled(n) {
        traceBench(n, 'test1');
    },
    singleCategoryDisabled(n) {
        tracing.enableRecording('test2');
        traceBench(n, 'test1');
    },
    multiCategoryDisabled(n) {
        tracing.enableRecording('test3');
        traceBench(n, ['test1', 'test2']);
    },
    singleCategoryEnabled(n) {
        tracing.enableRecording('test1');
        traceBench(n, 'test1');
    },
    firstCategoryEnabled(n) {
        tracing.enableRecording('test1');
        traceBench(n, ['test1', 'test2']);
    },
    secondCategoryEnabled(n) {
        tracing.enableRecording('test2');
        traceBench(n, ['test1', 'test2']);
    },
    singleCategoryEnabledCount(n) {
        tracing.enableRecording('test1');
        tracing.options.logConsoleTracingEvents = false;
        traceCountBench(n, 'test1');
    },
    secondCategoryEnabledCount(n) {
        tracing.enableRecording('test2');
        tracing.options.logConsoleTracingEvents = false;
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
        tracing.emit({
            eventType: 'count',
            name: 'benchmark',
            category,
            value: i,
        });
    }
}

function traceCountBench(n, category) {
    for (let i = 0; i < n; i++) {
        console.count('benchmark', i, null, category);
    }
}
