# Security policy

Pulse is an experimental per-user Windows power controller. The current review target is the latest source on `main`; there is no supported-version SLA or security certification.

Use GitHub's **Report a vulnerability** option under the repository Security tab for a private report when available. If private reporting is unavailable, open a minimal issue requesting a private reporting channel without posting exploit details, credentials or sensitive logs.

Relevant boundaries include restoration-journal handling, same-user configuration/IPC, process-handle lifetime, unsupported Windows capabilities and interference between power-setting writers. The journal checksum is not an authentication mechanism; this application does not establish a security boundary against a malicious process running as the same user.

The application has no remote-control or telemetry endpoint and ships no privileged driver. Do not run it elevated as a routine workaround. Successful builds, hardening flags and existing tests do not imply that an independent security audit has occurred.
