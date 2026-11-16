#!/usr/bin/env python3
"""Review AVI streams with ffprobe instead of assuming a successful write."""
import json
import subprocess
import sys
def probe(path):
    return json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_entries",
        "stream=index,codec_name,codec_type,width,height,sample_rate,channels,nb_frames",
        "-of", "json", path], text=True))["streams"]
streams=probe(sys.argv[1])
assert len(streams)==2, streams
video=next(s for s in streams if s["codec_type"]=="video")
audio=next(s for s in streams if s["codec_type"]=="audio")
assert video["codec_name"]=="rawvideo", video
assert (video["width"],video["height"])==(240,160),video
assert audio["codec_name"]=="pcm_s16le",audio
assert (int(audio["sample_rate"]),int(audio["channels"]))==(32768,2),audio
assert int(video["nb_frames"])==10,video
print("AVI validation passed: 10 rawvideo frames, 32768Hz stereo PCM")
