'use strict';
const assert = require('assert');
const common = require('../common.js');

let scenarios = {
    disabled(n) {
        doWork(n, 'test1');
    },
    singleCategoryDisabled(n) {
        console.tracing.enableRecording('test2');
        doWork(n, 'test1');
    },
    multiCategoryDisabled(n) {
        console.tracing.enableRecording('test3');
        doWork(n, ['test1', 'test2']);
    },
    singleCategoryEnabled(n) {
        console.tracing.enableRecording('test1');
        doWork(n, 'test1');
    },
    firstCategoryEnabled(n) {
        console.tracing.enableRecording('test1');
        doWork(n, ['test1', 'test2']);
    },
    secondCategoryEnabled(n) {
        console.tracing.enableRecording('test2');
        doWork(n, ['test1', 'test2']);
    },
};

const bench = common.createBenchmark(main, {
    scenario: Object.keys(scenarios),
});

function main(conf) {
  let n = 10000;
  let scenarioName = conf.scenario;
  let scenarioFn = scenarios[scenarioName] || function () {};
  bench.start();
  scenarioFn(n);
  bench.end(n);
}

function doWork(n, emitCategory) {
    for (let i = 0; i < n; i++) {
        let value = Math.round(Math.sqrt(i) * Math.sqrt(i));
        if (emitCategory) {
            console.tracing.emit({
                eventType: 'count',
                name: 'benchmark',
                category: emitCategory,
                value,
            });
        }
    }
}
