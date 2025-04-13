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

> **Requires**: `C++14`


## Usage

For usage examples, see the [examples](./examples) folder.

Note, this package uses [ETL](https://www.etlcpp.com/), but does not pin the
concrete version, to avoid conflict with your application. Set a concrete
dependency version in your app, to have stable configuration.


## Supported Formats

Here are the supported formatting options:

```cpp
// Simple placeholder without format
{}

// Hexadecimal (lowercase/uppercase)
{:x}  // ff
{:X}  // FF

// Hexadecimal with leading 0X
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

The logger supports both numeric and string-like parameters. By default, numeric types include 32-bit integers and floating-point numbers. For custom configurations, such as adding 64-bit integers or removing floating-point types, refer to the [typelists](./include/jetlog/private/typelists.hpp) file. This allows you to optimize the logger for your specific needs and minimize overhead.


## Known Edge Cases

Each writer first creates a shadow record and then publishes it. For parallel writes, the last writer publishes all records. This can cause a side effect when a high-pressure writer interrupts another: if the buffer overflows before publishing, the upcoming records will be lost. This behavior is an intentional tradeoff to balance features with the constraints of embedded systems.

Such situations are uncommon in small embedded systems. However, if you require high-pressure writes, use a separate buffer for each writer. When a single writer is used per buffer, this side effect will not occur.