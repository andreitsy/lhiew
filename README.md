# LHiew

Linux clone of [hiew editor](https://www.hiew.ru/) which can view binary files in raw, hex or dissasembly mode.


[![license](https://img.shields.io/github/license/dec0dOS/amazing-github-template.svg?style=flat-square)](src/LICENSE)

</div>

<details open="open">
<summary>Table of Contents</summary>

- [About](##about)
- [Getting Started](#getting-started)
    - [Usage](###usage)
        - [Open file](####open_file)
    - [Keybindings](###keybindings)
- [Installing](##installing)
  - [Requirments](#####requirments)
  - [Compilling from source](#####compilling_from_source)
- [License](#license)
- [Acknowledgements](#acknowledgements)

</details>

---

## About
**LHiew** is a console editor, Linux clone of well-know editor Hiew. Amongst its feature set is its ability to view files in text, hex and disassembly mode in which A&T syntax is used.


## Getting Started

### Usage

#### Open file

You could open binary file to view
```sh
lhiew ./a.out
```

![image](pics/example.png)

#### Terminal sizes

LHiew follows the terminal's current width and height and redraws automatically
when you resize it, preserving the selected byte. The minimum usable size is
**24 columns by 5 rows**. Smaller windows show a resize message; enlarge the
window to resume, or press `Ctrl-q` to quit.

Text wraps to the available width. Hex mode adjusts the number of bytes per
row and keeps offsets, hex values, and ASCII aligned. Narrow disassembly views
prioritize offsets and instructions, adding the raw instruction bytes when
space permits. Long instructions are marked with `~` when truncated.
The status and help bars also adapt to the available width.

Disassembly keeps the selected instruction near the middle of the screen,
with preceding instructions above it when available. `PgUp` and `PgDn` move
by one screen of instructions; text and hex modes move by one screen of rows.

![Centered disassembly with the selected instruction highlighted](pics/assembler-centered.png)

#### Tests and sample binaries

After building, run `ctest --test-dir build --output-on-failure`. Unit tests cover
file loading, disassembly, rendering, cursor boundaries, and resizing. When
Python 3 is available, CTest also verifies the binary fixtures and drives the
application through a pseudo-terminal to test live resizing and keyboard input.

The reproducible [binary fixtures](tests/fixtures/README.md) include 16-, 32-,
and 64-bit x86 code, a minimal Linux ELF executable, and empty or malformed
instruction streams. For example, open `./build/lhiew tests/fixtures/raw_x86_64.bin`,
press `m` twice for disassembly, then `o` once to select 64-bit decoding.



#### Keybindings

| Key Combination    | Action                                 |
|--------------------|----------------------------------------|
| `Ctrl-q`           | Quit                                   |
| `Ctrl-m`           | Toggle previous mode                   |
| `m`                | Toggle next mode                       |
| `o`                | Change between assembly operator sizes |
| `h`, `Left Arrow`  | Move cursor left                       |
| `k`, `Up Arrow`    | Move cursor up                         |
| `j`, `Down Arrow`  | Move cursor down                       |
| `l`, `Right Arrow` | Move cursor right                      |
| `PgUp`             | Move up one screen                     |
| `PgDn`             | Move down one screen                   |

Supported assembly operator sizes are the following: 
- 64 bit mode;
- 32 bit protected mode;
- 16 bit protected mode;
- real mode.

#### Installing

##### Requirments
1. CMake 3.0 or higher
2. compiler that supports C11

##### Compilling from source

Please follow these steps for manual setup:

1. [Clone the code](https://github.com/dec0dOS/amazing-github-template/releases/download/latest/) using `git clone --recursive git@github.com:andreitsy/lhiew.git`
2. Create directory with build `cd lhiew && mkdir build`
3. Run `cd build && cmake ..`
4. Run `make`

## License

This project is licensed under the **MIT license**. Feel free to edit and distribute this template as you like.

See [LICENSE](src/LICENSE) for more information.

## Acknowledgements

Thanks for this awesome resources that were used during the development:

- https://viewsourcecode.org/snaptoken/kilo/
- https://www.hiew.ru/
- https://github.com/zyantific/zydis
