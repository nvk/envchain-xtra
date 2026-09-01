```text
             _
            | |
 ________  _| |_ _ __ __ _
|______\ \/ / __| '__/ _` |
        >  <| |_| | | (_| |
       /_/\_\\__|_|  \__,_|
```

# envchain-xtra

Maintained `envchain` fork for safer macOS Keychain secret release and local
AI-agent security stacks.

`envchain-xtra` installs the familiar `envchain` executable: secrets stay in the
OS vault, and are released only to the command you explicitly launch. This fork
keeps the original interface useful while tightening the defaults that matter
when coding agents, wrappers, and sandbox layers are part of the workflow.

```zsh
brew tap nvk/tap
brew install nvk/tap/envchain-xtra

envchain --set aws AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY
envchain aws env | grep AWS_
```

## Why This Fork Exists

The original [`sorah/envchain`](https://github.com/sorah/envchain) made the
important design choice: do not put secrets in shell startup files. Store them in
the OS secret store and release them only when needed.

This fork is no longer just an experiment. As of 2026-05-01, the original
upstream `master` branch was last updated on 2024-04-24. This fork now carries
the active macOS maintenance, Homebrew distribution, security defaults, and
agent-stack integration used by this environment.

The premise is direct:

- you cannot let coding agents run loose with live keys
- you cannot assume a familiar command implies a trustworthy dependency tree
- you should not grant broad ambient environment access by default

This fork exists to keep the secret-release layer strong while leaving launch
verification and sandbox policy to the layers above and below it.

## What You Get

- `envchain` command compatibility with the maintained `envchain-xtra` package
- macOS Keychain storage through the modernized `SecItem` path
- hidden input by default when running `envchain --set`
- redacted save confirmations instead of plaintext terminal leaks
- direct binary approval and fingerprint inspection commands
- Homebrew distribution through `nvk/tap/envchain-xtra`
- documented fit with `bondage` and `nono` for local agent hardening

## Quick Links

- [Install](#installation)
- [Usage](#usage)
- [Security model](#security-model-in-this-fork)
- [Maintenance policy](#maintenance-policy)
- [Project lineage](#project-lineage)
- [Release notes](RELEASING.md)

## Maintenance Policy

This fork preserves upstream compatibility where that does not weaken the
security model. It will intentionally diverge when safer defaults or modern
macOS behavior require it.

Current fork responsibilities:

- maintain the Homebrew formula as `nvk/tap/envchain-xtra`
- keep the installed binary name as `envchain` for drop-in compatibility
- ship macOS Keychain fixes as the primary supported path
- keep Linux Secret Service support on a best-effort basis
- default toward non-leaky terminal behavior
- document how this fits into a multi-agent local security stack

Notable fork changes:

- modernized the macOS backend away from deprecated Keychain APIs toward
  `SecItem`
- added binary approval and fingerprint inspection commands
- made `envchain --set` hide input by default
- redacts saved-value confirmation output instead of printing secrets in
  plaintext
- documents the preferred `bondage` + `nono` launch stack

## Upstream Sync Policy

This fork still tracks the original project as `upstream`, but does not wait on
upstream for releases that affect local security posture.

Upstream patches are welcome when they fit the fork's goals. If upstream becomes
active again, this repository remains the maintained downstream distribution for
the `envchain-xtra` security stack rather than a temporary branch.

## What It Does

Secrets for common computing environments, such as `AWS_SECRET_ACCESS_KEY`, are
commonly provided through environment variables.

A common practice is to place them in shell initialization files such as `.bashrc` and `.zshrc`.

Putting these secrets on disk in this way is a grave risk.

`envchain` stores credential values in a secure vault and exports them to environment
variables only when you invoke it explicitly.

Currently, `envchain` supports macOS Keychain and D-Bus Secret Service
(`gnome-keyring`) as storage backends.

Don't give any credentials implicitly!

## Security Model in This Fork

The intended security model in this fork is not just "store secrets in Keychain and
run `envchain`."

The preferred stack is:

```text
shell name -> bondage -> [envchain] -> [nono] -> exact pinned tool
```

In that model:

- `envchain` stores and releases secrets
- `envchain` approves the direct binary it executes
- `bondage` verifies the exact leaf target, interpreter, and package tree
- `nono` remains the sandbox layer

That split is cleaner because secret release, launch verification, and sandbox policy
stay in separate layers instead of being mixed into shell glue.

It also deliberately leans on the OS where the OS is stronger than userland glue:

- Keychain remains the real secret store on macOS
- OS-level signing identity can be used as part of binary approval and drift detection
- `envchain` is not trying to replace either of those with custom storage logic

### Compatibility Layer: `contrib/shell-guards.zsh`

[`contrib/shell-guards.zsh`](contrib/shell-guards.zsh) is still provided for
wrapper-based workflows that have not moved to `bondage` yet.

It remains useful as a transitional or lightweight compatibility layer because it can:

- fingerprint the direct binary `envchain` executes
- fingerprint the final leaf binary a trusted wrapper launches
- optionally require an external `touchid-check` helper before launch

Example:

```zsh
source /path/to/contrib/shell-guards.zsh

my_tool() {
  _verify_binary my_tool || return 1
  envchain my-namespace nono run -- command my_tool "$@"
}
```

The helper file also provides:

- `envchain-approve <binary>` to store a fingerprint for a resolved binary path
- `envchain-reapprove` to refresh fingerprints after upgrades
- `envchain-status` to inspect the allowlist and current fingerprints

If you also want a human-approval gate, `_require_touchid` can be used with an
external `touchid-check` helper when `ENVCHAIN_TOUCHID=1`.

For new setups, `bondage` should be the preferred launcher path. The shell guard
script is best treated as compatibility glue for setups that still need shell-based
wrappers.

## Experimental Branches

The `touch-id` branch is kept separate from `master` while biometric prompting
and secret-storage behavior are evaluated:

- [PR #1: Add macOS Touch ID gates and biometric secret storage](https://github.com/nvk/envchain-xtra/pull/1)

The stable path is still `envchain` for secret release, `bondage` for launch
verification, and `nono` for sandboxing.

## Requirements (macOS)

- macOS
  - Current Homebrew releases are built and used on modern Apple Silicon macOS.
  - Older macOS compatibility is inherited from upstream and best-effort only.

## Requirements (Linux)

- readline
- libsecret
- D-Bus Secret Service
    - GNOME keyring
    - KeePassXC

Linux support is inherited from upstream. It should not be treated as the
primary tested path for this fork unless a maintainer is actively exercising it.

## Installation

### Homebrew Tap (macOS)

```
brew tap nvk/tap
brew install nvk/tap/envchain-xtra
```

This installs the `envchain` executable from this fork.

Choose this fork when you want the maintained macOS path, safer terminal
defaults, Homebrew packaging, and integration with the local AI-agent security
stack. Choose upstream only if you specifically need the original behavior and
are prepared to own any missing maintenance locally.

If you also want the launcher/policy layer described above:

```
brew install nvk/tap/agent-bondage
```

It intentionally conflicts with the upstream Homebrew `envchain` formula because
both install the same binary name.

If upstream `envchain` is already installed:

```
brew uninstall envchain
brew install nvk/tap/envchain-xtra
```

If `envchain-xtra` is already installed but not linked yet:

```
brew link --overwrite envchain-xtra
```

### From Source

This path is mainly for development work on the fork itself:

```
$ make

$ sudo make install
(or)
$ cp ./envchain ~/bin/
```

## Usage

### Saving variables

Environment variables are set within a specified _namespace._ You can set variables in a single command:

```
envchain --set NAMESPACE ENV [ENV ..]
```

You will be prompted to enter the values for each variable.
Input is hidden by default. After saving, `envchain` prints a redacted preview:
the first four characters, or the prefix up to an early dash, followed by
`...` and the last three characters. Short values are shown only as `...`.

For example, we can set two variables, `AWS_ACCESS_KEY_ID` and
`AWS_SECRET_ACCESS_KEY`, within a namespace called `aws`:

```
$ envchain --set aws AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY
aws.AWS_ACCESS_KEY_ID (hidden):
aws.AWS_ACCESS_KEY_ID saved: my-a...key
aws.AWS_SECRET_ACCESS_KEY (hidden):
aws.AWS_SECRET_ACCESS_KEY saved: ...
```

Here we define a single new variable within a different namespace:

```
$ envchain --set hubot HUBOT_HIPCHAT_PASSWORD
hubot.HUBOT_HIPCHAT_PASSWORD (hidden):
hubot.HUBOT_HIPCHAT_PASSWORD saved: ...
```

These will all appear as application passwords with `envchain-NAMESPACE` in the data store (Keychain in macOS, gnome-keyring in common Linux distros).

### Execute commands with defined variables

```
$ env | grep AWS_ || echo "No AWS_ env vars"
No AWS_ env vars
$ envchain aws env | grep AWS_
AWS_ACCESS_KEY_ID=my-access-key
AWS_SECRET_ACCESS_KEY=secret
$ envchain aws s3cmd blah blah blah
⋮
```

```
$ envchain hubot env | grep AWS_ || echo "No AWS_ env vars for hubot"
No AWS_ env vars for hubot
$ envchain hubot env | grep HUBOT_
HUBOT_HIPCHAT_PASSWORD: xxxx
```

You may specify multiple namespaces at once by separating them with commas:

```
$ envchain aws,hubot env | grep 'AWS_\|HUBOT_'
AWS_ACCESS_KEY_ID=my-access-key
AWS_SECRET_ACCESS_KEY=secret
HUBOT_HIPCHAT_PASSWORD: xxxx
```

The executed process also receives a sanitized injection manifest:

- `ENVCHAIN_METADATA_VERSION=1`
- `ENVCHAIN_NAMESPACES` contains the requested comma-separated namespaces
- `ENVCHAIN_KEYS` contains sorted, comma-separated variable names only
- `ENVCHAIN_KEYS_ENCODING=percent-v1` identifies percent-encoding used for
  non-alphanumeric bytes in key names

Secret values are never copied into the manifest. The envchain manifest names
and the Bondage profile-context names are reserved; envchain fails closed if a
namespace attempts to define one of them. A namespace lookup failure also stops
execution instead of launching a command with missing credentials.

### More options

#### `--list`

List namespaces that have been created
```
$ envchain --list
aws
hubot
```

#### `--noecho`

Do not echo user input. This is the default.
```
$ envchain --set --noecho foo BAR
foo.BAR (hidden):
foo.BAR saved: ...
```

#### `--echo`

Echo user input while typing. Use this only when you explicitly want plaintext
visible in terminal scrollback or logs.
```
$ envchain --set --echo foo BAR
foo.BAR: visible-while-typing
foo.BAR saved: visi...ing
```
#### `--require-passphrase`

Always ask for keychain passphrase
```
$ envchain --set --require-passphrase name
```

#### `--no-require-passphrase`

Do not ask for keychain passphrase
```
$ envchain --set --no-require-passphrase name
```

## Project Lineage

`envchain-xtra` is based on `envchain` by Shota Fukumori and contributors.
The original project made the important design choice: keep secrets out of shell
startup files and release them only for explicit commands.

Original authors:

- Sorah Fukumori <her@sorah.jp>
- eagletmt

Fork maintenance and additional changes:

- NVK, 2026-present

## License

MIT License

## Releasing

See [`RELEASING.md`](RELEASING.md).
