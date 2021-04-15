#!/bin/bash
SCRIPT_DIR=$(dirname "$(readlink -f "$0")") # root dir?
HOOKS="pre-commit"
HOOK_DIR=$(git rev-parse --show-toplevel)/.git/hooks

for h in $HOOKS; do
  # TODO move into an archive?
  THIS_HOOK=${HOOK_DIR}/${h}
  if [ -f ${THIS_HOOK} ]; then
    echo "Found existing <$h>; overwriting."
    rm -f ${THIS_HOOK}
  fi

  if [ -f ${THIS_HOOK} ]; then
    echo "Tried to remove ${THIS_HOOK}, but it's still there."
    echo "You'll have to figure it out."
  else
    ln -s -f $SCRIPT_DIR/$h ${THIS_HOOK}
  fi
done
