"""Self-signed TLS for Focus hub — Chrome camera needs HTTPS (or localhost)."""
from __future__ import annotations

import datetime as dt
import ipaddress
import socket
from pathlib import Path
from typing import Iterable

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

REPO = Path(__file__).resolve().parents[2]
CERT_DIR = REPO / "data" / "productivity" / "behavior" / "hub_tls"
CERT_FILE = CERT_DIR / "hub.crt"
KEY_FILE = CERT_DIR / "hub.key"


def _lan_ips() -> list[str]:
    ips: list[str] = []
    try:
        hostname = socket.gethostname()
        for info in socket.getaddrinfo(hostname, None, socket.AF_INET):
            ip = info[4][0]
            if ip and not ip.startswith("127.") and ip not in ips:
                ips.append(ip)
    except OSError:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        if ip and ip not in ips and not ip.startswith("127."):
            ips.insert(0, ip)
    except OSError:
        pass
    return ips


def ensure_hub_cert(extra_hosts: Iterable[str] | None = None) -> tuple[Path, Path]:
    """Create or refresh local cert covering localhost + LAN IPs."""
    CERT_DIR.mkdir(parents=True, exist_ok=True)
    hosts = ["localhost", "127.0.0.1"]
    for ip in _lan_ips():
        if ip not in hosts:
            hosts.append(ip)
    if extra_hosts:
        for h in extra_hosts:
            h = (h or "").strip()
            if h and h not in hosts:
                hosts.append(h)

    need_new = True
    if CERT_FILE.is_file() and KEY_FILE.is_file():
        try:
            cert = x509.load_pem_x509_certificate(CERT_FILE.read_bytes())
            expiry = getattr(cert, "not_valid_after_utc", None) or cert.not_valid_after.replace(
                tzinfo=dt.timezone.utc
            )
            if expiry > dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=7):
                # Refresh if LAN IP missing from SAN
                sans = cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
                present: set[str] = set()
                for name in sans:
                    if isinstance(name, x509.DNSName):
                        present.add(name.value)
                    elif isinstance(name, x509.IPAddress):
                        present.add(str(name.value))
                if all(h in present for h in hosts):
                    need_new = False
        except Exception:
            need_new = True

    if not need_new:
        return CERT_FILE, KEY_FILE

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = issuer = x509.Name(
        [
            x509.NameAttribute(NameOID.COUNTRY_NAME, "IN"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "CALT Focus Local"),
            x509.NameAttribute(NameOID.COMMON_NAME, "calt-focus-hub.local"),
        ]
    )
    san_list: list[x509.GeneralName] = []
    for h in hosts:
        try:
            san_list.append(x509.IPAddress(ipaddress.ip_address(h)))
        except ValueError:
            san_list.append(x509.DNSName(h))

    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(dt.datetime.now(dt.timezone.utc) - dt.timedelta(minutes=1))
        .not_valid_after(dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=825))
        .add_extension(x509.SubjectAlternativeName(san_list), critical=False)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .sign(key, hashes.SHA256())
    )

    KEY_FILE.write_bytes(
        key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption(),
        )
    )
    CERT_FILE.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    return CERT_FILE, KEY_FILE
