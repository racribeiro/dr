# Packet 3 — reconciliations + command semantics

- `RECONCILE_AND_COMMANDS.md` — resolves the two open review points (field45 rule;
  keepalive-body rule) and defines the gimbal command API (encoding + semantics).
- `tools/reconcile.py <pcap>` — reproduces the field45 + keepalive-length findings.
- `tools/ka_corr.py <pcap>` — confirms the keepalive body [18:20] == type-5 f45.

Run the tools against the sanitized capture delivered in packet 2
(`captures/gimbalmove-sanitized.pcap`). No credentials in any of these files.
