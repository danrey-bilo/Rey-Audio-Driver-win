# Ordinary Windows audio endpoints

The current delivered USB-ASIO EXE gives Ableton/ASIO eight inputs and eight
outputs. It does **not** install system microphones/speakers for Chrome or
Telegram. The Mixer identifies this mode explicitly; Windows Sound is disabled.

The separate Windows endpoint experiment is now restricted to one USB card.
Its mapping code describes four stereo input/output pairs (1/2, 3/4, 5/6,
7/8) plus one full 1–8 input/output pair: ten endpoints, not ten cards.
Channel mapping contracts pass, but this is not proof of a loaded sound driver.

The unsigned ACX development backend and optional signed TAG evaluation
backend are outside the installed ASIO path. TAG's shared Windows experiment
has an unresolved PCM marker failure; ACX loading in normal Windows is not
qualified. The main package has no dependency on either backend, no custom
SYS and no boot/security setting changes. Their historical evidence is kept
separately. Production virtual Windows audio remains a later qualification
stage. The <2 ms target applies to ASIO, not browser/communication buffers.
