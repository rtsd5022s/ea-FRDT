# Security Policy

## Reporting a vulnerability

This tool is a floating-point stress tester. It does not run as a network service and does not read arbitrary files. `--dump <file>` writes only the user-supplied output path (a binary walk log); it never opens other files on its own. Command-line arguments are validated and invalid input exits hard with a message on stderr.

If you find a memory-safety bug (buffer overflow, use-after-free, undefined behavior that could be exploited) in how the tool parses arguments, walks formats, or writes the dump file, report it privately instead of opening a public issue.

Do not open a public issue for a suspected CVE. Contact the maintainer and give a reasonable window to fix before disclosure.

## What is not a vulnerability

- The tool intentionally saturates CPU and may lag the system. This is its purpose.
- Running `-l -1` without `-t` risks a long full-load run; it is gated by a confirmation warning and should always be paired with `-t`.
- The tool rejects malformed arguments up front. A crash reachable only by passing obviously invalid input (unknown format, non-numeric start, malformed `0x` pattern) after validation is not a security bug; please still report it as a robustness defect.
