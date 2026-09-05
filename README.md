# jetlog

> **Fast, lock-less and type-safe logging for small embedded systems.**

<img src="./support/intro.jpg" width="30%">

## Features

- **Thread-safe**: Usable from multiple threads and interrupts.
- **Minimal dependencies**: Relies only on atomic operations and ETL, avoiding
  platform-specific features.
- **High performance**: Fast writer; formatting is deferred to the reader.
- **Circular buffer**: Fixed memory usage.
- **Type-safe**: Ensures correctness at compile time.
- **Modern formatting**: Supports a subset of `std::format`-style syntax.
- **Configurable types support**: You can select only required types to minimize
  overhead.

> **Requires**: `C++17`


## Usage

See the [examples](./examples) folder.

This package depends on [ETL](https://www.etlcpp.com/) but does not pin a
version, to avoid conflicts with your application. Pin it in your application
instead.


## Supported Formats

```cpp
// Simple placeholder without format
{}

// Hexadecimal (lowercase/uppercase)
{:x}  // ff
{:X}  // FF

// Hexadecimal with base prefix
{:#x} // 0xff
{:#X} // 0XFF

// Zero-padded hexadecimal
{:04x} // 00ff

// Binary format
{:b}   // 101010
{:#b}  // 0b101010

// Decimal (default for integral types)
{:d}   // 42
```


## Supported Types

By default: integers up to 32 bits, `bool` and string-like parameters. To add
64-bit integers or floating point, list them in the `Codecs<>` option of your
config, see [codecs](./include/jetlog/private/codecs.hpp) for what is available.

Tag and format are stored by pointer and must outlive their records.
String arguments are copied unless wrapped in `jetlog::static_str`.
