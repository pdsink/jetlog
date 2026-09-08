# Changelog

## 2.0.0 - 2026-09-08

Full rewrite. Log points stay as they are - `push()` calls do not change -
everything around them does.

### Added

- `jetlog::static_str`, a way to say that a string outlives its records.
- The reader tells how many records were lost to overflow.

### Changed

- Tag and format are stored by pointer. String arguments are copied unless
  wrapped in `jetlog::static_str`.
- Configuration goes through one config object instead of positional template
  parameters.
- The time source is a constructor argument, so the writer needs no inheritance
  any more.
- Records are written directly into and read directly from buffer storage,
  without intermediate record-sized buffers.
- Requires C++17.


## 1.0.1 - 2025-08-31

### Changed

- Code polish. Nothing serious.

## 1.0.0 - 2025-04-19

### Added

- Initial release.
