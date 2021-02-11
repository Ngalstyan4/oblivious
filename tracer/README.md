# Tracer API

Defines the public api for tracing recording, processing and prefetching
Adapted (Chris's LRU simulator) into util.py here in order to post-process raw trace.
Todo:: Maybe in the future we can replace the python script with a C device file which can do postprocessing on the fly as
it is being recorded? This way the raw trace will never have to be spilled on disc and prefetching process will be even more streamlined.
