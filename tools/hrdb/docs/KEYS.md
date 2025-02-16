Normal focus
============

Basic program control:
| Key           | Operation                                    |
|---------------|----------------------------------------------|
| Ctrl+R        | Start/stop                                   |
| Esc           | Stop                                         |
| S             | Step (execute next instruction)              |
| Shift+S       | Step DSP                                     |
| Ctrl+S        | Skip (jump over instruction                  |
| N             | Next (execute, but step through bsr/jsr/dbf) |
| Shift + N     | Next DSP                                     |
| U             | Run Until RTS/RTE/VBL/HBL/RAM                |
| Ctrl+Shift+U  | Cycle "Run Until.." choice                   |
| Ctrl+U, S     | Run To RTS                                   |
| Ctrl+U, E     | Run To RTE                                   |
| Ctrl+U, V     | Run To VBL                                   |
| Ctrl+U, H     | Run To HBL                                   |
| Ctrl+U, R     | Run To RAM (exits e.g. TOS, cartridge)       |
| Alt+Shift+B   | Add breakpoint (dialog)                      |

Window focus:
| Key           | Operation                |
|---------------|--------------------------|
| Alt+D         | Focus Disassembly View   |
| Alt+M         | Focus Memory View 1      |
| Alt+2         | Focus Memory View 2      |
| Alt+3         | Focus Memory View 3      |
| Alt+4         | Focus Memory View 4      |
| Alt+G         | Focus Graphics Inspector |
| Alt+B         | Focus Breakpoints View   |
| Alt+H         | Focus Hardware View      |
| Alt+P         | Focus Profile Window     |
| Alt+C         | Focus Console Window     |

Running Hatari:
| Key           | Operation                     |
|---------------|-------------------------------|
| Alt+L         | Launch Dialog (to run Hatari) |
| Alt+Q         | QuickLaunch                   |

Visual Studio control:

| Key           | Operation                        |
|---------------|----------------------------------|
| F5            | Start/stop                       |
| F10           | Step (execute next instruction)  |
| F11           | Next (step through bsr/jsr)      |

Disassembly focus
=================
| Key           | Operation                           |
|---------------|-------------------------------------|
| Up/Down       | Move Cursor                         |
| Page Up/Down  | Move by several lines               |
| Ctrl+B        | Toggle breakpoint (cursor position) |
| Ctrl+F        | Search for text/hexadecimal string  |
| Ctrl+H        | Run to "Here" (cursor position)     |
| Ctrl+Space    | Show Context Menu                   |

Visual Studio control:
| Key           | Operation                           |
|---------------|-------------------------------------|
| F9            | Toggle breakpoint (cursor position) |
| Ctrl+F10      | Run to "Here" (cursor position)     |

Memory Window focus
===================
| Key           | Operation                          |
|---------------|------------------------------------|
| Cursor kyes   | Move editing cursor                |
| Page Up/Down  | Move to previous/next page         |
| Ctrl+F        | Search for text/hexadecimal string |
| Ctrl+W        | Write memory to file               |
| Ctrl+Space    | Show Context Menu                  |

Graphics Inspector focus
========================
| Key           | Operation               |
|---------------|-------------------------|
| Up/Down       | move by one bitmap line |
| Shift+Up/Down | move by 8 lines         |
| Left/Right    | move by 2 bytes         |
| Ctrl+Space    | Show Context Menu       |

