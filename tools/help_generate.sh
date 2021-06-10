#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

FILE=$(git rev-parse --show-toplevel)/docs/Commands.md
echo "# ddprof Commands" > ${FILE}
echo "" >> ${FILE}
echo "" >> ${FILE}
echo '```bash' >> ${FILE}
release/ddprof >> ${FILE}
echo '```' >> ${FILE}
