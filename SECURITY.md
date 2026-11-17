# Security policy

## Reporting a vulnerability

Do not open a public issue for a security problem. Report it privately through GitHub's
"Report a vulnerability" button on this repository, or contact the maintainer directly.
Include the affected version or commit, a minimal reproduction, and the expected impact.

## Scope

In scope: the MTProto handshake and key handling, message encryption and decryption, session
storage, TL parsing of untrusted server data, and the bot HTTP/webhook code.

Out of scope: vulnerabilities in Telegram's servers, and issues that need a compromised local
machine with read access to the session files.

## Handling of secrets

- Session files and the sqlite session store are created with mode `0600`.
- API hashes, bot tokens and session strings must come from the environment or a secret store,
  never from source code or committed files.
- Never paste session strings or tokens into issues, logs or chat.
