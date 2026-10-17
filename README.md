# CyberNoteQt — Markdown Notes for Penetration Testing

**CyberNoteQt** is a Linux desktop application for taking Markdown notes during penetration tests and security assessments. Built with **C++, Qt 6 and CMake**, it combines project organization, a Markdown editor and integrated Linux terminals in a single workspace.

Designed for penetration testers, cybersecurity professionals and security researchers, CyberNoteQt helps organize targets, document findings, manage reusable notes and execute commands directly from a working note.

**Repository:** https://github.com/hildgerald/CyberNoteQt

## Features

### Markdown editor for pentest notes

- Live Markdown formatting while editing.
- Syntax highlighting for code blocks.
- Support for headings, lists, task checkboxes, quotations and horizontal rules.
- Editable Markdown tables with row and column operations.
- Inline image integration using `![[path]]` references.
- Paste screenshots directly from the clipboard into a note.
- Automatic table of contents through the Outline panel.
- Save notes in Markdown format.

### Project and notes management

- Organize penetration tests into projects, folders, machines and notes.
- Create, rename, duplicate, move, merge and delete notes.
- Browse project files and reusable note libraries.
- Reuse note templates with target-specific parameter substitution.
- Store target information in `.target` files.

### Integrated Linux terminal

- Open interactive Linux terminals alongside your notes.
- Use a pseudo-terminal (PTY) for interactive shell applications.
- Run commands directly from the current Markdown note.
- Work with shell prompts, terminal colors and applications such as `vim`, `nano`, `less` and `htop`.
- Copy terminal output as text (CTRL + C)or as an image(CTRL+s).
- Manage multiple terminal sessions.

### Integrated Netcat listener
- Use a `ǹc -lnvp <port>` to listen yours reverse shell
- Run commands directly from the current Markdown note.
- Work with shell prompts, terminal colors and applications such as `vim`, `nano`, `less` and `htop`.
- Copy terminal output as text (CTRL + C)or as an image(CTRL+s).
- Manage multiple terminal sessions.

### Built for Linux and cybersecurity workflows

CyberNoteQt is developed in C++ using Qt 6 and CMake. It has been compiled and tested in a Kali Linux/Debian environment.

The application is intended to make penetration testing documentation more convenient by keeping notes, target information and terminal sessions in one place.

## Use cases

CyberNoteQt can be used to:

- Document penetration tests and authorized security assessments.
- Record commands, outputs and technical findings.
- Organize notes by target, host or assessment.
- Maintain reusable Markdown checklists and testing procedures.
- Capture screenshots for technical documentation.
- Keep a structured record of a security testing workflow.

## Requirements

The current build instructions target Kali Linux and Debian-based distributions.

- Linux
- C++ compiler with C++ support required by the project
- Qt 6 development libraries
- CMake
- GNU Make and build tools
- Qt 6 SVG development support
- `iproute2` for target-related IP address detection

## Build and install from source

Clone the repository:

```bash
git clone https://github.com/hildgerald/CyberNoteQt.git
cd CyberNoteQt
```

Install the build dependencies on Kali Linux or Debian:

```bash
sudo apt update
sudo apt install qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-svg-dev cmake build-essential
```

Configure and compile the application:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Launch the compiled application:

```bash
./build/CyberNotePasQt
```

The executable is currently named `CyberNotePasQt` in the build instructions, even though the repository is named `CyberNoteQt`.

## Technology stack

- **C++** — application implementation
- **Qt 6** — graphical user interface and application framework
- **CMake** — build system
- **Markdown** — note format
- **POSIX PTY** — interactive Linux terminal sessions
- **Kali Linux / Debian** — tested development environments

## Project status

The project contains a working Qt 6 application with project management, Markdown editing, target information and integrated Linux terminal functionality.

Development and compatibility work may continue. Check the repository for the latest source code, changes and build instructions.

## License

CyberNoteQt is distributed under the MIT License. See [`LICENSE`](LICENSE) for the license text and terms.

Third-party components, including Qt, remain subject to their respective licenses.

## Contributing

Bug reports, suggestions, testing and contributions are welcome. Please use the GitHub repository to report issues or propose improvements.

## Author and source code

- GitHub repository: https://github.com/hildgerald/CyberNoteQt
- Source code: https://github.com/hildgerald/CyberNoteQt/tree/main/src
- Issues and bug reports: https://github.com/hildgerald/CyberNoteQt/issues

CyberNoteQt is a tool for documenting and organizing **authorized penetration tests and security assessments**. Users are responsible for ensuring that their testing activities are properly authorized.
