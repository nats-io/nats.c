# NATS - C Client 
A C client for the [NATS messaging system](https://nats.io).

This is an Alpha work, heavily based on the [NATS GO Client](https://github.com/nats-io/nats).

[![License MIT](https://img.shields.io/npm/l/express.svg)](http://opensource.org/licenses/MIT)

## Installation

```
# Download 
git clone git@github.com:kozlovic/cnats.git .

# Build
make <options>

where options can be any, or a combination of,

clean
debug
install
examples
test

For instance:

make clean debug install 


# Run the test suite
make test
```

## Usage

After `make install` is executed, the header files will be located in `install/include` directory, and the libraries (static and dynamic) will be located in `install/lib`. Note that during a `make clean`, some of the directories get removed, so don't put personal files that you don't want to lose in those directories. The list as of now is:

```
install/lib
install/include
examples/
build/
build/unix
build/win
```

Check the `Makefile`'s `clean` tag to see what gets removed.

You cab also run the examples `make install examples` which will be located in the `examples` directory and check them for API usage.
 

## License

(The MIT License)

Copyright (c) 2015 Apcera Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
