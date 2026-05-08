# BUILDING

This project: **a-curl-gcloud-plugin**
Version: **0.1.4**

## Local build

```bash
# one-shot build + install
./build.sh install
```

Or run the steps manually:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc || sysctl -n hw.ncpu || echo 4)"
sudo cmake --install .
```



## Install dependencies (from `deps.libraries`)


### System packages (required)

```bash
sudo apt-get update && sudo apt-get install -y build-essential libcurl4-openssl-dev libjansson-dev libssl-dev zlib1g-dev
```



### Development tooling (optional)

```bash
sudo apt-get update && sudo apt-get install -y autoconf automake gdb libtool perl python3 python3-pip python3-venv valgrind
```



### lexbor

Clone & build:

```bash
git clone --depth 1 --branch v2.3.0 --single-branch "https://github.com/lexbor/lexbor.git" "lexbor"
cd "lexbor"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local} -DLEXBOR_BUILD_TESTS=OFF -DLEXBOR_BUILD_EXAMPLES=OFF
cmake --build build -j"$(nproc)"
${SUDO}cmake --install build
cd ..
rm -rf "lexbor"
```


### libjansson-dev

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y libjansson-dev
```


### libjwt

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/benmcollins/libjwt.git" "libjwt"
cd "libjwt"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local}
cmake --build build -j"$(nproc)"
${SUDO}cmake --install build
cd ..
rm -rf "libjwt"
```


### OpenSSL

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y libssl-dev
```


### the-macro-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/the-macro-library.git" "the-macro-library"
cd "the-macro-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "the-macro-library"
```


### a-memory-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-memory-library.git" "a-memory-library"
cd "a-memory-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-memory-library"
```


### a-json-sax-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-json-sax-library.git" "a-json-sax-library"
cd "a-json-sax-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-json-sax-library"
```


### a-json-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-json-library.git" "a-json-library"
cd "a-json-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-json-library"
```


### the-lz4-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/the-lz4-library.git" "the-lz4-library"
cd "the-lz4-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "the-lz4-library"
```


### Threads

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y build-essential
```


### ZLIB

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y zlib1g-dev
```


### CURL

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y libcurl4-openssl-dev
```


### a-curl-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-curl-library.git" "a-curl-library"
cd "a-curl-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-curl-library"
```


### the-io-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/the-io-library.git" "the-io-library"
cd "the-io-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "the-io-library"
```

