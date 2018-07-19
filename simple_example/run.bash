#!/bin/bash

make
node --expose-wasm test_in_node.js
