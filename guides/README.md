# cppgram guides

| Guide | For |
|---|---|
| [LINUX_INSTALL.md](LINUX_INSTALL.md) | Building on Linux, tested versions, package list |
| [AUTHENTICATION.md](AUTHENTICATION.md) | User login, bot login, states and errors |
| [SESSIONS.md](SESSIONS.md) | Session storage, backup, restore |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layers, threads, request and update flow |
| [API_SUPPORT.md](API_SUPPORT.md) | What is compiled, tested, or live-verified |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Errors and what to do |
| [VPS_OPERATIONS.md](VPS_OPERATIONS.md) | Running as a service on a Linux VPS |
| [THREAT_MODEL.md](THREAT_MODEL.md) | Assets, attackers, known limits |
| [BENCHMARKS.md](BENCHMARKS.md) | Measured local numbers and environment |
| [VERSIONING.md](VERSIONING.md) | Semver policy and API compatibility rules |
| [MIGRATION.md](MIGRATION.md) | What changed for existing code and data |
- [Encrypted session files](SESSION_ENCRYPTION.md): passphrase, backup, recovery, limits.
- [Obfuscated proxy design notes](OBFUSCATED_PROXY_PLAN.md): implemented for the client side (plain and padded secrets), offline tested only; not verified against a real proxy server.
- [Final report](../FINAL_REPORT.md): current status, verified results, incomplete list, verdict.
- [Reconnect and FLOOD_WAIT](RECONNECT_AND_FLOOD_WAIT.md): backoff, budgets, cancellation, FLOOD_WAIT limits.
- [Proxies](PROXIES.md): SOCKS5 and MTProto obfuscated proxy configuration and limits.
- [Known limitations](../KNOWN_LIMITATIONS.md): feature status by label and live-test blockers.
