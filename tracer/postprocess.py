import argparse
import util
import struct

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('input_trace')
    parser.add_argument('output_trace')
    parser.add_argument('cache_size', type=int)
    parser.add_argument('batch_size', type=int)
    args = parser.parse_args()

    modifications = util.trace_to_mod_list(args.input_trace, args.cache_size, args.batch_size)

    with open(args.output_trace, 'wb') as f:
        for access in modifications:
            for fetch in modifications[access].get_accesses():
                f.write(struct.pack('q', fetch))
