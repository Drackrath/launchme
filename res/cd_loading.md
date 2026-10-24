# CD loading cue

Source: [CD Player Sound Effect](https://www.youtube.com/watch?v=IAuvHMP98R4), 4.000–8.000 s.

Downloaded with `yt-dlp --download-sections '*4-8' --force-keyframes-at-cuts -f bestaudio`.
Converted with FFmpeg to 48 kHz stereo PCM16 WAV, gain +34 dB (source peak −40.5 dBFS),
15 ms fade-in and 200 ms fade-out. Playback gain is unity; duration is 4 seconds.
The file is bundled in `assets/cd_loading.wav` alongside the executable.
