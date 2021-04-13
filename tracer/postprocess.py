import argparse
import util
import os
import re
import struct
from multiprocessing import Pool

if __name__ == "__main__":
    ALL_RATIOS = list(range(100,0,-10))
    parser = argparse.ArgumentParser()
    parser.add_argument('input_trace_prefix')
    parser.add_argument('rss', help='workload resident set size *in pages*', type=int)
    parser.add_argument('batch_size', type=int)
    parser.add_argument('ratios', nargs='*', default=ALL_RATIOS, type=int)
    args = parser.parse_args()

    path = os.path.normpath(args.input_trace_prefix)
    input_dir = os.path.dirname(path)
    input_file_prefix = os.path.basename(path)
    output_file_prefix = re.sub('.bin$', '.tape', input_file_prefix)
    input_files = []

    for file in os.listdir(input_dir):
        if file.startswith(input_file_prefix):
            input_files.append(file)

    inputs = [(f, r, args.batch_size) for f in input_files for r in args.ratios]

    print("Postprocessing raw trace at %s.*", args.input_trace_prefix)
    print("RSS: %d pages, %.2f MB" %(args.rss, args.rss * 4096 / 1024 / 1024))
    print("For local memory ratios %s (in percent)", args.ratios)
    cache_sizes = [r * args.rss / 100 for r in args.ratios]
    print("For local memory sizes %s (in pages)", cache_sizes)
    print("For local memory sizes %s (in MB)", [c * 4096 / 1024 / 1024 for c in cache_sizes])
    print("")
    print("Output will be at %s", os.path.join(input_dir, "[LOCAL_MEMORY_RATIO]", output_file_prefix +  ".[THREAD_ID]"))


    def work(workarg):
        input_file, ratio, batch_size = workarg
        input_file_path = os.path.join(input_dir, input_file)
        modifications = util.trace_to_mod_list(input_file_path, 0.7 * ratio * args.rss // 100, batch_size, pkl=False)
        input_file_suffix = input_file[len(input_file_prefix):]
        output_trace = os.path.join(input_dir,  str(ratio), output_file_prefix +  input_file_suffix)
        print("final", output_trace)

        os.makedirs(os.path.dirname(output_trace), exist_ok=True)
        with open(output_trace, 'wb') as f:
            for access in modifications:
                for fetch in modifications[access].get_accesses():
                    f.write(struct.pack('q', fetch))


    with Pool(min(10, len(inputs))) as p:
        p.map(work, inputs)
