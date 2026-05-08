#!/usr/bin/env bash
set -euo pipefail

identity="${1:-REAC Local Root Code Signing}"
work_dir="${TMPDIR:-/tmp}/reac-local-codesign"
crt="$work_dir/reac-local-codesign.crt"
key="$work_dir/reac-local-codesign.key"
p12="$work_dir/reac-local-codesign.p12"
config="$work_dir/reac-local-codesign.cnf"

mkdir -p "$work_dir"

cat > "$config" <<CONFIG
[req]
distinguished_name = dn
x509_extensions = extensions
prompt = no

[dn]
CN = $identity

[extensions]
basicConstraints = critical,CA:true
keyUsage = critical,digitalSignature,keyCertSign
extendedKeyUsage = critical,codeSigning
subjectKeyIdentifier = hash
CONFIG

openssl req \
  -new \
  -newkey rsa:2048 \
  -nodes \
  -x509 \
  -days 3650 \
  -keyout "$key" \
  -out "$crt" \
  -config "$config" \
  >/dev/null 2>&1

openssl pkcs12 \
  -export \
  -inkey "$key" \
  -in "$crt" \
  -out "$p12" \
  -passout pass: \
  >/dev/null 2>&1

security import "$p12" \
  -k "$HOME/Library/Keychains/login.keychain-db" \
  -P "" \
  -T /usr/bin/codesign

sudo security add-trusted-cert \
  -d \
  -r trustRoot \
  -p codeSign \
  -k /Library/Keychains/System.keychain \
  "$crt"

echo "Created and trusted code signing identity: $identity"
security find-identity -v -p codesigning | grep -F "$identity" || true
