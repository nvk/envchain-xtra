# Releasing

This fork is distributed to users primarily through the Homebrew tap:

- tap repo: `nvk/homebrew-tap`
- formula: `envchain-xtra`
- install:

```zsh
brew tap nvk/tap
brew install nvk/tap/envchain-xtra
```

The formula installs the `envchain` executable directly and declares:

```ruby
conflicts_with "envchain", because: "both install the envchain executable"
```

That keeps the user choice explicit between upstream and this fork.

## Release Flow

1. Commit the `envchain-xtra` changes you want to ship.
2. Choose the new release version.
3. Create and push a tag in `envchain-xtra`.
4. Update `Formula/envchain-xtra.rb` in `nvk/homebrew-tap` to the new tag and revision.
5. Push the tap update.
6. Verify the install path from Homebrew.

## Example

From the `envchain-xtra` repo:

```zsh
git tag -a v1.2.0 -m 'v1.2.0'
git push origin v1.2.0
```

Then update the tap formula to:

- `tag: "v1.2.0"`
- `revision: "<commit-sha>"`
- `version "1.2.0"`

And push the tap:

```zsh
git -C "$HOME/Library/Mobile Documents/com~apple~CloudDocs/claude-sandbox/homebrew-tap" push origin main
```

## Verify

Fresh install:

```zsh
brew update
brew install nvk/tap/envchain-xtra
```

If upstream `envchain` is already installed:

```zsh
brew uninstall envchain
brew install nvk/tap/envchain-xtra
```

If `envchain-xtra` is already installed but not linked:

```zsh
brew link --overwrite envchain-xtra
```
