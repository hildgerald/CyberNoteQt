# CyberNoteQt
A little Software that used to take Markdown when you do a pentest. It accelerate screenshot and execute command from notes
I developed this software with the help of ClaudeAI Code to gain time when I will do the OSCP exam.
This program is similar than Obsidian (https://obsidian.md/) but it is adapted to grow the speed to takes notes.
You can launch PTY terminal with the option CTRL+SHIFT+S to get a screenshot from a selected zone in the PTY.
In the right zone in the software, il you double click on a library note, CyberNoteQt add it to the end of the current note.

And to finish, If you can execute an command line from a note directly to an opened PTY.

With this software, you reduce copy/paste and the screenshot operations.

## Compiling on Kali

```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-svg-dev cmake build-essential
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
./CyberNotePasQt
```

## Opening in Qt Designer / Qt Creator

- Qt Creator: `File > Open File or Project...` → select `CMakeLists.txt`. 
Double-clicking a `.ui` file in the project tree opens the built-in form
editor (i.e., Qt Designer).
- Standalone Qt Designer: `/usr/lib/qt6/bin/designer src/mainwindow.ui`

## Credits and third-party licenses

For the sake of transparency and respect for intellectual property, here
are the origins of each element of this project.

**Source code**: All C++ code for this port (VT100/xterm terminal emulator,
Markdown editor and highlighter, project management, pty integration, etc.)
was written specifically for this project, based on general Qt/C++ knowledge
and public technical specifications (VT100/xterm protocol, CommonMark/GFM
format)—no code was copied from other terminal emulators (xterm, VTE,
QTermWidget/Konsole, etc.) or other third-party projects.

**Icons** (`resources/icons/*.svg`, including the application icon):
Hand-drawn for this project (using simple geometric shapes: rectangles,
circles, vector paths) rather than sourced from third-party icon libraries
(Font Awesome, Material Design Icons, Feather Icons, etc.).

## The future
- Add the netcat functionality
- Add a HTTP webserver to serve attack software to a target
- And so on ...

