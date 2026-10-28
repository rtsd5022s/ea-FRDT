# Security Policy

## Reporting a vulnerability

This tool is a floating-point stress tester. It does not handle untrusted input, does not run as a network service, and does not read or write arbitrary files. The command-line arguments are validated and invalid input exits hard.

If you find a memory-safety bug (buffer overflow, use-after-free, undefined behavior that could be exploited) in how the tool parses arguments or walks formats, please report it privately by email instead of opening a public issue.

Do not open a public issue for a suspected CVE. Contact the maintainer and give them a reasonable window to fix before disclosure.

## What is not a vulnerability

- The tool intentionally saturates CPU and may lag the system. This is its purpose.
- The tool may run forever if `-l -1` is passed without `-t`. This is documented.
- The tool may crash on malformed bit-pattern arguments. Arguments are validated; invalid ones exit with an error. If a crash is reachable only by passing obviously invalid input, that is not a security bug.
