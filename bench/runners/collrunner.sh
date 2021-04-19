#!/bin/bash
for i in {1..10}; do
  release/collatz 1 1000000 E
  release/collatz 2 1000000 F
  release/collatz 3 1000000 G
done
