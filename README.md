This is a GUI for OpenAI's Whisper TTS model... or anything with a compatible API.

![main_window](https://github.com/user-attachments/assets/267aae50-adab-4da0-b647-3f093eb92895)

# Project status

As you might have guessed from the existence of various buttons for which there isn't a lot of explanation, this is a side project of mine that I have, for some reason, decided to put on the internet. Do not expect it to work in any meaningful way. (It might work though.)

The code might also provide excellent examples for

- how to do some stuff in win32 (tray icons! global hotkeys! COM automation!)
- how to _not_ do stuff in win32 (the code is ugly, could use some cleanup, and this was my first attempt at tray icons, global hotkeys and COM automation.)

# Usage

You hit record. You talk. You then press the button again (it is now labeled "Stop").

This will cause your recorded speech to be converted to MP3 and sent to Whisper. Once it responds, we insert the result into the application in the foreground.

You might already have noticed that this has questionable levels of usability, given how "the application in the front" is this GUI. To solve this, we are registering a global hotkey on F8. This corresponds to the record / stop button.

## Automation

We also provide a command line utility, `whisper32cmd`, that lets you use an already running instance of the GUI program from the command line.

```
> whisper32cmd start
Recording started.
```

presses the button for you. You talk. Then you invoke

```
> whisper32cmd stop
Recording stopped. Result: Hey this is some stuff that I said between the two commands.

```

This is using COM to communicate; no TCP ports involved! It also works under Wine, on arbitrary operating systems.


# Setup

![settings](https://github.com/user-attachments/assets/22c8af2b-4bb8-474c-b75d-191c8659202a)

You should start by either setting an OpenAI API key for the official OpenAI Whisper API, or, if you're running Whisper locally, pointing it at an endpoint that has the same API.

# Building

You need libcurl & liblame to be available in `c:\devel` to compile this. At some point I should put the zip file containing them somewhere.

# FAQ

## How do we insert the text?

If the application in the foreground happens to be Emacs, we try to connect to its server and insert the text that way. For everyone else, we copy the text to the clipboard and send a literal Ctrl-V to the app in the foreground.



