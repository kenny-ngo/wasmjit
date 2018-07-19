function printErr(er) {
	console.log(er);
}

const util = require('util');
var filename = 'main.wasm';
const fs = require('fs');
const buf = fs.readFileSync(filename);
var buffer = new Uint8Array(buf);
const decoder = new util.TextDecoder("utf-8");

var lib = null;
WebAssembly.compile(buffer).then(function(module) {

    const memory = new WebAssembly.Memory({ initial: 32 });
    const imports = {
        env: {
            memory: memory,
            __indirect_function_table: new WebAssembly.Table({ initial: 1, maximum: 1, element: 'anyfunc' }),
        },
    };

    WebAssembly.instantiate(module, imports).then(function(instance) {
    	lib = instance.exports;
    }, printErr);
}, printErr).then(function() {

    // This is where the real main is...
    console.log("Module set to var `lib`...");
    console.log("Add 5 and 10: " + lib.add(5, 10));

});
