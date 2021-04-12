#!/bin/bash
SCRIPT_DIR=$(dirname "$(readlink -f "$0")") # root dir?
HOOKS="pre-commit"
HOOK_DIR=$(git rev-parse --show-toplevel)/.git/hooks

for h in $HOOKS; do
  # TODO move into an archive?
  [ -f $HOOK_DIR/$h ] && rm $HOOK_DIR/$h

  ln -s -f $SCRIPT_DIR/$h $HOOK_DIR/$h
done
