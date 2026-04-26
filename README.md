# envchain - set environment variables with macOS keychain or D-Bus secret service

This repository is a fork of the original [`sorah/envchain`](https://github.com/sorah/envchain).
The goal here is to stay close to the upstream project while evaluating a small number of
security- and macOS-focused changes separately.

## What?

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

For wrapper-based workflows, there are two separate trust decisions:

- `envchain` approves the binary it executes directly and fingerprints that exact
  path before exporting secrets
- [`contrib/shell-guards.zsh`](contrib/shell-guards.zsh) approves the final leaf
  binary when a trusted wrapper such as `nono` launches something else on your behalf

If you use wrappers, the shell guard layer is part of the intended setup. Without it,
`envchain` can validate the wrapper (`nono`) but not the final tool (`claude`,
`codex`, `opencode`, `pi`, etc.) that the wrapper actually launches.

Recommended pattern:

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

Longer-term, the goal should be to absorb the leaf-binary verification logic into
`envchain` itself so the shell script becomes convenience glue instead of a required
security component.

## Testing

The following change is being evaluated in an open pull request and is not part of `master` yet:

- [PR #1: Add macOS Touch ID gates and biometric secret storage](https://github.com/nvk/envchain-xtra/pull/1)

## Requirements (macOS)

- macOS
  - Confirmed to work on OS X 10.11 (El Capitan), macOS 10.12 (Sierra).
  - OS X 10.7 (Lion) or later is required, but not confirmed

## Requirements (Linux)

- readline
- libsecret
- D-Bus Secret Service
    - GNOME keyring
    - KeePassXC

## Installation

### Homebrew Tap (macOS)

```
brew tap nvk/tap
brew install nvk/tap/envchain-xtra
```

This installs the `envchain` executable from this fork.

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
For example, we can set two variables, `AWS_ACCESS_KEY_ID` and
`AWS_SECRET_ACCESS_KEY`, within a namespace called `aws`:

```
$ envchain --set aws AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY
aws.AWS_ACCESS_KEY_ID: my-access-key
aws.AWS_SECRET_ACCESS_KEY: secret
```

Here we define a single new variable within a different namespace:

```
$ envchain --set hubot HUBOT_HIPCHAT_PASSWORD
hubot.HUBOT_HIPCHAT_PASSWORD: xxxx
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

### More options

#### `--list`

List namespaces that have been created
```
$ envchain --list
aws
hubot
```

#### `--noecho`

Do not echo user input
```
$ envchain --set --noecho foo BAR
foo.BAR (noecho):
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

## Sponsor

<a href='https://ko-fi.com/J3J8CKMUU' target='_blank'><img height='36' style='border:0px;height:36px;' src='https://cdn.ko-fi.com/cdn/kofi3.png?v=3' border='0' alt='Buy Me a Coffee at ko-fi.com' /></a>

### Screenshot

#### OS X Keychain

![](http://img.sorah.jp/20140519_060147_dqwbh_20140519_060144_s1zku_Keychain_Access.png)

#### Seahorse (gnome-keyring)

![](https://img.sorah.jp/2016-06-08_19-46-10_ff9c444.png)

## Author

- Sorah Fukumori <her@sorah.jp>
- eagletmt

## License

MIT License

## Releasing

See [`RELEASING.md`](RELEASING.md).
