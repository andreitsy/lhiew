# LHiew

Linux clone of [hiew editor](https://www.hiew.ru/) which to view binary files in raw, hex or dissasembly mode.


[![license](https://img.shields.io/github/license/dec0dOS/amazing-github-template.svg?style=flat-square)](LICENSE)

</div>

<details open="open">
<summary>Table of Contents</summary>

- [About](#about)
    - [Built With](#built-with)
- [Getting Started](#getting-started)
    - [Prerequisites](#prerequisites)
    - [Usage](#usage)
        - [Cookiecutter template](#cookiecutter-template)
        - [Manual setup](#manual-setup)
        - [Variables reference](#variables-reference)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [Support](#support)
- [License](#license)
- [Acknowledgements](#acknowledgements)

</details>

---

## About
**LHiew** is a console hex editor for Linux clone of well-know editor Hiew. Amongst its feature set is its ability to view files in text, hex and disassembly mode in which A&T syntax is used.


## Getting Started

### Usage

#### Open file

You could open binary file to view
```sh
lhiew ./a.out
```

![image](pics/example.png)



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
| `PgUp`             | Move cursor up on screen               |
| `PgDn`             | Move cursor down on screen             |

Supported assembly operator size are: 
- 64 bit mode;
- 32 bit protected mode;
- 16 bit protected mode;
- real mode.

#### Installing

##### Requirments
1. CMake 3.23 or higher
2. Compillar which supports C11

##### Compilling from source

Please follow these steps for manual setup:

1. [Clone the code](https://github.com/dec0dOS/amazing-github-template/releases/download/latest/) using `git clone --recursive git@github.com:andreitsy/lhiew.git`
2. Create directory with build `cd lhiew && mkdir build`
3. Run `cd build && cmake ..`
4. Run `make`

##### Downloading

Simply download precompilled binary lhiew.

## License

This project is licensed under the **MIT license**. Feel free to edit and distribute this template as you like.

See [LICENSE](LICENSE) for more information.

## Acknowledgements

Thanks for this awesome resources that were used during the development:

- https://viewsourcecode.org/snaptoken/kilo/
- https://www.hiew.ru/
- https://github.com/zyantific/zydis
