#!/bin/bash
#valgrind --leak-check=full \
#         --show-leak-kinds=all \
#         --track-origins=yes \
#         --verbose \
#         --log-file=valgrind-out.txt \
#         --trace-children=yes \
#         bench/runners/runit.sh
#
valgrind --tool=massif --log-file=massif-out.txt --trace-children=yes bench/runners/runit.sh
