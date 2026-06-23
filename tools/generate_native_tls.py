#!/usr/bin/env python3
"""Generate native firmware TLS bootstrap material from local/CI secrets."""

from __future__ import annotations

import argparse
import base64
from datetime import datetime, timedelta, timezone
import os
import re
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


CERT_ENV = "QUOTES_CLOCK_TLS_CERT_PEM"
KEY_ENV = "QUOTES_CLOCK_TLS_KEY_PEM"
CERT_B64_ENV = "QUOTES_CLOCK_TLS_CERT_PEM_B64"
KEY_B64_ENV = "QUOTES_CLOCK_TLS_KEY_PEM_B64"


def load_dotenv(path: Path) -> None:
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines(keepends=False):
        candidate = line.lstrip()
        if not candidate or candidate.startswith("#") or "=" not in candidate:
            continue
        match = re.match(r"(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=(.*)", candidate)
        if not match:
            continue
        key, value = match.groups()
        if key in os.environ:
            continue
        value = value.lstrip()
        double_quoted = value.startswith('"') and value.endswith('"')
        if double_quoted or (value.startswith("'") and value.endswith("'")):
            value = value[1:-1]
        if double_quoted:
            value = (
                value.replace(r"\"", '"')
                .replace(r"\n", "\n")
                .replace(r"\r", "\r")
                .replace(r"\t", "\t")
            )
        os.environ[key] = value


def env_secret(name: str, b64_name: str) -> str:
    value = os.environ.get(name, "")
    if not value:
        encoded = os.environ.get(b64_name, "").strip()
        if encoded:
            value = base64.b64decode(encoded).decode("utf-8")
    return normalize_pem(value) if value else ""


def normalize_pem(value: str) -> str:
    normalized = value.replace("\r\n", "\n").replace("\r", "\n")
    return normalized if normalized.endswith("\n") else normalized + "\n"


def generate_insecure_fallback() -> tuple[str, str]:
    key = ec.generate_private_key(ec.SECP256R1())
    subject = issuer = x509.Name(
        [
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Quotes Clock CI"),
            x509.NameAttribute(NameOID.COMMON_NAME, "quotes-clock-ci.invalid"),
        ]
    )
    now = datetime.now(timezone.utc)
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(minutes=5))
        .not_valid_after(now + timedelta(days=1))
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.SubjectAlternativeName([x509.DNSName("quotes-clock-ci.invalid")]), critical=False)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                key_encipherment=False,
                content_commitment=False,
                data_encipherment=False,
                key_agreement=True,
                key_cert_sign=False,
                crl_sign=False,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .sign(key, hashes.SHA256())
    )
    cert_pem = cert.public_bytes(serialization.Encoding.PEM).decode("ascii")
    key_pem = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ).decode("ascii")
    return cert_pem, key_pem


def cpp_string_literal(value: str) -> str:
    lines = []
    for line in value.splitlines():
        escaped = line.replace("\\", "\\\\").replace('"', r"\"")
        lines.append(f'    "{escaped}\\n"')
    return "\n".join(lines)


def validate_pem(value: str, kind: str) -> None:
    if f"-----BEGIN {kind}-----" not in value or f"-----END {kind}-----" not in value:
        raise SystemExit(f"{kind} PEM is missing BEGIN/END markers.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate native ESP-IDF TLS bootstrap header.")
    parser.add_argument("--output", type=Path, default=Path("firmware/native-idf/main/generated/tls_bootstrap.hpp"))
    parser.add_argument("--env-file", type=Path, default=Path(".env"))
    parser.add_argument(
        "--allow-insecure-fallback",
        action="store_true",
        help="Generate an ephemeral throwaway self-signed cert when no TLS secrets are present.",
    )
    args = parser.parse_args()

    load_dotenv(args.env_file)
    cert = env_secret(CERT_ENV, CERT_B64_ENV)
    key = env_secret(KEY_ENV, KEY_B64_ENV)
    if not cert or not key:
        if not args.allow_insecure_fallback:
            raise SystemExit(
                "Missing TLS certificate/key. Set "
                f"{CERT_ENV}/{KEY_ENV} or {CERT_B64_ENV}/{KEY_B64_ENV} in .env or CI secrets."
            )
        cert, key = generate_insecure_fallback()
        print("Generated insecure throwaway TLS bootstrap material for this build.")
    validate_pem(cert, "CERTIFICATE")
    validate_pem(key, "PRIVATE KEY")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "\n".join(
            [
                "#pragma once",
                "",
                "// Generated by tools/generate_native_tls.py. Do not edit or commit.",
                "namespace quotes_clock::generated {",
                "",
                "inline constexpr char kBootstrapCertPem[] =",
                cpp_string_literal(cert) + ";",
                "",
                "inline constexpr char kBootstrapKeyPem[] =",
                cpp_string_literal(key) + ";",
                "",
                "}  // namespace quotes_clock::generated",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
