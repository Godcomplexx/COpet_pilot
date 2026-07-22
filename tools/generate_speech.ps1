# Generates the concatenative speech vocabulary for CoPet's spoken time/weather.
#
# Uses the built-in Windows SAPI voice to render each word to WAV, then ffmpeg
# to trim silence and convert to 8 kHz mono signed-16-bit little-endian PCM
# (the format copet_audio plays, upsampled x2 to the 16 kHz I2S bus).
#
# Output: main/assets/speech/word_*.pcm  (git-ignored; regenerate as needed).
# Requires: Windows PowerShell (System.Speech) and ffmpeg on PATH.

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Speech

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root "main\assets\speech"
$tmpDir = Join-Path $env:TEMP "copet_speech_tmp"
New-Item -ItemType Directory -Force $outDir | Out-Null
New-Item -ItemType Directory -Force $tmpDir | Out-Null

# name -> spoken text. `name` becomes word_<name>.pcm and the embedded symbol.
$words = [ordered]@{
    zero = "zero"; one = "one"; two = "two"; three = "three"; four = "four"
    five = "five"; six = "six"; seven = "seven"; eight = "eight"; nine = "nine"
    ten = "ten"; eleven = "eleven"; twelve = "twelve"; thirteen = "thirteen"
    fourteen = "fourteen"; fifteen = "fifteen"; sixteen = "sixteen"
    seventeen = "seventeen"; eighteen = "eighteen"; nineteen = "nineteen"
    twenty = "twenty"; thirty = "thirty"; forty = "forty"; fifty = "fifty"
    degrees = "degrees"; minus = "minus"; oh = "oh"; oclock = "o'clock"
    clear = "clear sky"; cloudy = "cloudy"; fog = "foggy"; rain = "rain"
    snow = "snow"; storm = "thunderstorm"
}

$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synth.Rate = 1        # a touch quicker so phrases do not drag
$synth.Volume = 100

$count = 0
foreach ($name in $words.Keys) {
    $wav = Join-Path $tmpDir "$name.wav"
    $pcm = Join-Path $outDir "word_$name.pcm"
    $synth.SetOutputToWaveFile($wav)
    $synth.Speak($words[$name])
    $synth.SetOutputToNull()

    # Trim near-silence, boost loudness with a limiter (louder without clipping),
    # then 8 kHz / mono / s16le raw PCM.
    & ffmpeg -y -loglevel error -i $wav `
        -af "silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.02:stop_periods=-1:stop_threshold=-45dB:stop_silence=0.05,volume=9dB,alimiter=level_in=1:level_out=1:limit=0.97" `
        -ar 8000 -ac 1 -f s16le $pcm
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed for $name" }
    $count++
}
$synth.Dispose()

$bytes = (Get-ChildItem $outDir -Filter "word_*.pcm" | Measure-Object Length -Sum).Sum
Write-Host "Generated $count word clips, $([math]::Round($bytes/1024)) KB total in $outDir"
