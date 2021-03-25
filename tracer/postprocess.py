import argparse
import util
import os
import struct
from multiprocessing import Pool

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('input_trace_prefix')
    parser.add_argument('output_trace_prefix')
    parser.add_argument('cache_size', type=int)
    parser.add_argument('batch_size', type=int)
    args = parser.parse_args()

    path_parts = os.path.normpath(args.input_trace_prefix).split("/")
    input_dir = "/".join(path_parts[:-1])
    input_file_prefix = path_parts[-1]
    input_files = []

    for file in os.listdir(input_dir):
        if file.startswith(input_file_prefix):
            input_files.append(file)


    def work(input_file):
        input_file_path = os.path.join(input_dir, input_file)
        modifications = util.trace_to_mod_list(input_file_path, args.cache_size, args.batch_size)
        input_file_suffix = input_file[len(input_file_prefix):]
        output_trace = os.path.join(input_dir, args.output_trace_prefix + input_file_suffix)

        with open(output_trace, 'wb') as f:
            for access in modifications:
                for fetch in modifications[access].get_accesses():
                    f.write(struct.pack('q', fetch))
    with Pool(len(input_files)) as p:
        p.map(work, input_files)
