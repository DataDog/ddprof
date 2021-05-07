#!/bin/bash
for i in {1..10}; do
  release/collatz 1 1000000 A
  release/collatz 2 1000000 B
  release/collatz 3 1000000 C
done
