#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
launcher="$HOME/Desktop/REAC Control.command"

cat > "$launcher" <<SCRIPT
#!/usr/bin/env bash
exec "$repo_root/scripts/reac_control.command"
SCRIPT

chmod 755 "$launcher"
echo "Installed $launcher"
